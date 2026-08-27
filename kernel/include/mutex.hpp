#pragma once

#include "semaphore.hpp"

struct Mutex {
    explicit Mutex();
    Semaphore sem;
    void lock();
    void unlock();
};