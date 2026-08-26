#pragma once

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

namespace test {

// A test's main() (or whichever thread it hands off to) should call
// exactly one of these, as its very last action -- both stop the calling
// thread themselves, which every thread's entry function must do anyway
// (see threads::make_tcb()).
//
// `make test <name>` greps the test's console output for these exact
// marker lines to decide pass/fail -- keep them in sync with the
// Makefile's TEST_TEMPLATE if either ever changes.
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
