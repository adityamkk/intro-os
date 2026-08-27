#include "semaphore.hpp"

#include "panic.hpp"
#include "percore.hpp"
#include "scheduler.hpp"
#include "smp.hpp"
#include "tcb.hpp"

Semaphore::Semaphore(int64_t cnt) : counter(cnt) {}

Semaphore::~Semaphore() { ASSERT(q.empty()); }

void Semaphore::up() {
    lock_.lock();
    if (counter++ < 0) {
        // Unlock someone
        TCB *unblocked_tcb = nullptr;
        bool got_value = q.pop(reinterpret_cast<void **>(&unblocked_tcb));
        ASSERT(got_value); // Must have received value
        scheduler::schedule(unblocked_tcb);
    }
    lock_.unlock();
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