#include "test_util.hpp"

#include "heap.hpp"

// Exercises the kernel heap (kernel/include/heap.hpp): a block is
// actually usable memory (not just a non-null pointer), and free() makes
// its block available for reuse.
extern "C" void main() {
    void *a = heap::malloc(64);
    if (a == nullptr) {
        test::fail("malloc(64) returned nullptr");
    }

    auto *bytes = static_cast<unsigned char *>(a);
    for (int i = 0; i < 64; i++) {
        bytes[i] = static_cast<unsigned char>(i);
    }
    for (int i = 0; i < 64; i++) {
        if (bytes[i] != static_cast<unsigned char>(i)) {
            test::fail("read-back mismatch after malloc(64)");
        }
    }

    heap::free(a);

    // Nothing else has allocated in between, so a same-size allocation
    // right after free() should reuse the exact block just freed
    // (first-fit).
    void *b = heap::malloc(64);
    if (b != a) {
        test::fail("free() didn't make its block reusable");
    }
    heap::free(b);

    test::pass();
}
