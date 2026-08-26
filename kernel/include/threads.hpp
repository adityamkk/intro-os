#pragma once

#include <cstddef>
#include <cstdint>

#include "context_switch.hpp"
#include "scheduler.hpp"
#include "tcb.hpp"

namespace threads {
    void stop();

    // Size of the stack allocated for each new thread. Arbitrary but
    // generous for a kernel that isn't doing much yet -- bump it if a
    // thread overflows.
    constexpr size_t kStackSize = 64 * 1024;

    // Starts a new thread running `func` and hands it to the scheduler.
    // `func` must be convertible to `void (*)()` -- a plain function or a
    // captureless lambda -- since nothing here arranges to pass state
    // into it (the "return" into a never-run thread, below, has no
    // notion of call arguments).
    //
    // go() only sets up the thread's initial stack and queues its TCB;
    // `func` doesn't run here, only later, whenever the scheduler picks
    // this thread and context_switch()es into it for the first time.
    template <typename F>
    void go(F func) {
        void (*entry)() = func;

        auto *stack = new uint8_t[kStackSize];

        // Build, at the top of the new stack, the frame context_switch()
        // itself would have saved there (see SavedFrame / context_switch.S)
        // -- sized via sizeof(SavedFrame) rather than a hardcoded offset,
        // so this keeps working if that layout ever changes. Every saved
        // register but x30 goes unused by a thread that has never run, so
        // zeroing them is enough; x30 -- where context_switch()'s final
        // `ret` jumps -- is `entry`, so the first switch onto this stack
        // pops this fake frame back off and jumps straight into `func`.
        uint8_t *stack_top = stack + kStackSize;
        auto *frame = reinterpret_cast<SavedFrame *>(stack_top - sizeof(SavedFrame));
        *frame = {};
        frame->x30 = reinterpret_cast<void *>(entry);

        TCB *tcb = new TCB(frame);
        scheduler::schedule(tcb);
    }

    void yield();
} // namespace threads