#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "libhttp.h"
#include "wq.h"

/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
wq_t work_queue;
int num_threads;
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
char server_proxy_ip[16];
int server_proxy_port;


void *relay_thread_loop(void *input);

void send404(int fd, char *filename){
    http_start_response(fd, 200);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    http_send_string(fd,
            "<center>"
            "<h1>Welcome to httpserver!</h1>"
            "<hr>"
            "<p>"
            );
    http_send_string(fd, filename);
    http_send_string(fd,
            " not found</p>"
            "</center>");
}

void serve_file(int fd, char *filename){
    int length;
    char buffer[1096];
    int fp;

    fp = open(filename, O_RDONLY);
    if(fp == -1){
        printf("Couldn't find file. Serving 404\n");
        send404(fd, filename);
    }
    else{
        http_start_response(fd, 200);
        http_send_header(fd, "Content-Type", http_get_mime_type(filename));
        http_send_header(fd, "Server", "httpserver/1.0");
        http_end_headers(fd);

        while((length = read(fp, buffer, 1096)) > 0){
            http_send_data(fd, buffer, length);
        }
    }
}

void serve_directory_listing(int fd, char *dirname){
    struct dirent *ent;
    DIR *dir = opendir(dirname);
    
    if(!dir){
        send404(fd, dirname);
        return;
    }

    http_start_response(fd, 200);
    http_send_header(fd, "Content-Type", "text/html");
    http_send_header(fd, "Server", "httpserver/1.0");
    http_end_headers(fd);

    http_send_string(fd,
            "<h1>Welcome to httpserver!</h1>"
            "<hr>"
            );

    while((ent = readdir(dir)) != NULL){
        http_send_string(fd, "<a href=\"");
        http_send_string(fd, ent->d_name);
        http_send_string(fd, "\">");

        http_send_string(fd, ent->d_name);
        http_send_string(fd, "</a>");
        http_send_string(fd, "<br/>\n");
    }

    closedir(dir);
}

/*
 * Reads an HTTP request from stream (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a list
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 */
void handle_files_request(int fd) {
    /*
     * TODO: Your solution for Task 1 goes here! Feel free to delete/modify *
     * any existing code.
     */
    struct stat file_info;

    struct http_request *request = http_request_parse(fd);
    char path[1024];

    if(request == NULL || request->path == NULL){
        // printf("Wierd Null pointer here\n");
        return;
    }

    // Favicon.ico
    if(strcmp(request->path, "/favicon.ico") == 0){
        send404(fd, request->path);
        return;
    }
    strcpy(path, server_files_directory);
    strcat(path, request->path);

    // printf("Handling Files request for %s\n", request->path);
    stat(path, &file_info);

    if(S_ISDIR(file_info.st_mode)){
        char index_name[1024];
        strcpy(index_name, path);
        strcat(index_name, "/index.html");

        if(access(index_name, F_OK) == 0){
            // printf("Serving index file %s\n", index_name);
            serve_file(fd, index_name);
        }
        else{
            // printf("Index not found. Serving directory listing %s\n", path);
            serve_directory_listing(fd, path);
        }
    }
    else if(S_ISREG(file_info.st_mode)){
        // printf("Serving file %s\n", path);
        serve_file(fd, path);
    }
    else{
        send404(fd, path);
    }
}


/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target. HTTP requests from the client (fd) should be sent to the
 * proxy target, and HTTP responses from the proxy target should be sent to
 * the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */
void handle_proxy_request(int fd) {

    /*
     * TODO: Your solution for Task 3 goes here! Feel free to delete/modify *
     * any existing code.
     */
    pthread_t forward_relay;
    pthread_t backward_relay;
    
    int forward_args[2];
    int backward_args[2];

    int proxy_socket = socket(PF_INET, SOCK_STREAM, 0);
    struct sockaddr_in proxy_addr;

    memset(&proxy_addr, 0, sizeof(proxy_addr));

    proxy_addr.sin_family = AF_INET;
    inet_pton(AF_INET, server_proxy_ip, &proxy_addr.sin_addr);
    proxy_addr.sin_port = htons(server_proxy_port);

    if(connect(proxy_socket, (struct sockaddr *)&proxy_addr, sizeof(proxy_addr)) < 0){
        printf("Connection failed! %s\n", strerror(errno));
    }

    forward_args[0] = fd;
    forward_args[1] = proxy_socket;

    backward_args[0] = proxy_socket;
    backward_args[1] = fd;

    pthread_create(&forward_relay, NULL, relay_thread_loop, (void *)forward_args);
    pthread_create(&backward_relay, NULL, relay_thread_loop, (void *)backward_args);

    pthread_join(forward_relay, NULL);
}

void *relay_thread_loop(void *input){
    int *fds = (int *)input;
    int in = fds[0];
    int out = fds[1];
    char buffer[1024];
    int length;

    while((length = read(in, buffer, sizeof(buffer) - 1)) > 0){
        printf("%d to %d\n", in, out);
        printf("%s\n\n\n", buffer);
        write(out, buffer, length);
    }
    return NULL;
}

