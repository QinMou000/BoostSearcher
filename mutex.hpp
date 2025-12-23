/*
 * @Author: wang-qin928 2830862261@qq.com
 * @Date: 2025-12-23 11:25:03
 * @LastEditors: wang-qin928 2830862261@qq.com
 * @LastEditTime: 2025-12-23 11:27:04
 * @FilePath: \boost-searcher\mutex.hpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
#pragma once
#include <iostream>
#include <unistd.h>
#include <pthread.h>
#include <mutex>

class Mutex {
  public:
    Mutex() {
        pthread_mutex_init(&_mutex, nullptr);
    }
    void Lock() {
        pthread_mutex_lock(&_mutex);
    }
    void UnLock() {
        pthread_mutex_unlock(&_mutex);
    }
    pthread_mutex_t *Get() {
        return &_mutex;
    }
    ~Mutex() {
        pthread_mutex_destroy(&_mutex);
    }

  private:
    pthread_mutex_t _mutex;
};

class global_mutex {
  public:
    global_mutex(Mutex &mutex)
        : _mutex(mutex) {
        _mutex.Lock();
    }
    ~global_mutex() {
        _mutex.UnLock();
    }
  private:
    Mutex &_mutex;
};