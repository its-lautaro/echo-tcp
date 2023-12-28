/*
* Servidor echo tcp IPv4 concurrente usando procesos
* 23/12/23
* Lautaro La Vecchia
*/
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>

// Receive message buffer
#define _MSG_BUFFER_ 256
// Default port for the server
#define _DEF_PORT_ 1234
// Default backlog for the server
#define _DEF_BACKLOG_ 10
// Upper limit on child process number
#define _MAX_CHILD_ 5

int server_fd, client_fd, child_count;
// Client info
struct sockaddr_in client_addr;

// On child termination decrement child count
void childTerminatedHandler(int signum) {
    int status;
    pid_t child_pid;

    // Reap terminated child processes using waitpid
    while ((child_pid = waitpid(-1, &status, WNOHANG)) > 0) {
        child_count--;
        printf("Remaining childs: %d.\n",child_count);
    }
}

// On program termination close server listening socket
void cleanup() {
    printf("\nCleaning up resources...\n");
    close(server_fd);
    exit(EXIT_SUCCESS);
}

// Accept incoming connection and store client information
void accept_connection() {
    socklen_t client_addr_len = sizeof(client_addr);
    if ((client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len)) < 0) {
        fprintf(stderr, "Error on connection accept.\n");
        close(client_fd);
        exit(EXIT_FAILURE);
    }
}

// Perform actions after accepting the connection and getting the client information
void handle_connection() {
    char client_ip[INET_ADDRSTRLEN];
    int client_port = ntohs(client_addr.sin_port);
    inet_ntop(client_addr.sin_family, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    printf("Connection from %s:%d handle by process %d\n", client_ip, client_port, getpid());
}

// Receive and echo messages until connection termination
void perform_echo() {
    char client_ip[INET_ADDRSTRLEN];
    int client_port = ntohs(client_addr.sin_port);
    inet_ntop(client_addr.sin_family, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    char msg[_MSG_BUFFER_] = { 0 };
    int cant;
    while ((cant = recv(client_fd, msg, sizeof(msg) - 1, 0)) > 0) {
        msg[cant] = '\0'; // Null-terminate the received data
        printf("%s[%d] says: %s", client_ip, client_port, msg); // Local echo
        send(client_fd, msg, cant, 0);
    }
}

// Terminate client connection
void terminate_connection() {
    char client_ip[INET_ADDRSTRLEN];
    int client_port = ntohs(client_addr.sin_port);
    inet_ntop(client_addr.sin_family, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    printf("Connection from %s:%d closed. Process %d terminating\n", client_ip, client_port, getpid());
    close(client_fd);
}

int main(int argc, char* argv[]) {
    int port = _DEF_PORT_;
    int backlog = _DEF_BACKLOG_;
    // Set up signal handler for cleanup on program termination
    signal(SIGINT, cleanup);
    // Set up signal handler for child termination
    signal(SIGCHLD, childTerminatedHandler);
    // Parse program options
    int c;
    opterr = 0;
    while ((c = getopt(argc, argv, "p:b:h")) != -1) {
        switch (c) {
        case 'h':
            printf("usage: echo [option]\n");
            printf("Options:\n");
            printf("-p  : set port to listen to, default is 1234\n");
            printf("-b  : define backlog, default is %d\n",_DEF_BACKLOG_);
            printf("-h  : help\n");
            return 0;
            break;
        case 'b':
            backlog = atoi(optarg);
            if (backlog <= 0) {
                fprintf(stderr, "Backlog should be set greater than 0.\n");
                exit(EXIT_FAILURE);
            }
            break;
        case 'p':
            port = atoi(optarg);
            if (port < 1024) {
                fprintf(stderr, "Warning: Can't use well-known ports without privileges. A random available port will be assigned.\n");
            }
            break;
        case '?':
            if (optopt == 'p' || optopt == 'b')
                fprintf(stderr, "Option -%c requires an argument. See -h for help.\n", optopt);
            else if (isprint(optopt))
                fprintf(stderr, "Unknown option `-%c'. See -h for help.\n", optopt);
            else
                fprintf(stderr, "Unknown option character `\\x%x'. See -h for help.\n", optopt);
            return 1;
        default:
            abort();
        }
    }
    // Create TCP Socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        fprintf(stderr, "Error on socket create.\n");
        exit(1);
    }
    // Set socket address and port
    struct sockaddr_in addr = {
        AF_INET,
        htons(port),
        {INADDR_ANY}
    };
    // Bind
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Error on socket bind.\n");
        exit(1);
    }
    // Listen for incoming connections
    if (listen(server_fd, backlog) < 0) {
        fprintf(stderr, "Error on socket listen.\n");
        exit(1);
    }
    printf("Echo server up. Listening on port %u.\n", port);
    child_count = 0;

    while (1) {
        accept_connection();
        // If child count exceeds max, drop connection
        if (child_count >= _MAX_CHILD_) {
            printf("Child limit reached, connection dropped.\n");
            close(client_fd);
            continue;
        }
        // Handle client
        switch (fork()) {
        case -1:
            fprintf(stderr, "Error on fork.\n");
            break;
        case 0: // Child process
            close(server_fd); // Unused
            handle_connection();
            perform_echo();
            terminate_connection();
            exit(EXIT_SUCCESS);
            break;
        default: // Main process
            child_count++;
            close(client_fd); // Unused
            break;
        }
    }

    return 0;
}
