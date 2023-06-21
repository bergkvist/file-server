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

#define assert_ok(e) ({int x = (e); if (x < 0) { printf("%s:%d: ", __FILE__, __LINE__); fflush(stdout); perror(#e); abort(); } x;})
#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 1233
#define FILE_BUFFER_SIZE 4096
#define RECV_BUFFER_SIZE 4096


int create_ipv4_socket_and_listen(char *host, uint16_t port);
int parse_http_status_line(char *status_line, char **method, char **path, char **version);
size_t read_filesize(FILE *file);
int is_directory(const char *path);
int render_directory_as_html(char **html, size_t *html_length, DIR *d, const char *path);
void log_http_request_response(char *method, char *path, char *response_status);

int main(int argc, char **argv) {
    char *host = DEFAULT_HOST;
    uint16_t port = DEFAULT_PORT;
    if (argc > 1) host = argv[1];
    if (argc > 2) port = strtol(argv[2], &argv[2], 10);

    int sock = create_ipv4_socket_and_listen(host, port);
    printf("Listening on http://%s:%hu\n", host, port);

    while (1) {
        int connection = accept(sock, NULL, NULL);
        char http_status[RECV_BUFFER_SIZE];
        read(connection, http_status, RECV_BUFFER_SIZE);

        char *response = NULL;
        size_t response_length = 0;
        FILE *response_stream = open_memstream(&response, &response_length);

        char *http_method, *http_path;
        if (parse_http_status_line(http_status, &http_method, &http_path, NULL) < 0) {
            log_http_request_response(http_method, http_path, "400 BAD REQUEST");
            fprintf(response_stream, "HTTP/1.1 400 BAD REQUEST\r\nContent-Length: 16\r\n\r\n400 Bad Request\n");
            fflush(response_stream);
            write(connection, response, response_length);
            goto cleanup;
        }

        // Open requested file for reading if it exists
        char *relative_filepath;
        assert_ok(asprintf(&relative_filepath, ".%s", http_path));
        if (is_directory(relative_filepath)) {
            char *html;
            size_t html_length;
            DIR *dir = opendir(relative_filepath);
            render_directory_as_html(&html, &html_length, dir, http_path);
            closedir(dir);
            fprintf(response_stream, "HTTP/1.1 200 OK\r\nContent-Type: text/html\nContent-Length: %lu\r\n\r\n", html_length);
            fwrite(html, 1, html_length, response_stream);
            fflush(response_stream);
            write(connection, response, response_length);
            log_http_request_response(http_method, http_path, "200 OK");
            free(html);
            goto cleanup;
        }

        FILE *file;
        if ((file = fopen(relative_filepath, "r")) == NULL) {
            fprintf(response_stream, "HTTP/1.1 404 NOT FOUND\r\nContent-Length: 14\r\n\r\n404 Not Found\n");
            fflush(response_stream);
            write(connection, response, response_length);
            log_http_request_response(http_method, http_path, "404 NOT FOUND");
            goto cleanup;
        }

        // Send HTTP Response Headers
        size_t filesize = read_filesize(file);
        fprintf(response_stream, "HTTP/1.1 200 OK\r\nContent-Length: %lu\r\n\r\n", filesize);
        fflush(response_stream);
        write(connection, response, response_length);
        log_http_request_response(http_method, http_path, "200 OK");

        // Send file contents to socket
        char file_buffer[FILE_BUFFER_SIZE];
        size_t bytes_read;
        while ((bytes_read = fread(file_buffer, 1, FILE_BUFFER_SIZE, file))) {
            write(connection, file_buffer, bytes_read);
        }
        fclose(file);

    cleanup:
        free(relative_filepath);
        close(connection);
        fclose(response_stream);
        free(response);
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

int parse_http_status_line(char *status_line, char **method, char **path, char **version) {
    if (status_line == NULL) return -1;
    
    char *_method, *_path, *_version, *tmp;
    _method = status_line;
    _path = strchr(_method, ' ');
    if (_path == NULL) return -2;
    _version = strchr(_path+1, ' ');
    if (_version == NULL) return -2;

    (_path++)[0] = 0;
    (_version++)[0] = 0;
    if ((tmp = strchr(_version, '\r'))) tmp[0] = 0;
    if ((tmp = strchr(_version, '\n'))) tmp[0] = 0;
    
    if (method != NULL) *method = _method;
    if (path != NULL) *path = _path;
    if (version != NULL) *version = _version;
    return 0;
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
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        size_t len = strlen(path);
        fprintf(stream, "<li><a href=\"");
        if (len > 0 && path[len - 1] == '/') {
            fprintf(stream, "%s%s", path, dir->d_name);
        } else {
            fprintf(stream, "%s/%s", path, dir->d_name);
        }
        fprintf(stream, "\">%s</a></li>\n", dir->d_name);
    }
    fprintf(stream, "</ul>\n</code>\n");
    fflush(stream);
    fclose(stream);
    return 0;
}

void log_http_request_response(char *method, char *path, char *response_status) {
    printf("[%s %s] => (%s)\n", method, path, response_status);
}
