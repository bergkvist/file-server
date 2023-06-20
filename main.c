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
#define DEFAULT_PORT 5000
#define FILE_BUFFER_SIZE 4096


int create_ipv4_socket_and_listen(char *host, uint16_t port);
int parse_http_status_line(char *status_line, char **method, char **path, char **version);
size_t read_filesize(FILE *file);
int is_directory(const char *path);
int render_directory_as_html(FILE *out, DIR *d, const char *path);


int main(int argc, char **argv) {
    char *host = DEFAULT_HOST;
    uint16_t port = DEFAULT_PORT;
    if (argc > 1) host = argv[1];
    if (argc > 2) port = strtol(argv[2], &argv[2], 10);

    int sock = create_ipv4_socket_and_listen(host, port);
    printf("Listening on http://%s:%hu\n", host, port);

    while (1) {
        int connection = accept(sock, NULL, NULL);
        char *http_status = NULL;
        size_t http_status_length = 0;
        FILE *connection_handle = fdopen(connection, "r+");
        getline(&http_status, &http_status_length, connection_handle);
        char *response_status;

        char *http_method, *http_path;
        if (parse_http_status_line(http_status, &http_method, &http_path, NULL) < 0) {
            response_status = "401 BAD REQUEST";
            fprintf(connection_handle, "HTTP/1.1 %s\r\nContent-Length: 16\r\n\r\n401 Bad Request\n", response_status);
            fflush(connection_handle);
            goto cleanup;
        }

        // Open requested file for reading if it exists
        char *relative_filepath;
        assert_ok(asprintf(&relative_filepath, ".%s", http_path));
        if (is_directory(relative_filepath)) {
            char *buffer;
            size_t buffer_length;
            FILE *buffer_stream = open_memstream(&buffer, &buffer_length);

            DIR *d = opendir(relative_filepath);
            render_directory_as_html(buffer_stream, d, http_path);
            closedir(d);
            
            response_status = "200 OK";
            fprintf(connection_handle, "HTTP/1.1 %s\r\nContent-Type: text/html\r\nContent-Length: %lu\r\n\r\n", response_status, buffer_length);
            fwrite(buffer, 1, buffer_length, connection_handle);
            fflush(connection_handle);

            fclose(buffer_stream);
            free(buffer);
            goto cleanup;
        }

        FILE *file;
        if ((file = fopen(relative_filepath, "r")) == NULL) {
            response_status = "404 NOT FOUND";
            fprintf(connection_handle, "HTTP/1.1 %s\r\nContent-Length: 14\r\n\r\n404 Not Found\n", response_status);
            fflush(connection_handle);
            goto cleanup;
        }

        // Send HTTP Response Headers
        size_t filesize = read_filesize(file);
        response_status = "200 OK";
        fprintf(connection_handle, "HTTP/1.1 %s\r\nContent-Length: %lu\r\n\r\n", response_status, filesize);
        fflush(connection_handle);

        // Send file contents to socket
        char file_buffer[FILE_BUFFER_SIZE];
        size_t bytes_read;
        while ((bytes_read = fread(file_buffer, 1, FILE_BUFFER_SIZE, file))) {
            write(connection, file_buffer, bytes_read);
        }
        fclose(file);

    cleanup:
        printf("[%s %s] => (%s)\n", http_method, http_path, response_status);
        free(relative_filepath);
        free(http_status);
        fclose(connection_handle);
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
    struct stat statbuf;
    stat(path, &statbuf);
    return S_ISDIR(statbuf.st_mode);
}

int render_directory_as_html(FILE *out, DIR *d, const char *path) {
    fprintf(out, "<code>\n<h1>Directory: %s</h1>\n<ul>\n", path);
    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
        size_t len = strlen(path);
        fprintf(out, "<li><a href=\"");
        if (len > 0 && path[len - 1] == '/') {
            fprintf(out, "%s%s", path, dir->d_name);
        } else {
            fprintf(out, "%s/%s", path, dir->d_name);
        }
        fprintf(out, "\">%s</a></li>\n", dir->d_name);
    }
    fprintf(out, "</ul>\n</code>\n");
    fflush(out);
    return 0;
}
