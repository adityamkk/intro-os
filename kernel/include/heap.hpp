#pragma once

#include <cstddef>

// A simple first-fit, coalescing kernel heap backed by a fixed-size static
// arena (see heap.cpp) -- there's no page allocator yet for it to request
// memory from, so it can't grow past that arena. Global operator
// new/delete (bottom of heap.cpp) are implemented on top of malloc()/
// free() here, so this is what every `new`/`delete` in the kernel reaches.
namespace heap {

// Lays out the arena as one large free block. Must be called exactly
// once, by the bootstrap processor, before any core calls malloc()/free()
// or uses new/delete -- there's no other startup code in this kernel to
// run a constructor for the arena automatically.
void init();

// Returns a block of at least `size` bytes, 16-byte aligned, or nullptr if
// no free block big enough remains. Safe to call concurrently from
// multiple cores.
void *malloc(size_t size);

// Frees a block previously returned by malloc(). Passing nullptr is a
// no-op, mirroring free(3). Safe to call concurrently from multiple
// cores.
void free(void *ptr);

} // namespace heap
