#include "test_util.hpp"

// The simplest possible test: proves the harness itself works end to end
// -- kmain() schedules this main(), it runs as a real thread, and it can
// print and report a result.
extern "C" void main() {
    console::print("Hello, test!\n");
    test::pass();
}
