#pragma once

#include <cstdint>

// Thread control block: per-thread state.
struct TCB {
    // Saved stack pointer, valid whenever this thread is not the one
    // currently running. Pass `&tcb->sp` to context_switch() (see
    // context_switch.hpp) to save or restore this thread's context.
    void *sp;

    // Thread ID assigned to this TCB, unique per queued thread -- taken
    // from a shared counter at construction time, so it's safe to
    // construct TCBs concurrently from multiple cores.
    uint64_t tid;

    // `initial_sp` must point at a frame laid out the way context_switch()
    // itself lays one out (see SavedFrame in context_switch.hpp) -- this
    // TCB must be ready to context_switch() into as soon as it exists.
    explicit TCB(void *initial_sp);
};