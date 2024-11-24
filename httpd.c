#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include "net.h"

typedef enum {
    GET, 
    HEAD, 
    INVALID 
} request_type;

#define VERSION "HTTP/1.0"

char *format_error(const char *type, const char *explanation, off_t *content_length);
void responder(int socket, char* version, char* status, off_t content_length, char* content);
void handle_request(int nfd);
void run_service(int fd);
request_type parse_request(const char *line, char *filename);


int main(int argc, char const *argv[])
{
    // ARGUMENT VERIFICATION
    if (argc != 2) {
        printf("ERROR: expected ./httpd <SERVER PORT>\n");
        return 1;
    }

    int port = atoi(argv[1]);
    printf("port number: %i\n", port);
    if (port == 0) {
        printf("ERROR: INVALID PORT - Port must be between 1024 and 65535 [GOT: %s]\n", argv[1]);
        return 1;
    } else if (port < 1024 || port > 65535) {
        printf("ERROR: INVALID PORT - Port must be between 1024 and 65535 [GOT: %s]\n", argv[1]);
        return 1;
    }

    // IMPORT FROM SERVER.C [LAB 7]
    int fd = create_service(port);

    if (fd == -1) {
        perror(0);
        exit(1);
    }

    printf("listening on port: %d\n", port);
    run_service(fd);
    close(fd);

    return 0;
}

char *format_error(const char *type, const char *explanation, off_t *content_length) {
    static char formatted_html[512];
    int len = snprintf(formatted_html, sizeof(formatted_html),
             "<html><body><h1>%s</h1><p>%s</p></body></html>",
             type, explanation);
    *content_length = len;
    return formatted_html;
}

void responder(int socket, char* version, char* status, off_t content_length, char* content) {
    char* response = malloc((256+content_length)*sizeof(char));

    sprintf(response, "%s %s\r\n" 
        "Content-Type: text/html\r\n"
		"Content-Length: %lld\r\n"
		"\r\n"
		"%s",
        version, status, (long long)content_length, content
    );

    write(socket, response, strlen(response));

    // CLEANUP AND CLOSE
    free(response);
	close(socket);
}

request_type parse_request(const char *line, char *filename) {
    char type[1024];
    char version[1024];

    if (sscanf(line, "%s %s %s", type, filename, version) != 3) {
        return INVALID;
    }

    printf("Parsed type: %s, filename: %s, version: %s\n", type, filename, version);


    if (strcmp(type, "GET") == 0) {
        return GET;
    } else if (strcmp(type, "HEAD") == 0) {
        return HEAD;
    } else {
        return INVALID;
    }
}

