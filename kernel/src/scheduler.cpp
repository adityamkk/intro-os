#include "scheduler.hpp"
#include "spin_queue.hpp"

namespace {
    
SpinQueue g_ready_queue;

} // namespace

namespace scheduler {
    void schedule(TCB* t) {
        g_ready_queue.push(t);
    }

    TCB* next() {
        TCB* t = nullptr;
        if (!g_ready_queue.pop(reinterpret_cast<void**>(&t))) {
            return nullptr;
        }
        return t;
    }
} // namespace scheduler