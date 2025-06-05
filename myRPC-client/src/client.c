#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <pwd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "libmysyslog.h"

#define BUFFER_SIZE 1024

// Вывод справки
void print_help() {
    printf("Usage: myRPC-client [OPTIONS]\n");
    printf("Options:\n");
    printf("  -c, --command \"bash_command\"   Command to execute\n");
    printf("  -h, --host \"ip_address\"        Server IP address\n");
    printf("  -p, --port PORT                Server port\n");
    printf("  -s, --stream                   Use stream socket\n");
    printf("  -d, --dgram                    Use datagram socket\n");
    printf("      --help                     Show help and exit\n");
}

int main(int argc, char *argv[]) {
    char *command = NULL;
    char *server_ip = NULL;
    int port = 0;
    int use_stream = 1;
    int option;

    static struct option long_options[] = {
        {"command", required_argument, 0, 'c'},
        {"host", required_argument, 0, 'h'},
        {"port", required_argument, 0, 'p'},
        {"stream", no_argument, 0, 's'},
        {"dgram", no_argument, 0, 'd'},
        {"help", no_argument, 0,  0 },
        {0, 0, 0, 0}
    };

    int option_index = 0;
    while ((option = getopt_long(argc, argv, "c:h:p:sd", long_options, &option_index)) != -1) {
        switch (option) {
            case 'c':
                command = optarg;
                break;
            case 'h':
                server_ip = optarg;
                break;
            case 'p':
                port = atoi(optarg);
                break;
            case 's':
                use_stream = 1;
                break;
            case 'd':
                use_stream = 0;
                break;
            case 0:
                print_help();
                return 0;
            default:
                print_help();
                return 1;
        }
    }

    if (!command || !server_ip || !port) {
        fprintf(stderr, "Error: Missing required arguments\n");
        print_help();
        return 1;
    }

    struct passwd *pw = getpwuid(getuid());
    char *username = pw->pw_name;

    char request[BUFFER_SIZE];
    snprintf(request, BUFFER_SIZE, "%s: %s", username, command);

    mysyslog("Connecting to server...", INFO, 0, 0, "/var/log/myrpc.log");

    int client_socket;
    if (use_stream) {
        client_socket = socket(AF_INET, SOCK_STREAM, 0);
    } else {
        client_socket = socket(AF_INET, SOCK_DGRAM, 0);
    }

    if (client_socket < 0) {
        mysyslog("Socket creation failed", ERROR, 0, 0, "/var/log/myrpc.log");
        perror("Socket creation failed");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);

    if (use_stream) {
        if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            mysyslog("Connection failed", ERROR, 0, 0, "/var/log/myrpc.log");
            perror("Connection failed");
            close(client_socket);
            return 1;
        }

        mysyslog("Connected to server", INFO, 0, 0, "/var/log/myrpc.log");

        send(client_socket, request, strlen(request), 0);

        char response[BUFFER_SIZE];
        int bytes_received = recv(client_socket, response, BUFFER_SIZE, 0);
        response[bytes_received] = '\0';
        printf("Server response: %s\n", response);

        mysyslog("Received server response", INFO, 0, 0, "/var/log/myrpc.log");

    } else {
        sendto(client_socket, request, strlen(request), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

        char response[BUFFER_SIZE];
        socklen_t addr_len = sizeof(server_addr);
        int bytes_received = recvfrom(client_socket, response, BUFFER_SIZE, 0, (struct sockaddr*)&server_addr, &addr_len);
        response[bytes_received] = '\0';
        printf("Server response: %s\n", response);

        mysyslog("Received server response", INFO, 0, 0, "/var/log/myrpc.log");
    }

    close(client_socket);
    return 0;
}
