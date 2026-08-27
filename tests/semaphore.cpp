#include "test_util.hpp"

#include "semaphore.hpp"
#include "threads.hpp"

// Exercises Semaphore::down()/up(): a thread blocked in down() must not
// proceed until another thread calls up(). This can't assert a specific
// interleaving order between the two threads (not guaranteed with
// multiple cores) -- instead it gives the blocked thread every chance to
// (wrongly) proceed before up() is ever called, confirms it hasn't, then
// confirms it does proceed once up() is actually called.
//
// Only main() ever prints. Each console::print() call is individually
// atomic, but a multi-call message isn't (see console.hpp) -- keeping
// blocked_thread silent avoids any risk of its output interleaving
// mid-line with main()'s.
namespace {

Semaphore *g_sem = nullptr;

// Set by blocked_thread only after down() returns.
volatile bool g_unblocked = false;

constexpr int kSpinRounds = 2000;

void blocked_thread() {
    g_sem->down();
    g_unblocked = true;
}

} // namespace

extern "C" void main() {
    g_sem = new Semaphore(0); // starts at 0: the first down() must block

    threads::go(blocked_thread);

    // Give blocked_thread every chance to (wrongly) run past down()
    // before up() is ever called.
    for (int i = 0; i < kSpinRounds; i++) {
        threads::yield();
    }
    if (g_unblocked) {
        test::fail("down() returned before up() was ever called");
    }

    g_sem->up();

    // Give blocked_thread a chance to actually resume now that it's been
    // woken, without waiting the full kSpinRounds if it wakes up sooner.
    for (int i = 0; i < kSpinRounds; i++) {
        if (g_unblocked) {
            break;
        }
        threads::yield();
    }
    if (!g_unblocked) {
        test::fail("down() never returned after up() was called");
    }

    test::pass();
}
