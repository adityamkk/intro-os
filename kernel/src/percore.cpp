#include "percore.hpp"

#include "panic.hpp"
#include "scheduler.hpp"
#include "smp.hpp"
#include "threads.hpp"

namespace {

// Runs forever on its core: ask the scheduler for the next runnable
// thread and switch to it whenever there is one, otherwise just keep
// asking. This is what every core falls back to running whenever the
// ready queue is empty.
void idle_loop() {
    percore::PerCore &pc = percore::g_table[smp::me()];
    pc.curr = pc.idle;

    for (;;) {
        TCB *next = scheduler::next();
        if (next != nullptr) {
            pc.prev = pc.curr;
            pc.curr = next;
            switch_to(pc.prev, pc.curr);
            if (pc.to_delete != nullptr) {
                delete pc.to_delete;
                pc.to_delete = nullptr;
            }
            if (pc.to_queue != nullptr) {
                // Must have provided a queue and a spinlock
                ASSERT(pc.to_add_to != nullptr);
                ASSERT(pc.to_unlock != nullptr);
                pc.to_add_to->push(pc.to_queue);
                pc.to_unlock->unlock();
                pc.to_queue = nullptr;
                pc.to_add_to = nullptr;
                pc.to_unlock = nullptr;
            }
            if (pc.to_reschedule != nullptr) {
                scheduler::schedule(pc.to_reschedule);
                pc.to_reschedule = nullptr;
            }
        }
    }
}

} // namespace

namespace percore {

PerCore g_table[kMaxCores];

void init() {
    for (uint32_t i = 0; i < kMaxCores; i++) {
        g_table[i].idle = threads::make_tcb(idle_loop);
    }
}

} // namespace percore
