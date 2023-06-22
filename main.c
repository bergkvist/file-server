#define _GNU_SOURCE 2
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <ctype.h>

#define assert_ok(e) ({int x = (e); if (x < 0) { printf("%s:%d: ", __FILE__, __LINE__); fflush(stdout); perror(#e); abort(); } x;})
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 1233
#define FILE_BUFFER_SIZE 8192
#define MAX_HEADER_SIZE 8192

int create_ipv4_socket_and_listen(char *host, uint16_t port);
size_t read_filesize(FILE *file);
int is_directory(const char *path);
int render_directory_as_html(char **html, size_t *html_length, DIR *d, const char *path);
void urlencode(char *dst, const char *src);
void urldecode(char *dst, const char *src);
void log_http_request_response(char *method, char *path, char *relative_filepath, char *response_status);

const char response_200_ok[] = "HTTP/1.1 200 OK\r\nContent-Length: ";
const char response_200_ok_html[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF8\r\nContent-Length: ";
const char response_400_bad_request[] = "HTTP/1.1 400 BAD REQUEST\r\nContent-Length: 16\r\n\r\n400 Bad Request\n";
const char response_404_not_found[] = "HTTP/1.1 404 NOT FOUND\r\nContent-Length: 14\r\n\r\n404 Not Found\n";

int main(int argc, char **argv) {
    char *host = DEFAULT_HOST;
    uint16_t port = DEFAULT_PORT;
    if (argc > 1) host = argv[1];
    if (argc > 2) port = strtol(argv[2], &argv[2], 10);

    int sock = create_ipv4_socket_and_listen(host, port);
    printf("Listening on http://%s:%hu\n", host, port);

    while (1) {
        int connection = accept(sock, NULL, NULL);
        char http_headers[MAX_HEADER_SIZE];
        size_t header_size = 0;
        if ((header_size = read(connection, http_headers, MAX_HEADER_SIZE)) <= 0) {
            close(connection);
            continue;
        }
        char *http_method = http_headers;
        char *http_path = NULL;
        for (int i = 0; i < MAX_HEADER_SIZE; ++i) {
            if (http_headers[i] == ' ') {
                http_headers[i] = 0;
                if (http_path == NULL) http_path = &http_headers[i+1];
            }
            if (http_headers[i] == '\r' || http_headers[i] == '\n') {
                http_headers[i] = 0;
                break;
            }
        }

        if (http_path == NULL) {
            log_http_request_response(http_headers, "", "", "400 BAD REQUEST");
            write(connection, response_400_bad_request, (sizeof response_400_bad_request) - 1);
            close(connection);
            continue;
        }

        size_t relative_filepath_length = strlen(http_path) + 2;
        char *relative_filepath = malloc(relative_filepath_length);
        relative_filepath[0] = '.';
        urldecode(&relative_filepath[1], http_path);

        if (is_directory(relative_filepath)) {
            char *html;
            size_t html_length;
            DIR *dir = opendir(relative_filepath);
            render_directory_as_html(&html, &html_length, dir, http_path);
            closedir(dir);

            char *response_buffer;
            size_t response_length = asprintf(&response_buffer, "%s%lu\r\n\r\n%s", response_200_ok_html, html_length, html);
            write(connection, response_buffer, response_length);
            log_http_request_response(http_method, http_path, relative_filepath, "200 OK");
            free(response_buffer);
            free(html);
            free(relative_filepath);
            close(connection);
            continue;
        }

        FILE *file;
        if ((file = fopen(relative_filepath, "r")) == NULL) {
            write(connection, response_404_not_found, (sizeof response_404_not_found) - 1);
            log_http_request_response(http_method, http_path, relative_filepath, "404 NOT FOUND");
            free(relative_filepath);
            close(connection);
            continue;
        }

        // Send HTTP Response Headers
        size_t filesize = read_filesize(file);
        char *response_buffer;
        size_t response_length = asprintf(&response_buffer, "%s%lu\r\n\r\n", response_200_ok, filesize);
        write(connection, response_buffer, response_length);

        // Send file contents to socket
        char file_buffer[FILE_BUFFER_SIZE];
        size_t bytes_read;
        while ((bytes_read = fread(file_buffer, 1, FILE_BUFFER_SIZE, file))) {
            write(connection, file_buffer, bytes_read);
        }

        log_http_request_response(http_method, http_path, relative_filepath, "200 OK");
        free(relative_filepath);
        fclose(file);
        close(connection);
    }
}

int create_ipv4_socket_and_listen(char *host, uint16_t port) {
    int reuseaddr = 1;
    int listen_backlog = 10;
    struct sockaddr_in listen_address = {
        .sin_port = htons(port),
        .sin_addr.s_addr = inet_addr(host),
        .sin_family = AF_INET
    };
    int sock = assert_ok(socket(AF_INET, SOCK_STREAM, IPPROTO_IP));
    assert_ok(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof reuseaddr));
    assert_ok(bind(sock, (struct sockaddr*)&listen_address, sizeof listen_address));
    assert_ok(listen(sock, listen_backlog));
    return sock;
}

