#include "test_util.hpp"

#include "threads.hpp"

// Exercises threads::go()/context_switch() under real SMP concurrency:
// schedules kNumWorkers threads that each atomically increment a shared
// counter, and has whichever one's increment reaches kNumWorkers report
// success -- exactly one of them will, however the scheduler happens to
// spread them across cores (there are more workers than cores, so this
// also covers a core's idle thread going back to the scheduler more than
// once).
namespace {

constexpr int kNumWorkers = 8;
int g_run_count = 0;

void worker() {
    int count = __atomic_add_fetch(&g_run_count, 1, __ATOMIC_SEQ_CST);
    if (count == kNumWorkers) {
        test::pass();
    }
}

} // namespace

extern "C" void main() {
    for (int i = 0; i < kNumWorkers; i++) {
        threads::go(worker);
    }
}
