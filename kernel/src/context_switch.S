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