void handle_request(int nfd) {
    FILE *network = fdopen(nfd, "r");
    char *line = NULL;
    size_t size;
    ssize_t num = getline(&line, &size, network);

    if (network == NULL) {
        printf("ERROR: failed to open file\n");
        close(nfd);
        return;
    }

    if (num <= 0) {
        free(line);
        fclose(network);
        return;
    }

    char filename[2048];
    request_type req_type = parse_request(line, filename);
    off_t content_length = 0;

    if (filename[0] == '/') {
        memmove(filename, filename + 1, strlen(filename));
    }

    // if there is no filename, then default to "index.html"
    if (strlen(filename) == 0) {
        strcpy(filename, "index.html");
    }

    // prepend "public/" directory to the filepath
    char filepath[2048] = "public/";
    strcat(filepath, filename);

    // REQUIREMENT 5: CGI-LIKE IMPLEMENTATION
    if (strncmp(filename, "cgi-like/", 9) == 0) {
        if (strstr(filename, "..") != NULL) {
            char *error = format_error("400 Bad Request", "Directory traversal is not allowed.", &content_length);
            responder(nfd, VERSION, "400 Bad Request", content_length, error);

            free(line);
            fclose(network);
            return;
        }

        char pgrm[2048];
        // find first occurence in a str - strchr(string, <char to be searched for>)
        char *pgrm_argv = strchr(filename, '?');
        if (pgrm_argv != NULL) {
            *pgrm_argv = '\0';
            pgrm_argv++;
        }

        // create the path to the program
        snprintf(pgrm, sizeof(pgrm), "./%s", filename);

        if (access(pgrm, X_OK) == -1) {
            char *error = format_error("404 Not Found", "The requested CGI program was not found or is not executable.", &content_length);
            responder(nfd, VERSION, "404 Not Found", content_length, error);

            free(line);
            fclose(network);
            return;
        }

        pid_t pid = fork();
        if (pid < 0) {
            char *error = format_error("500 Internal Error", "Failed to create a child process.", &content_length);
            responder(nfd, VERSION, "500 Internal Error", content_length, error);

            free(line);
            fclose(network);
            return;
        } else if (pid == 0) {
            // EXECUTE CHILD PROCESS
            char tmp_filename[64];
            snprintf(tmp_filename, sizeof(tmp_filename), "/tmp/cgi_output_%d", getpid());

            FILE *output = freopen(tmp_filename, "w", stdout);
            if (!output) {
                printf("ERROR: failed to redirect output to terminal\n");
                exit(1);
            }

            if (pgrm_argv) {
                char *args[2048];
                int arg_count = 0;

                args[arg_count++] = pgrm;
                char *token = strtok(pgrm_argv, "&");

                while (token != NULL && arg_count < 2047) {
                    args[arg_count++] = token;
                    token = strtok(NULL, "&");
                }

                args[arg_count] = NULL;
                execv(pgrm, args);
            } else {
                execl(pgrm, pgrm, NULL);
            }

            printf("ERROR: failed to execute CGI program\n");
            exit(1);
        } else {
            // EXECUTE PARENT PROCESS
            int status;
            waitpid(pid, &status, 0);

            char tmp_filename[64];
            snprintf(tmp_filename, sizeof(tmp_filename), "/tmp/cgi_output_%d", pid);

            FILE *output = fopen(tmp_filename, "r");
            if (!output) {
                char *error = format_error("500 Internal Error", "Failed to read CGI program output.", &content_length);
                responder(nfd, VERSION, "500 Internal Error", content_length, error);
                free(line);
                fclose(network);
                return;
            }

            fseek(output, 0, SEEK_END);
            content_length = ftell(output);
            rewind(output);

            char *content = malloc(content_length);
            if (!content) {
                char *error = format_error("500 Internal Error", "Memory allocation failed.", &content_length);
                responder(nfd, VERSION, "500 Internal Error", content_length, error);

                fclose(output);
                free(line);
                fclose(network);
                return;
            }

            fread(content, 1, content_length, output);
            fclose(output);
            remove(tmp_filename);

            responder(nfd, VERSION, "200 OK", content_length, content);
            free(content);
        }

        free(line);
        fclose(network);
        return;
    }

    // 404 ERROR FILE CHECK
    if (access(filepath, F_OK) == -1) {
        char *error = format_error("404 Not Found", "The requested file was not found.", &content_length);
        responder(nfd, VERSION, "404 Not Found", content_length, error);
        free(line);
        fclose(network);
        return;
    }

    // 403 ERROR FILE CHECK
    if (access(filepath, R_OK) == -1) {
        char *error = format_error("403 Permission Denied", "The server does not have permission to access the requested file.", &content_length);
        responder(nfd, VERSION, "403 Permission Denied", content_length, error);
        free(line);
        fclose(network);
        return;
    }

    FILE *fd = fopen(filepath, "r");
    if (fd == NULL) {
        char *error = format_error("404 Not Found", "The requested file was not found.", &content_length);
        responder(nfd, VERSION, "404 Not Found", content_length, error);
        free(line);
        fclose(network);
        return;
    }

    fseek(fd, 0, SEEK_END);
    content_length = ftell(fd);
    rewind(fd);

    if (req_type == GET) {
        char *content = malloc(content_length);
        if (content == NULL) {
            char *error = format_error("500 Internal Error", "Memory allocation failed.", &content_length);
            responder(nfd, VERSION, "500 Internal Error", content_length, error);

            fclose(fd);
            free(line);
            fclose(network);
            return;
        }

        fread(content, 1, content_length, fd);
        responder(nfd, VERSION, "200 OK", content_length, content);
        free(content);
    } else if (req_type == HEAD) {
        responder(nfd, VERSION, "200 OK", content_length, NULL);
    }

    fclose(fd);
    free(line);
    fclose(network);
}

void run_service(int fd) {
    while (1)
    {
        int nfd = accept_connection(fd);
        if (nfd != -1)
        {
            printf("Connection established\n");
            handle_request(nfd);
            printf("Connection closed\n");
        }
    }
}
