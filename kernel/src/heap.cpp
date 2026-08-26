#include "heap.hpp"

#include <cstdint>

#include "spinlock.hpp"

namespace {

// Arbitrary but generous for a kernel that isn't doing much allocation
// yet; bump this if it's not enough.
constexpr size_t kArenaSize = 2 * 1024 * 1024; // 2 MiB

alignas(16) uint8_t g_arena[kArenaSize];

// Every block, whether free or in use, is bracketed by two copies of this
// tag: one right before the payload (the "header") and one right after it
// (the "footer"). Mirroring the tag lets free() find and merge with
// either neighboring block in O(1): the bytes right before a block's
// header are always the previous block's footer, and the bytes right
// after its footer are always the next block's header.
//
// Two size_t fields (rather than a size_t plus a bool) keep sizeof(Tag) an
// unambiguous 16 bytes, matching kAlignment, with no reliance on compiler
// padding choices.
struct Tag {
    size_t block_size; // header + payload + footer, in bytes
    size_t in_use;      // 0 = free, 1 = allocated
};

constexpr size_t kAlignment = 16;
constexpr size_t kTagSize = sizeof(Tag);
// Smallest possible block: header + footer around a zero-length payload.
constexpr size_t kMinBlockSize = 2 * kTagSize;

Spinlock g_lock;

size_t align_up(size_t n, size_t alignment) { return (n + alignment - 1) & ~(alignment - 1); }

Tag *footer_of(Tag *header) {
    return reinterpret_cast<Tag *>(reinterpret_cast<uint8_t *>(header) + header->block_size - kTagSize);
}

void *payload_of(Tag *header) { return reinterpret_cast<uint8_t *>(header) + kTagSize; }

Tag *header_of(void *payload) { return reinterpret_cast<Tag *>(static_cast<uint8_t *>(payload) - kTagSize); }

// Writes matching header and footer tags for a block of `size` bytes
// starting at `header`.
void write_tags(Tag *header, size_t size, bool in_use) {
    header->block_size = size;
    header->in_use = in_use;
    Tag *footer = footer_of(header);
    footer->block_size = size;
    footer->in_use = in_use;
}

Tag *arena_begin() { return reinterpret_cast<Tag *>(g_arena); }
uint8_t *arena_end() { return g_arena + kArenaSize; }

// The block physically following `header`, or nullptr if `header` is the
// last block in the arena.
Tag *next_block(Tag *header) {
    uint8_t *next = reinterpret_cast<uint8_t *>(header) + header->block_size;
    return next < arena_end() ? reinterpret_cast<Tag *>(next) : nullptr;
}

// The block physically preceding `header`, or nullptr if `header` is the
// first block in the arena. Found via the tag just before `header`, which
// is that block's footer.
Tag *prev_block(Tag *header) {
    if (reinterpret_cast<uint8_t *>(header) == g_arena) {
        return nullptr;
    }
    Tag *prev_footer = reinterpret_cast<Tag *>(reinterpret_cast<uint8_t *>(header) - kTagSize);
    return reinterpret_cast<Tag *>(reinterpret_cast<uint8_t *>(header) - prev_footer->block_size);
}

} // namespace

namespace heap {

void init() { write_tags(arena_begin(), kArenaSize, /*in_use=*/false); }

void *malloc(size_t size) {
    if (size == 0) {
        return nullptr;
    }

    size_t needed = align_up(size, kAlignment) + 2 * kTagSize;

    g_lock.lock();

    for (Tag *block = arena_begin(); block != nullptr; block = next_block(block)) {
        if (block->in_use || block->block_size < needed) {
            continue;
        }

        // Split off the remainder as its own free block if it's big
        // enough to stand alone; otherwise hand the whole block over,
        // wasting a few bytes rather than creating an unusable sliver.
        size_t remainder = block->block_size - needed;
        if (remainder >= kMinBlockSize) {
            write_tags(block, needed, /*in_use=*/true);
            write_tags(next_block(block), remainder, /*in_use=*/false);
        } else {
            write_tags(block, block->block_size, /*in_use=*/true);
        }

        g_lock.unlock();
        return payload_of(block);
    }

    g_lock.unlock();
    return nullptr;
}

void free(void *ptr) {
    if (ptr == nullptr) {
        return;
    }

    g_lock.lock();

    Tag *block = header_of(ptr);
    Tag *lo = block;
    size_t size = block->block_size;

    // Merge with the next block first, while `block` still points at our
    // own header (merging backward below reassigns `lo`, not `block`).
    if (Tag *next = next_block(block); next != nullptr && !next->in_use) {
        size += next->block_size;
    }

    if (Tag *prev = prev_block(block); prev != nullptr && !prev->in_use) {
        lo = prev;
        size += prev->block_size;
    }

    write_tags(lo, size, /*in_use=*/false);

    g_lock.unlock();
}

} // namespace heap

namespace {

// operator new is required to either succeed or report failure by
// throwing std::bad_alloc, but this kernel is built with -fno-exceptions,
// so there's no way to report failure at all -- hang instead, the same
// way kmain() does for other unrecoverable init failures.
[[noreturn]] void out_of_memory() {
    for (;;) {
        asm volatile("wfi");
    }
}

} // namespace

// This kernel is -nostdlib with no libsupc++ linked in, so nothing else
// provides these -- every allocating/deallocating expression in the
// kernel bottoms out here.
void *operator new(size_t size) {
    void *ptr = heap::malloc(size);
    if (ptr == nullptr) {
        out_of_memory();
    }
    return ptr;
}

void *operator new[](size_t size) { return ::operator new(size); }

void operator delete(void *ptr) noexcept { heap::free(ptr); }
void operator delete[](void *ptr) noexcept { heap::free(ptr); }

// Sized deallocation (C++14+): the compiler may prefer these over the
// unsized forms above when both are visible, so both must be defined.
void operator delete(void *ptr, size_t) noexcept { heap::free(ptr); }
void operator delete[](void *ptr, size_t) noexcept { heap::free(ptr); }
