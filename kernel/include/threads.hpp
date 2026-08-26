#pragma once

#include "scheduler.hpp"
#include "tcb.hpp"

namespace threads {
    // Stops the currently running thread and switches to this core's idle
    // thread, which will free the stopped thread's TCB (stack included)
    // once it's safely off of it. Never returns.
    [[noreturn]] void stop();

    // Builds a TCB with a freshly heap-allocated stack, set up so that the
    // first context_switch() into it pops a fake initial frame back off
    // and jumps straight to `entry` (see SavedFrame in context_switch.hpp
    // for how) -- no wrapping, and no automatic stop() if/when `entry`
    // returns. The TCB isn't queued anywhere. Used directly by
    // percore::init() (percore.hpp) to build each core's idle thread,
    // which manages its own lifecycle by hand and is never on the ready
    // queue; most other callers want go() (or make_thread_tcb()) instead.
    TCB *make_tcb(void (*entry)());

    // Like make_tcb(), but the resulting TCB runs `func` and then calls
    // stop() automatically when `func` returns, via a small thread_entry()
    // wrapper (threads.cpp) -- unlike a raw make_tcb() thread, `func`
    // itself doesn't need to call stop(). Used by go() below.
    TCB *make_thread_tcb(void (*func)());

    // Starts a new thread running `func` and hands it to the scheduler.
    // `func` must be convertible to `void (*)()` -- a plain function or a
    // captureless lambda, since nothing here arranges to pass captured
    // state into it -- and, unlike a raw make_tcb() thread, does not need
    // to call stop() itself: returning from `func` does that
    // automatically (see make_thread_tcb()/thread_entry()).
    //
    // go() only sets up the thread's initial stack and queues its TCB;
    // `func` doesn't run here, only later, whenever the scheduler picks
    // this thread and context_switch()es into it for the first time.
    template <typename F>
    void go(F func) {
        void (*entry)() = func;
        scheduler::schedule(make_thread_tcb(entry));
    }

    void yield();
} // namespace threads