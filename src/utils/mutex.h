#ifndef NOMALLOC_UTILS_MUTEX_H
#define NOMALLOC_UTILS_MUTEX_H

#include <pthread.h>
#include <stdbool.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef pthread_mutex_t mutex_t;
typedef pthread_cond_t cond_t;
typedef pthread_rwlock_t rwlock_t;

#define MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define COND_INITIALIZER PTHREAD_COND_INITIALIZER
#define RWLOCK_INITIALIZER PTHREAD_RWLOCK_INITIALIZER

static inline int mutex_init(mutex_t* mutex) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ADAPTIVE_NP);
    int ret = pthread_mutex_init(mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    return ret;
}

static inline void mutex_destroy(mutex_t* mutex) {
    pthread_mutex_destroy(mutex);
}

static inline int mutex_lock(mutex_t* mutex) {
    return pthread_mutex_lock(mutex);
}

static inline int mutex_trylock(mutex_t* mutex) {
    return pthread_mutex_trylock(mutex);
}

static inline int mutex_unlock(mutex_t* mutex) {
    return pthread_mutex_unlock(mutex);
}

static inline int cond_init(cond_t* cond) {
    return pthread_cond_init(cond, NULL);
}

static inline void cond_destroy(cond_t* cond) {
    pthread_cond_destroy(cond);
}

static inline int cond_wait(cond_t* cond, mutex_t* mutex) {
    return pthread_cond_wait(cond, mutex);
}

static inline int cond_timedwait(cond_t* cond, mutex_t* mutex, 
                                  const struct timespec* abstime) {
    return pthread_cond_timedwait(cond, mutex, abstime);
}

static inline int cond_signal(cond_t* cond) {
    return pthread_cond_signal(cond);
}

static inline int cond_broadcast(cond_t* cond) {
    return pthread_cond_broadcast(cond);
}

static inline int rwlock_init(rwlock_t* rwlock) {
    return pthread_rwlock_init(rwlock, NULL);
}

static inline void rwlock_destroy(rwlock_t* rwlock) {
    pthread_rwlock_destroy(rwlock);
}

static inline int rwlock_rdlock(rwlock_t* rwlock) {
    return pthread_rwlock_rdlock(rwlock);
}

static inline int rwlock_tryrdlock(rwlock_t* rwlock) {
    return pthread_rwlock_tryrdlock(rwlock);
}

static inline int rwlock_wrlock(rwlock_t* rwlock) {
    return pthread_rwlock_wrlock(rwlock);
}

static inline int rwlock_trywrlock(rwlock_t* rwlock) {
    return pthread_rwlock_trywrlock(rwlock);
}

static inline int rwlock_unlock(rwlock_t* rwlock) {
    return pthread_rwlock_unlock(rwlock);
}

#ifdef __cplusplus
}
#endif

#endif