#pragma once

// Prints `msg` (plus the call site) to the console and halts this core
// forever. Never returns. KPANIC() below is the usual way to call this --
// e.g. from a switch's default case that should be unreachable.
[[noreturn]] void kpanic_at(const char *msg, const char *file, int line);

#define KPANIC(msg) kpanic_at((msg), __FILE__, __LINE__)

// Halts with a description of the failed condition if `cond` is false;
// otherwise a no-op. Evaluates `cond` exactly once. Not compiled out in
// any build configuration -- there's only one build of this kernel.
#define ASSERT(cond)                                                         \
    do {                                                                     \
        if (!(cond)) {                                                       \
            KPANIC("ASSERT failed: " #cond);                                 \
        }                                                                    \
    } while (0)
