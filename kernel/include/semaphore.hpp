#pragma once

#include <cstdint>

#include "spinlock.hpp"
#include "spin_queue.hpp"

// A classic counting semaphore: `counter` can go negative, with its
// magnitude equal to the number of threads currently blocked in down().
struct Semaphore {
    explicit Semaphore(int64_t cnt);

    // Asserts no thread is blocked in down() -- if one were, destroying
    // the semaphore out from under it would leave that thread's TCB
    // orphaned in `q` and its thread stuck forever.
    ~Semaphore();

    void up();
    void down();

    SpinQueue q;
    Spinlock lock_;
    int64_t counter;
};