// AArch64 exception vector table: 16 entries, 0x80 bytes apart, the
// table itself 2KiB-aligned (VBAR_EL1 requires that alignment). Which
// entry runs depends on (exception class) x (what was running when it
// happened) -- 4 classes (Synchronous, IRQ, FIQ, SError) x 4 categories
// (current EL using SP_EL0, current EL using SP_ELx, lower EL in
// AArch64, lower EL in AArch32).
//
// This kernel is EL1-only (no EL0 code exists at all), has no interrupts
// yet, and -- less obviously -- runs its normal code with SPSel=0 (see
// exceptions.cpp's install_vectors(), which sets up SP_EL1 purely for
// exception handlers to use, precisely because of this): every thread
// runs on SP_EL0, so any same-EL exception is always categorized
// "Current EL, using SP_EL0", not "...SP_ELx". Exactly one entry is
// therefore ever expected to fire: Synchronous, Current EL, SP_EL0(!) --
// a thread's own code taking a Data Abort on an address its own VME list
// may or may not cover (see exceptions.cpp's handle_page_fault()). Every
// other entry exists only so a bug that lands on the wrong one fails
// loudly (KPANIC, with the vector number) instead of silently corrupting
// state or looping forever.
//
// Each slot is just a tiny branch out to a full-sized handler below the
// table, rather than the handler's own body -- the real one (for Sync/
// SP_EL0) needs far more than the 0x80 bytes (32 instructions) a slot
// actually has room for, and the vector table's own 0x80 spacing is
// fixed by the architecture, not something to work around by packing
// code tighter. Written out in full rather than with a .rept/.endr
// loop: GNU as's .rept has no accessible per-iteration counter (that
// needs .irp instead), and this kernel's assembly (context_switch.S)
// already prefers being fully explicit over being clever.

.section .text
.balign 2048
.global vector_table
vector_table:

// ---- Current EL, using SP_EL0 -- offset 0x000, index 0: this is the
// one vector this kernel actually expects to take. ----
b sync_el0_handler
.balign 0x80
mov x0, #1
bl exceptions_unhandled_vector
.balign 0x80
mov x0, #2
bl exceptions_unhandled_vector
.balign 0x80
mov x0, #3
bl exceptions_unhandled_vector
.balign 0x80

// ---- Current EL, using SP_ELx (never used -- see the file comment) ----
mov x0, #4
bl exceptions_unhandled_vector
.balign 0x80
mov x0, #5
bl exceptions_unhandled_vector
.balign 0x80
mov x0, #6
bl exceptions_unhandled_vector
.balign 0x80
mov x0, #7
bl exceptions_unhandled_vector
.balign 0x80

// ---- Lower EL, AArch64 (unreachable -- no EL0 exists in this kernel) ----
mov x0, #8
bl exceptions_unhandled_vector
.balign 0x80
mov x0, #9
bl exceptions_unhandled_vector
.balign 0x80
mov x0, #10
bl exceptions_unhandled_vector
.balign 0x80
mov x0, #11
bl exceptions_unhandled_vector
.balign 0x80

// ---- Lower EL, AArch32 (unreachable -- no EL0 exists, let alone AArch32) ----
mov x0, #12
bl exceptions_unhandled_vector
.balign 0x80
mov x0, #13
bl exceptions_unhandled_vector
.balign 0x80
mov x0, #14
bl exceptions_unhandled_vector
.balign 0x80
mov x0, #15
bl exceptions_unhandled_vector
.balign 0x80

.size vector_table, . - vector_table

// The real handler, out-of-line (see the file comment for why it can't
// live inline in the vector table's own 0x80-byte slot 0).
.type sync_el0_handler, %function
sync_el0_handler:
    // Save the entire interrupted GPR set (x0-x30, not just the AAPCS64
    // callee-saved subset -- the interrupted instruction was not a call,
    // so any of x0-x18 could be live and needs to survive the nested C++
    // call below) plus ELR_EL1 (where to resume) and SPSR_EL1 (the
    // interrupted processor state) -- 33 values, 264 bytes, rounded up
    // to a 16-byte-aligned 272 to keep SP valid for the C++ call. This
    // runs on SP_EL1 (install_vectors() set that up, since taking any
    // exception always switches to it, regardless of this being the
    // "...SP_EL0" category -- that name only describes what the
    // INTERRUPTED code was using), not the faulting thread's own stack,
    // so there's no risk of corrupting it. No FP/SIMD save anywhere in
    // this file: the whole kernel builds -mgeneral-regs-only -nofp
    // -nosimd, so nothing ever touches V0-V31.
    stp x0, x1,   [sp, #-272]!
    stp x2, x3,   [sp, #16]
    stp x4, x5,   [sp, #32]
    stp x6, x7,   [sp, #48]
    stp x8, x9,   [sp, #64]
    stp x10, x11, [sp, #80]
    stp x12, x13, [sp, #96]
    stp x14, x15, [sp, #112]
    stp x16, x17, [sp, #128]
    stp x18, x19, [sp, #144]
    stp x20, x21, [sp, #160]
    stp x22, x23, [sp, #176]
    stp x24, x25, [sp, #192]
    stp x26, x27, [sp, #208]
    stp x28, x29, [sp, #224]
    str x30,      [sp, #240]
    // x0/x1 are free to use as scratch now -- their real values are
    // already safe on the stack, saved by the very first stp above.
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1,   [sp, #248]

    bl handle_sync_exception       // C++ (exceptions.cpp); KPANICs if it can't fix things up

    ldp x0, x1,   [sp, #248]
    msr elr_el1, x0
    msr spsr_el1, x1
    ldr x30,      [sp, #240]
    ldp x28, x29, [sp, #224]
    ldp x26, x27, [sp, #208]
    ldp x24, x25, [sp, #192]
    ldp x22, x23, [sp, #176]
    ldp x20, x21, [sp, #160]
    ldp x18, x19, [sp, #144]
    ldp x16, x17, [sp, #128]
    ldp x14, x15, [sp, #112]
    ldp x12, x13, [sp, #96]
    ldp x10, x11, [sp, #80]
    ldp x8, x9,   [sp, #64]
    ldp x6, x7,   [sp, #48]
    ldp x4, x5,   [sp, #32]
    ldp x2, x3,   [sp, #16]
    ldp x0, x1,   [sp], #272
    eret                            // resumes at ELR_EL1 -- retries the faulting instruction
.size sync_el0_handler, . - sync_el0_handler