void * thread_loop(void *f){
    void (*request_handler)(int) = f;
    int client_fd;
    printf("Thread created\n");

    while(1){
        client_fd = wq_pop(&work_queue);
        request_handler(client_fd);
        printf("I'm back %lu!\n", (unsigned long)pthread_self());
        close(client_fd);
        printf("closed fd %d\n", client_fd);
    }

    return NULL;
}


/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *socket_number, void (*request_handler)(int)) {

    struct sockaddr_in server_address, client_address;
    size_t client_address_length = sizeof(client_address);
    int client_socket_number;

    *socket_number = socket(PF_INET, SOCK_STREAM, 0);
    if (*socket_number == -1) {
        perror("Failed to create a new socket");
        exit(errno);
    }

    int socket_option = 1;
    if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
                sizeof(socket_option)) == -1) {
        perror("Failed to set socket options");
        exit(errno);
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(server_port);

    if (bind(*socket_number, (struct sockaddr *) &server_address,
                sizeof(server_address)) == -1) {
        perror("Failed to bind on socket");
        exit(errno);
    }

    if (listen(*socket_number, 1024) == -1) {
        perror("Failed to listen on socket");
        exit(errno);
    }

    printf("Listening on port %d...\n", server_port);

    while (1) {
        client_socket_number = accept(*socket_number,
                (struct sockaddr *) &client_address,
                (socklen_t *) &client_address_length);
        if (client_socket_number < 0) {
            perror("Error accepting socket");
            continue;
        }

        // printf("Accepted connection from %s on port %d\n", inet_ntoa(client_address.sin_addr), client_address.sin_port);

        // Instead of this, push into queue
        // request_handler(client_socket_number);
        // close(client_socket_number);
        if(0 < num_threads){
            wq_push(&work_queue, client_socket_number);
        }
        else{
            request_handler(client_socket_number);
            close(client_socket_number);
        }
    }

    shutdown(*socket_number, SHUT_RDWR);
    close(*socket_number);
}

int server_fd;
void signal_callback_handler(int signum) {
    printf("Caught signal %d: %s\n", signum, strsignal(signum));
    printf("Closing socket %d\n", server_fd);
    if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
    exit(0);
}

char *USAGE =
"Usage: ./httpserver --files www_directory/ --port 8000 [--num-threads 5]\n"
"       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000 [--num-threads 5]\n";

void exit_with_usage() {
    fprintf(stderr, "%s", USAGE);
    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
    signal(SIGINT, signal_callback_handler);

    /* Default settings */
    server_port = 8000;
    void (*request_handler)(int) = NULL;

    int i;

    // Initialize wq
    wq_init(&work_queue);

    for (i = 1; i < argc; i++) {
        if (strcmp("--files", argv[i]) == 0) {
            request_handler = handle_files_request;
            free(server_files_directory);
            server_files_directory = argv[++i];
            if (!server_files_directory) {
                fprintf(stderr, "Expected argument after --files\n");
                exit_with_usage();
            }
        } else if (strcmp("--proxy", argv[i]) == 0) {
            request_handler = handle_proxy_request;

            char *proxy_target = argv[++i];
            if (!proxy_target) {
                fprintf(stderr, "Expected argument after --proxy\n");
                exit_with_usage();
            }

            char *colon_pointer = strchr(proxy_target, ':');
            if (colon_pointer != NULL) {
                *colon_pointer = '\0';
                server_proxy_hostname = proxy_target;
                server_proxy_port = atoi(colon_pointer + 1);
            } else {
                server_proxy_hostname = proxy_target;
                server_proxy_port = 80;
            }
            inet_ntop(
                AF_INET,
                gethostbyname(server_proxy_hostname)->h_addr,
                server_proxy_ip,
                16
            );

        } else if (strcmp("--port", argv[i]) == 0) {
            char *server_port_string = argv[++i];
            if (!server_port_string) {
                fprintf(stderr, "Expected argument after --port\n");
                exit_with_usage();
            }
            server_port = atoi(server_port_string);
        } else if (strcmp("--num-threads", argv[i]) == 0) {
            char *num_threads_str = argv[++i];
            pthread_t thread;
            int i;

            if (!num_threads_str || (num_threads = atoi(num_threads_str)) < 1) {
                fprintf(stderr, "Expected positive integer after --num-threads\n");
                exit_with_usage();
            }

            // Initialize num threads
            for(i=1;i<=num_threads;i++){
                if(pthread_create(&thread, NULL, thread_loop, request_handler) != 0){
                    fprintf(stderr, "Couldn't initialize thread %d, Error", i);
                }
            }

        } else if (strcmp("--help", argv[i]) == 0) {
            exit_with_usage();
        } else {
            fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
            exit_with_usage();
        }
    }

    if (server_files_directory == NULL && server_proxy_hostname == NULL) {
        fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                "                      \"--proxy [HOSTNAME:PORT]\"\n");
        exit_with_usage();
    }

    serve_forever(&server_fd, request_handler);

    return EXIT_SUCCESS;
}