size_t read_filesize(FILE *file) {
    fseek(file, 0L, SEEK_END);
    size_t size = ftell(file);
    fseek(file, 0L, SEEK_SET);
    return size;
}

int is_directory(const char *path) {
    struct stat statbuf = {};
    stat(path, &statbuf);
    return S_ISDIR(statbuf.st_mode);
}

int render_directory_as_html(char **html, size_t *html_length, DIR *d, const char *path) {
    FILE *stream = open_memstream(html, html_length);
    fprintf(stream, "<code>\n<h1>Directory: %s</h1>\n<ul>\n", path);
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        size_t len = strlen(path);
        fprintf(stream, "<li><a href=\"");
        char *raw_path = NULL;
        size_t raw_path_len = 0;
        if (len > 0 && path[len - 1] == '/') {
            raw_path_len = asprintf(&raw_path, "%s%s", path, entry->d_name);
        } else {
            raw_path_len = asprintf(&raw_path, "%s/%s", path, entry->d_name);
        }
        char *encoded_path = malloc(3 * raw_path_len + 2);
        urlencode(encoded_path, raw_path);
        fprintf(stream, "%s", encoded_path);
        free(encoded_path);
        free(raw_path);
        fprintf(stream, "\">%s</a></li>\n", entry->d_name);
    }
    fprintf(stream, "</ul>\n</code>\n");
    fflush(stream);
    fclose(stream);
    return 0;
}

// https://en.wikipedia.org/wiki/URL_encoding
// chars that need to be encoded: [<33, 34, 37, 60, 62, 92, 94, 96, 123, 124, 125, >126]
void urlencode(char *dst, const char *src) {
    char c;
    while ((c = *src++)) {
        if (c == ' ') *dst++ = '+';
        else if (
            c  < '!' || c  > '~' || c ==  '"' || c == '%' ||
            c == '<' || c == '>' || c == '\\' || c == '^' ||
            c == '`' || c == '{' || c ==  '|' || c == '}'
        ) {
            char hi = (c & 0xF0) >> 4;
            char lo = (c & 0x0F);
            *dst++ = '%';
            *dst++ = hi + (hi < 10) * ('0') + (hi >= 10) * ('a' - 10);
            *dst++ = lo + (lo < 10) * ('0') + (lo >= 10) * ('a' - 10);
        }
        else *dst++ = c;
    }
    *dst++ = 0;
}

void urldecode(char *dst, const char *src) {
    while (*src) {
        char c1;
        char c2;
        if ((*src == '%') && (c1 = src[1]) && (c2 = src[2]) && isxdigit(c1) && isxdigit(c2)) {
            c1 -= ((c1 >= 'a' && c1 <= 'f') * ('a' - 10) +
                   (c1 >= 'A' && c1 <= 'F') * ('A' - 10) +
                   (c1 >= '0' && c1 <= '9') * ('0' - 0));
            c2 -= ((c2 >= 'a' && c2 <= 'f') * ('a' - 10) +
                   (c2 >= 'A' && c2 <= 'F') * ('A' - 10) +
                   (c2 >= '0' && c2 <= '9') * ('0' - 0));
            *dst++ = 16*c1 + c2;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst++ = 0;
}

void log_http_request_response(char *method, char *path, char *relative_filepath, char *response_status) {
    printf("[%s %s] => %s (%s)\n", method, path, response_status, relative_filepath);
}
