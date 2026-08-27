#include "semaphore.hpp"

#include "panic.hpp"
#include "percore.hpp"
#include "scheduler.hpp"
#include "smp.hpp"
#include "tcb.hpp"

Semaphore::Semaphore(int64_t cnt) : counter(cnt) {}

Semaphore::~Semaphore() { ASSERT(q.empty()); }

void Semaphore::up() {
    // TODO: Implement Semaphore.up
}

void Semaphore::down() {
    lock_.lock();
    if (--counter < 0) {
        // Need to block
        percore::PerCore &pc = percore::g_table[smp::me()];

        // Politely ask the idle routine to add yourself to the queue
        pc.to_queue = pc.curr;
        pc.to_add_to = &q;
        pc.to_unlock = &lock_; // After switching, the lock will be unlocked

        pc.prev = pc.curr;
        pc.curr = pc.idle;
        switch_to(pc.prev, pc.curr);
    } else {
        // Unlock and leave peacefully
        lock_.unlock();
    }
}