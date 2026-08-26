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

    // The exact pointer `new uint8_t[...]` returned for this thread's
    // stack, i.e. its base -- not the same as `sp`, which moves around
    // inside that allocation as the thread runs. Kept only so the
    // destructor can free the right pointer; nullptr if this TCB isn't
    // backed by a heap-allocated stack.
    void *stack_base;

    // `initial_sp` must point at a frame laid out the way context_switch()
    // itself lays one out (see SavedFrame in context_switch.hpp) -- this
    // TCB must be ready to context_switch() into as soon as it exists.
    TCB(void *initial_sp, void *stack_base);

    // Frees the thread's stack (via `stack_base`, not `sp` -- see above).
    ~TCB();
};

// Switches from `prev` to `next`, exactly like context_switch() (see
// context_switch.hpp), except `prev` may be nullptr: when there's no real
// predecessor to resume -- e.g. the very first switch made on a core --
// the outgoing stack pointer is written to a throwaway scratch buffer
// instead of dereferencing a null TCB. Nothing ever reads that buffer
// back, since there's no TCB for anyone to switch back into.
void switch_to(TCB *prev, TCB *next);