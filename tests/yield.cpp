#include "test_util.hpp"

#include "threads.hpp"

// Exercises threads::yield(): a thread that yields must (a) actually
// return control to its caller afterward, with its own state intact --
// unlike threads::stop(), which never returns -- and (b) actually give
// another ready thread a chance to run in between, not just spin in
// place until it's done.
//
// This can't assert a specific interleaving order between thread_a and
// thread_b: with multiple cores, either could genuinely run in parallel
// on a different core rather than time-sliced on the same one. Instead
// it checks order-independent invariants that only hold if yield() is
// actually working.
namespace {

constexpr int kRounds = 5;

// Bumped on every step either thread takes -- its own first run and each
// resumption after a yield(). If yield() were broken (didn't return,
// corrupted the caller's state, or never let anything else run), this
// wouldn't reach 2 * kRounds.
int g_steps = 0;

// Set once thread_b has taken at least one step -- proves the *other*
// thread genuinely got scheduled, not just that thread_a ran to
// completion on its own.
volatile bool g_b_has_run = false;

void thread_b() {
    for (int i = 0; i < kRounds; i++) {
        __atomic_add_fetch(&g_steps, 1, __ATOMIC_SEQ_CST);
        g_b_has_run = true;
        threads::yield();
    }
}

void thread_a() {
    for (int i = 0; i < kRounds; i++) {
        __atomic_add_fetch(&g_steps, 1, __ATOMIC_SEQ_CST);
        threads::yield();
        // yield() must have returned here -- if it instead behaved like
        // stop(), this loop (and the check below) would simply never
        // run again, and the test would time out instead of reaching
        // test::pass().
    }

    // Every one of thread_a's own yields gave the scheduler a chance to
    // run something else. Wait (yielding again each time, so this
    // doesn't just spin ahead of thread_b on the same core) until
    // thread_b has taken all of its steps too.
    while (__atomic_load_n(&g_steps, __ATOMIC_SEQ_CST) < 2 * kRounds) {
        threads::yield();
    }

    if (!g_b_has_run) {
        test::fail("thread_b never ran");
    }

    test::pass();
}

} // namespace

extern "C" void main() {
    threads::go(thread_a);
    threads::go(thread_b);
}
