#pragma once

#include <cstdint>

struct limine_memmap_response;

// A simple physical page allocator: a linked list of free 4KiB pages,
// threaded through the pages' own memory (via their HHDM mapping, see
// hhdm.hpp) rather than any separate bookkeeping structure.
namespace pmm {

constexpr uint64_t kPageSize = 4096;

// Seeds the free list from every LIMINE_MEMMAP_USABLE region of `memmap`.
// Must be called exactly once, by the bootstrap processor, after
// hhdm::init() and before any core calls alloc()/free() -- in particular
// before percore::init(), since every TCB it builds allocates its own
// address space.
void init(const limine_memmap_response *memmap);

// Returns the physical address of a free page, or 0 if none remain. The
// page's contents are NOT zeroed -- callers that are about to use it for
// anything a thread could later observe (a fresh page table, a newly
// faulted-in data page) must zero it themselves via hhdm::to_virt() first.
// Safe to call concurrently from multiple cores.
uint64_t alloc();

// Returns a page previously handed out by alloc() to the free list. Safe
// to call concurrently from multiple cores.
void free(uint64_t phys_addr);

} // namespace pmm
