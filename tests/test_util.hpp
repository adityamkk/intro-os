#pragma once

#include <cstdint>

#include "console.hpp"
#include "threads.hpp"

// Shared helpers for tests/*.cpp test cases.
//
// Every test file must define `main()` with exactly this signature --
// kmain() (kernel/src/main.cpp) schedules this exact symbol to run, and
// only extern "C" linkage gives it the unmangled name needed to override
// the kernel's weak default `main`. Declaring it here means a test that
// gets this wrong (e.g. forgets extern "C", or takes/returns something)
// fails to compile instead of silently linking against the default and
// never actually running.
extern "C" void main();

// `make test` verifies a test by diffing its console output (everything
// after kmain()'s "--- kernel output begins ---" anchor, see
// kernel/src/main.cpp) against tests/<name>.ok, byte for byte. The normal
// boot banner and each secondary core's online greeting both embed data
// (which core printed it, in what order relative to the others, `-smp`'s
// setting) that isn't deterministic -- so every test suppresses them by
// overriding these two weak hooks. A test that wants its own kernel
// output must account for whatever it or the threads it starts print,
// same as it always would.
// Not `inline`: GCC emits an `inline`-linkage extern "C" function as a
// *weak* symbol too, and a weak symbol doesn't override another weak
// symbol at link time (the linker just keeps whichever one it saw
// first) -- these need to be ordinary strong definitions to actually
// replace the kernel's weak defaults. Since exactly one tests/*.cpp
// includes this header per test binary, there's no ODR risk in that.
extern "C" void print_boot_banner(uint32_t, uint32_t) {}
extern "C" void print_ap_greeting(uint32_t) {}

namespace test {

// A test's main() (or whichever thread it hands off to) should call
// exactly one of these, as its very last action -- both stop the calling
// thread themselves, which every thread's entry function must do anyway
// (see threads::make_tcb()).
//
// Their output becomes part of what tests/<name>.ok must match -- keep
// the two in sync if either changes.
[[noreturn]] inline void pass() {
    console::print("TEST PASS\n");
    threads::stop();
}

[[noreturn]] inline void fail(const char *reason) {
    console::print("TEST FAIL: ");
    console::print(reason);
    console::print("\n");
    threads::stop();
}

} // namespace test
