#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pwd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "config_parser.h"
#include "libmysyslog.h"

#define BUFFER_SIZE 1024

volatile sig_atomic_t stop_flag;

// Обработчик сигналов для корректного завершения
void handle_signal(int signal) {
    stop_flag = 1;
}

void run_daemon() {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    setsid();
    umask(0);

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
}

// Проверка разрешенных пользователей
int is_user_allowed(const char *username) {
    FILE *config_file = fopen("/etc/myRPC/users.conf", "r");
    if (!config_file) {
        mysyslog("Failed to open users.conf", ERROR, 0, 0, "/var/log/myrpc.log");
        perror("Failed to open users.conf");
        return 0;
    }

    char config_line[256];
    int is_allowed = 0;

    while (fgets(config_line, sizeof(config_line), config_file)) {
        config_line[strcspn(config_line, "\n")] = '\0';

        // Пропуск комментариев и пустых строк
        if (config_line[0] == '#' || strlen(config_line) == 0)
            continue;

        if (strcmp(config_line, username) == 0) {
            is_allowed = 1;
            break;
        }
    }

    fclose(config_file);
    return is_allowed;
}

// Выполнение команды с перенаправлением вывода
void execute_command(const char *command, char *stdout_path, char *stderr_path) {
    char full_command[BUFFER_SIZE];
    snprintf(full_command, BUFFER_SIZE, "%s >%s 2>%s", command, stdout_path, stderr_path);
    system(full_command);
}

int main() {
    run_daemon();
    
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    Config server_config = parse_config("/etc/myRPC/myRPC.conf");

    int port = server_config.port;
    int use_stream = strcmp(server_config.socket_type, "stream") == 0;

    mysyslog("Server starting...", INFO, 0, 0, "/var/log/myrpc.log");

    int server_socket;
    if (use_stream) {
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
    } else {
        server_socket = socket(AF_INET, SOCK_DGRAM, 0);
    }

    if (server_socket < 0) {
        mysyslog("Socket creation failed", ERROR, 0, 0, "/var/log/myrpc.log");
        perror("Socket creation failed");
        return 1;
    }

    int socket_opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &socket_opt, sizeof(socket_opt)) < 0) {
        mysyslog("setsockopt failed", ERROR, 0, 0, "/var/log/myrpc.log");
        perror("setsockopt failed");
        close(server_socket);
        return 1;
    }

    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        mysyslog("Bind failed", ERROR, 0, 0, "/var/log/myrpc.log");
        perror("Bind failed");
        close(server_socket);
        return 1;
    }

    if (use_stream) {
        listen(server_socket, 5);
        mysyslog("Server listening (stream mode)", INFO, 0, 0, "/var/log/myrpc.log");
    } else {
        mysyslog("Server listening (datagram mode)", INFO, 0, 0, "/var/log/myrpc.log");
    }

    while (!stop_flag) {
        char buffer[BUFFER_SIZE];
        int bytes_received;

        if (use_stream) {
            addr_len = sizeof(client_addr);
            int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
            if (client_socket < 0) {
                mysyslog("Accept failed", ERROR, 0, 0, "/var/log/myrpc.log");
                perror("Accept failed");
                continue;
            }

            bytes_received = recv(client_socket, buffer, BUFFER_SIZE, 0);
            if (bytes_received <= 0) {
                close(client_socket);
                continue;
            }
            buffer[bytes_received] = '\0';

            mysyslog("Request received", INFO, 0, 0, "/var/log/myrpc.log");

            char *username = strtok(buffer, ":");
            char *command = strtok(NULL, "");
            if (command) {
                while (*command == ' ')
                    command++;
            }

            char response[BUFFER_SIZE];

            if (is_user_allowed(username)) {
                mysyslog("User authorized", INFO, 0, 0, "/var/log/myrpc.log");

                char stdout_file[] = "/tmp/myRPC_XXXXXX.stdout";
                char stderr_file[] = "/tmp/myRPC_XXXXXX.stderr";
                mkstemp(stdout_file);
                mkstemp(stderr_file);

                execute_command(command, stdout_file, stderr_file);

                FILE *output_file = fopen(stdout_file, "r");
                if (output_file) {
                    size_t bytes_read = fread(response, 1, BUFFER_SIZE, output_file);
                    response[bytes_read] = '\0';
                    fclose(output_file);
                    mysyslog("Command executed successfully", INFO, 0, 0, "/var/log/myrpc.log");
                } else {
                    strcpy(response, "Error reading stdout file");
                    mysyslog("Error reading stdout file", ERROR, 0, 0, "/var/log/myrpc.log");
                }

                remove(stdout_file);
                remove(stderr_file);

            } else {
                snprintf(response, BUFFER_SIZE, "1: User '%s' not authorized", username);
                mysyslog("User not authorized", WARN, 0, 0, "/var/log/myrpc.log");
            }

            send(client_socket, response, strlen(response), 0);
            close(client_socket);

        } else {
            addr_len = sizeof(client_addr);
            bytes_received = recvfrom(server_socket, buffer, BUFFER_SIZE, 0, (struct sockaddr*)&client_addr, &addr_len);
            if (bytes_received <= 0) {
                continue;
            }
            buffer[bytes_received] = '\0';

            mysyslog("Request received", INFO, 0, 0, "/var/log/myrpc.log");

            char *username = strtok(buffer, ":");
            char *command = strtok(NULL, "");
            if (command) {
                while (*command == ' ')
                    command++;
            }

            char response[BUFFER_SIZE];

            if (is_user_allowed(username)) {
                mysyslog("User authorized", INFO, 0, 0, "/var/log/myrpc.log");

                char stdout_file[] = "/tmp/myRPC_XXXXXX.stdout";
                char stderr_file[] = "/tmp/myRPC_XXXXXX.stderr";
                mkstemp(stdout_file);
                mkstemp(stderr_file);

                execute_command(command, stdout_file, stderr_file);

                FILE *output_file = fopen(stdout_file, "r");
                if (output_file) {
                    size_t bytes_read = fread(response, 1, BUFFER_SIZE, output_file);
                    response[bytes_read] = '\0';
                    fclose(output_file);
                    mysyslog("Command executed successfully", INFO, 0, 0, "/var/log/myrpc.log");
                } else {
                    strcpy(response, "Error reading stdout file");
                    mysyslog("Error reading stdout file", ERROR, 0, 0, "/var/log/myrpc.log");
                }

                remove(stdout_file);
                remove(stderr_file);

            } else {
                snprintf(response, BUFFER_SIZE, "1: User '%s' not authorized", username);
                mysyslog("User not authorized", WARN, 0, 0, "/var/log/myrpc.log");
            }

            sendto(server_socket, response, strlen(response), 0, (struct sockaddr*)&client_addr, addr_len);
        }
    }

    close(server_socket);
    mysyslog("Server stopped", INFO, 0, 0, "/var/log/myrpc.log");
    return 0;
}
