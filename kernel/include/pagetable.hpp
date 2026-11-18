#pragma once

#include <cstdint>

// Per-address-space page table management: builds a fresh private TTBR0
// table for a thread, maps/unmaps individual 4KiB pages in it (using the
// HHDM, see hhdm.hpp, to reach the physical page-table pages themselves),
// and tears a whole address space down.
//
// TCR_EL1 is configured once, globally, by mmu::activate_this_core() --
// T0SZ=25 (39-bit input address, 4KiB granule, walk starts at level 1) --
// and every address space built here assumes exactly that: a 3-level
// walk, L1 covering 1GiB per entry, L2 covering 2MiB per entry, L3
// covering 4KiB per entry (leaf). L1 index 0 is always the device MMIO
// block mmu.cpp already sets up (see mmu::device_l1_descriptor()); this
// is why the kmmap-able window below starts at L1 index 1, not 0.
namespace pagetable {

// The only range of virtual addresses kmmap() is allowed to use -- 1GiB,
// immediately after the (also 1GiB) device MMIO region every address
// space shares.
constexpr uint64_t kUserBase = 0x40000000ULL; // 1 GiB
constexpr uint64_t kUserEnd = 0x80000000ULL;  // 2 GiB

enum class PageFlags : uint32_t {
    kNone = 0,
    kWrite = 1u << 0,
    kExec = 1u << 1,
};

inline bool has_flag(PageFlags flags, PageFlags bit) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(bit)) != 0;
}

// Builds a fresh, otherwise-empty private address space: a new L1 table
// whose only populated entry is index 0, a copy of the shared device MMIO
// block descriptor (see mmu::device_l1_descriptor()) -- so this thread
// can still reach the UART/disk no matter whose TTBR0 is active. Returns
// its physical address (what to load into TTBR0_EL1, i.e. what a TCB's
// ttbr0_phys field holds).
uint64_t new_address_space();

// Frees every physical page this address space owns: every resident leaf
// page, every L2/L3 table page, and the L1 table itself (but never the
// device descriptor at L1 index 0 -- that's a value copied in, not a
// pointer to anything this address space itself owns).
void destroy_address_space(uint64_t ttbr0_phys);

// Maps `vaddr` (must be 4KiB-aligned) to `paddr` (must be 4KiB-aligned,
// and should be a page this caller owns -- e.g. just pmm::alloc()'d) in
// the address space rooted at `ttbr0_phys`, with the given flags.
// Allocates (via pmm::alloc(), zeroing through the HHDM) any L2/L3 table
// page that doesn't exist yet along the way. Overwrites any previous
// mapping at `vaddr` without freeing its old physical page -- callers
// that are replacing a mapping are responsible for freeing it themselves
// first if needed.
void map_page(uint64_t ttbr0_phys, uint64_t vaddr, uint64_t paddr, PageFlags flags);

// Removes whatever mapping `vaddr` has in the address space rooted at
// `ttbr0_phys`, if any. Does NOT free the physical page it was mapped
// to -- callers get that address from resolve() (below) first if they
// need to pmm::free() it. A no-op if `vaddr` was never mapped.
void unmap_page(uint64_t ttbr0_phys, uint64_t vaddr);

// The physical address `vaddr` is currently mapped to in the address
// space rooted at `ttbr0_phys`, or 0 if it isn't mapped.
uint64_t resolve(uint64_t ttbr0_phys, uint64_t vaddr);

inline bool is_resident(uint64_t ttbr0_phys, uint64_t vaddr) {
    return resolve(ttbr0_phys, vaddr) != 0;
}

// Loads `ttbr0_phys` into this core's TTBR0_EL1 and flushes the TLB.
// Mirrors mmu::activate_this_core()'s own ttbr0/isb/tlbi/dsb/isb sequence.
void activate(uint64_t ttbr0_phys);

} // namespace pagetable
