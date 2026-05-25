#include <stdio.h>
#include <pthread.h>

int g_log_level = 3;
FILE* g_log_file = NULL;
pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;