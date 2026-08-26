#pragma once

// A simple test-and-set spinlock for mutual exclusion across cores.
class Spinlock {
private:
    volatile int g_lock = 0;

public:
    Spinlock() = default;

    // Copying would let two Spinlocks diverge on what should be one shared
    // lock state, defeating mutual exclusion -- disallow it.
    Spinlock(const Spinlock &) = delete;
    Spinlock &operator=(const Spinlock &) = delete;

    void lock() {
        while (__atomic_test_and_set(&g_lock, __ATOMIC_ACQUIRE)) {
            asm volatile("yield" ::: "memory");
        }
    }

    void unlock() { __atomic_clear(&g_lock, __ATOMIC_RELEASE); }
};