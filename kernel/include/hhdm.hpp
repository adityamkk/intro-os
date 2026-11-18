#pragma once

#include <cstdint>

// Limine's Higher Half Direct Map: a fixed offset such that, for every
// physical address P that's part of ordinary RAM, `hhdm::offset() + P` is
// a virtual address the kernel can read/write through TTBR1 (Limine's own
// mapping, set up once at boot and never touched by anything in this
// kernel) -- regardless of which TTBR0 table happens to be active on this
// core. This is how pmm.cpp and pagetable.cpp touch physical pages they've
// just allocated (to link them into a page table, zero them, or fill them
// with file data) without needing those pages mapped anywhere else first.
namespace hhdm {

// Must be called once, by the bootstrap processor, before any call to
// to_virt() (pmm::init() and everything after it depends on this).
void init(uint64_t offset);

// The raw offset itself. Most callers should prefer to_virt() below;
// this is exposed for the rare case that needs the plain integer.
uint64_t offset();

// `phys` interpreted as a T* reachable through the HHDM.
template <typename T>
T *to_virt(uint64_t phys) {
    return reinterpret_cast<T *>(offset() + phys);
}

} // namespace hhdm
