#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "config_parser.h"

// Разбор конфигурационного файла
Config parse_config(const char *config_path) {
    Config config;
    config.port = 0;
    strcpy(config.socket_type, "stream");

    FILE *config_file = fopen(config_path, "r");
    if (!config_file) {
        perror("Failed to open config file");
        return config;
    }

    char line[256];
    while (fgets(line, sizeof(line), config_file)) {
        line[strcspn(line, "\n")] = '\0';
        // Пропуск комментариев и пустых строк
        if (line[0] == '#' || strlen(line) == 0)
            continue;

        char *key = strtok(line, "=");
        char *value = strtok(NULL, "");

        if (strcmp(key, "port") == 0) {
            config.port = atoi(value);
        } else if (strcmp(key, "socket_type") == 0) {
            strcpy(config.socket_type, value);
        }
    }

    fclose(config_file);
    return config;
}
