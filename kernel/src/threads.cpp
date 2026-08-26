#include "threads.hpp"

#include <cstddef>
#include <cstdint>

#include "context_switch.hpp"
#include "percore.hpp"
#include "smp.hpp"

namespace threads {

namespace {

// Size of the stack allocated for each new thread. Arbitrary but generous
// for a kernel that isn't doing much yet -- bump it if a thread overflows.
constexpr size_t kStackSize = 64 * 1024;

} // namespace

void stop() {
    percore::PerCore &pc = percore::g_table[smp::me()];

    // Hand the now-stopping thread's TCB off to idle_loop() to free once
    // it's safely off of it (i.e. once idle_loop() itself is the one
    // running, on its own stack, after the switch below).
    pc.to_delete = pc.curr;
    pc.prev = pc.curr;
    pc.curr = pc.idle;
    switch_to(pc.prev, pc.curr);

    __builtin_unreachable(); // switch_to() into idle never returns here
}

TCB *make_tcb(void (*entry)()) {
    auto *stack = new uint8_t[kStackSize];

    // Build, at the top of the new stack, the frame context_switch()
    // itself would have saved there (see SavedFrame / context_switch.S)
    // -- sized via sizeof(SavedFrame) rather than a hardcoded offset, so
    // this keeps working if that layout ever changes. Every saved
    // register but x30 goes unused by a thread that has never run, so
    // zeroing them is enough; x30 -- where context_switch()'s final `ret`
    // jumps -- is `entry`, so the first switch onto this stack pops this
    // fake frame back off and jumps straight into it.
    uint8_t *stack_top = stack + kStackSize;
    auto *frame = reinterpret_cast<SavedFrame *>(stack_top - sizeof(SavedFrame));
    *frame = {};
    frame->x30 = reinterpret_cast<void *>(entry);

    return new TCB(frame, stack);
}

// Reached via thread_trampoline (context_switch.S) with `func` -- the
// function go() was given -- as a genuine AAPCS64 argument, not by a
// direct jump to it as x30. Running `func` here rather than jumping
// straight to it is what lets go() stop() the thread automatically
// instead of requiring `func` to do it itself.
extern "C" [[noreturn]] void thread_entry(void (*func)()) {
    func();
    stop();
}

TCB *make_thread_tcb(void (*func)()) {
    TCB *tcb = make_tcb(thread_trampoline);

    // thread_trampoline expects the real entry function in x19 (see
    // context_switch.S). tcb->sp is exactly the frame make_tcb() just
    // built -- the same slot context_switch() itself would restore x19
    // from, were this TCB switched into normally instead of for the
    // first time.
    static_cast<SavedFrame *>(tcb->sp)->x19 = reinterpret_cast<void *>(func);
    return tcb;
}

} // namespace threads
