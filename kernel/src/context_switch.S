// void context_switch(void **prev_sp, void **next_sp)
// x0 = prev_sp, x1 = next_sp
//
// AAPCS64 callee-saved state is x19-x28, x29 (FP) and x30 (LR); the kernel
// is built with -mgeneral-regs-only -march=armv8-a+nofp+nosimd, so there
// are no FP/SIMD callee-saved registers (d8-d15) to save. SP itself needs
// no separate save slot: we only ever move it between the two frames this
// function lays out, writing/reading it through the caller-supplied
// pointers (`[x0]` / `[x1]` below).

.section .text
.global context_switch
.type context_switch, %function

context_switch:
    // Push the outgoing thread's callee-saved registers.
    // 12 registers * 8 bytes = 96 bytes, already 16-byte aligned.
    stp x19, x20, [sp, #-96]!
    stp x21, x22, [sp, #16]
    stp x23, x24, [sp, #32]
    stp x25, x26, [sp, #48]
    stp x27, x28, [sp, #64]
    stp x29, x30, [sp, #80]

    // Write the outgoing stack pointer through prev_sp, then switch to
    // whatever next_sp points at.
    mov x2, sp
    str x2, [x0]
    ldr x2, [x1]
    mov sp, x2

    // Pop the incoming thread's callee-saved registers off its stack.
    ldp x29, x30, [sp, #80]
    ldp x27, x28, [sp, #64]
    ldp x25, x26, [sp, #48]
    ldp x23, x24, [sp, #32]
    ldp x21, x22, [sp, #16]
    ldp x19, x20, [sp], #96

    // The restored x30 (LR) is wherever the incoming thread left off --
    // this ret is the "switch pcs" step.
    ret

.size context_switch, . - context_switch

// void thread_trampoline()
//
// context_switch()'s `ret` above is not a `bl`: it doesn't put anything
// meaningful in x0, so a thread that has never run can't receive a real
// AAPCS64 argument straight from context_switch() the way a normally
// -bl'd function would. threads::make_thread_tcb() (threads.cpp) works
// around this by pointing a fresh thread's frame at this trampoline
// instead of straight at threads::thread_entry(), with the real entry
// function stashed in x19 -- a register context_switch() restores like
// any other. All this does is move that value into x0, where
// threads::thread_entry() actually expects to find its argument, then
// branch (not link -- nothing will ever return here) into it.
.global thread_trampoline
.type thread_trampoline, %function

thread_trampoline:
    mov x0, x19
    b thread_entry

.size thread_trampoline, . - thread_trampoline
