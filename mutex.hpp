#pragma once
#include <pthread.h>
#include <mutex>

class Mutex {
  public:
    Mutex() { pthread_mutex_init(&_mutex, nullptr); }
    ~Mutex() { pthread_mutex_destroy(&_mutex); }

    void Lock() { pthread_mutex_lock(&_mutex); }
    void UnLock() { pthread_mutex_unlock(&_mutex); }
    pthread_mutex_t *Get() { return &_mutex; }

  private:
    pthread_mutex_t _mutex;
};

// RAII 风格的锁守卫
class MutexGuard {
  public:
    explicit MutexGuard(Mutex &mutex) : _mutex(mutex) { _mutex.Lock(); }
    ~MutexGuard() { _mutex.UnLock(); }

  private:
    Mutex &_mutex;
};
