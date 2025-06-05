#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "libmysyslog.h"

// Реализация функции логирования
int mysyslog(const char* message, int level, int driver, int format, const char* log_path) {
    FILE *log_file;
    log_file = fopen(log_path, "a");
    if (log_file == NULL) {
        return -1;
    }

    time_t current_time;
    time(&current_time);
    char *timestamp = ctime(&current_time);
    timestamp[strlen(timestamp) - 1] = '\0';

    const char *level_str;
    switch (level) {
        case DEBUG: level_str = "DEBUG"; break;
        case INFO: level_str = "INFO"; break;
        case WARN: level_str = "WARN"; break;
        case ERROR: level_str = "ERROR"; break;
        case CRITICAL: level_str = "CRITICAL"; break;
        default: level_str = "UNKNOWN"; break;
    }

    if (format == 0) {
        fprintf(log_file, "%s [%s] %s\n", timestamp, level_str, message);
    }

    fclose(log_file);
    return 0;
}
