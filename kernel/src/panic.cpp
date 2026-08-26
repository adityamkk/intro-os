#include "panic.hpp"

#include <cstdint>

#include "console.hpp"

// Multiple cores can panic at once, and each of the print() calls below is
// only individually atomic (see console.hpp) -- so two simultaneous panics
// can interleave their output. That's a cosmetic problem on an already-fatal
// path, not a correctness one, so it isn't worth building a single-print
// buffered message (as main.cpp does for its ordinary boot messages) here.
[[noreturn]] void kpanic_at(const char *msg, const char *file, int line) {
    console::print("KPANIC: ");
    console::print(msg);
    console::print(" at ");
    console::print(file);
    console::print(":");
    console::print_uint(static_cast<uint64_t>(line));
    console::print("\n");

    for (;;) {
        asm volatile("wfi");
    }
}
