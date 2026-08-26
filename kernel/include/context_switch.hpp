#pragma once

// Switches the CPU from running the thread owning `*prev_sp` to running the
// thread owning `*next_sp`: saves the caller's callee-saved registers on its
// own stack and writes the resulting stack pointer through `prev_sp`, then
// reads `*next_sp` and restores the callee-saved registers waiting there.
//
// "Switching pcs" happens implicitly: the function's final `ret` jumps to
// whatever return address is sitting in the incoming frame -- wherever that
// thread was executing when it last called context_switch() itself, or, for
// a thread that has never run, wherever its initial stack was built to
// "return" to.
//
// `prev_sp`/`next_sp` are typically `&tcb->sp` for each thread's TCB, but
// this function doesn't know anything about TCB -- it only reads and writes
// through the two pointers it's given. `*next_sp` must point at a frame
// this function itself previously saved (via an earlier context_switch()
// call), or one built to look like one.
//
// Implemented in kernel/src/context_switch.S.
extern "C" void context_switch(void **prev_sp, void **next_sp);

// Bridges context_switch()'s `ret`-based control transfer into a normal,
// argument-passing AAPCS64 call: entered exactly like any other never-run
// thread (pointed to by a SavedFrame's `x30`), it moves the value in
// `x19` into `x0` and branches into threads::thread_entry() -- giving
// that function a real argument despite having been "returned into"
// rather than called. See context_switch.S and
// threads::make_thread_tcb() (kernel/src/threads.cpp).
extern "C" void thread_trampoline();

// The exact register frame context_switch() saves to and restores from a
// thread's stack -- field order matches the stp/ldp offsets in
// context_switch.S exactly (x19 first at the lowest address, x30 last at
// the highest), and the two must be kept in sync. Useful for building a
// stack for a thread that has never run: write one of these at the top of
// its stack, and the first context_switch() into it will pop the frame
// back off as if it had called context_switch() itself, jumping to
// whatever's in `x30` via the final `ret`.
struct SavedFrame {
    void *x19;
    void *x20;
    void *x21;
    void *x22;
    void *x23;
    void *x24;
    void *x25;
    void *x26;
    void *x27;
    void *x28;
    void *x29; // frame pointer
    void *x30; // link register -- where context_switch()'s final `ret` jumps
};
