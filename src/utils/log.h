#ifndef NOMALLOC_UTILS_LOG_H
#define NOMALLOC_UTILS_LOG_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <pthread.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOG_LEVEL_NONE  0
#define LOG_LEVEL_ERROR 1
#define LOG_LEVEL_WARN  2
#define LOG_LEVEL_INFO  3
#define LOG_LEVEL_DEBUG 4
#define LOG_LEVEL_TRACE 5

extern int g_log_level;
extern FILE* g_log_file;
extern pthread_mutex_t g_log_mutex;

static inline void log_init(int level, FILE* file) {
    g_log_level = level;
    g_log_file = file ? file : stderr;
    pthread_mutex_init(&g_log_mutex, NULL);
}

static inline void log_shutdown(void) {
    if (g_log_file && g_log_file != stderr && g_log_file != stdout) {
        fclose(g_log_file);
    }
    pthread_mutex_destroy(&g_log_mutex);
    g_log_file = NULL;
}

static inline int log_get_level(void) {
    return g_log_level;
}

static inline void log_set_level(int level) {
    g_log_level = level;
}

static inline void log_set_file(FILE* file) {
    g_log_file = file;
}

static inline void log_format_time(char* buffer, size_t size) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

static inline void log_write(int level, const char* file, int line, 
                              const char* func, const char* fmt, ...) {
    if (level > g_log_level || !g_log_file) {
        return;
    }
    
    pthread_mutex_lock(&g_log_mutex);
    
    char time_buf[32];
    log_format_time(time_buf, sizeof(time_buf));
    
    const char* level_names[] = {"NONE", "ERROR", "WARN", "INFO", "DEBUG", "TRACE"};
    const char* level_name = level_names[level];
    
    const char* short_file = strrchr(file, '/');
    if (short_file) {
        short_file++;
    } else {
        short_file = file;
    }
    
    fprintf(g_log_file, "[%s] [%s] [%lu] [%s:%d:%s] ",
            time_buf, level_name, (unsigned long)pthread_self(), 
            short_file, line, func);
    
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log_file, fmt, args);
    va_end(args);
    
    fprintf(g_log_file, "\n");
    fflush(g_log_file);
    
    pthread_mutex_unlock(&g_log_mutex);
}

#define log_error(...) log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_warn(...)  log_write(LOG_LEVEL_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_info(...)  log_write(LOG_LEVEL_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_debug(...) log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_trace(...) log_write(LOG_LEVEL_TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__)

#define log_enter() log_trace("ENTER")
#define log_exit()  log_trace("EXIT")

#ifdef __cplusplus
}
#endif

#endif