#include "pagetable.hpp"

#include "hhdm.hpp"
#include "mmu.hpp"
#include "panic.hpp"
#include "pmm.hpp"
#include "string.hpp"

namespace {

// ---- Descriptor bit layout -------------------------------------------
//
// At L1/L2, a valid entry is either a block (a leaf, like mmu.cpp's own
// device mapping) or a table (points at the next level down); at L3
// there's no such distinction -- a valid entry is always a leaf page.
// Bits [1:0] tell them apart: 0b00 invalid, 0b01 block (L1/L2 only),
// 0b11 table (L1/L2) or page (L3 only). Every table/page descriptor this
// file ever writes uses 0b11, since L1/L2 entries built here are always
// tables (never blocks -- that's mmu.cpp's device mapping's job, copied
// in as a ready-made value, not rebuilt here) and L3 entries are always
// pages.
constexpr uint64_t kDescValid = 0b11ULL; // bits[1:0]: table (L1/L2) or page (L3)

// Leaf (L3) page descriptor fields, 4KiB granule.
constexpr uint64_t kAttrIndxRam = 0ULL << 2; // bits[4:2]: MAIR index 0, Normal Write-Back
constexpr uint64_t kApEl1Rw = 0b00ULL << 6;  // bits[7:6]: EL1 rw, no EL0 access (ever)
constexpr uint64_t kApEl1Ro = 0b10ULL << 6;  // bits[7:6]: EL1 read-only, no EL0 access
// Inner (not outer) shareable, unlike mmu.cpp's device mapping: this is
// ordinary cacheable RAM shared coherently by this kernel's SMP cores,
// which is what Inner Shareable is for -- a deliberate difference from
// the device block's Outer Shareable, not an oversight.
constexpr uint64_t kShInner = 0b11ULL << 8; // bits[9:8]
constexpr uint64_t kAf = 1ULL << 10;        // access flag, must be set
constexpr uint64_t kPxn = 1ULL << 53;       // non-executable at EL1
constexpr uint64_t kUxn = 1ULL << 54;       // non-executable at EL0 -- always, no EL0 exists

// bits[47:12]: a 4KiB-aligned physical address, whether of the next-level
// table (L1/L2 table descriptors) or the mapped page itself (L3).
constexpr uint64_t kAddrMask = 0x0000FFFFFFFFF000ULL;

uint64_t *table_virt(uint64_t table_phys) { return hhdm::to_virt<uint64_t>(table_phys); }

uint32_t l1_index(uint64_t va) { return static_cast<uint32_t>((va >> 30) & 0x1FF); }
uint32_t l2_index(uint64_t va) { return static_cast<uint32_t>((va >> 21) & 0x1FF); }
uint32_t l3_index(uint64_t va) { return static_cast<uint32_t>((va >> 12) & 0x1FF); }

// Invalidates every cached translation for exactly `vaddr` (not the
// whole TLB -- map_page()/unmap_page() only ever change one page at a
// time), on every core ("is" broadcasts Inner Shareable).
void invalidate(uint64_t vaddr) {
    uint64_t page = vaddr >> 12;
    asm volatile(
        "dsb ishst\n"
        "tlbi vaae1is, %0\n"
        "dsb ish\n"
        "isb\n" ::"r"(page)
        : "memory");
}

// The physical address of the table `table_phys`'s entry at `index`
// points to. If that entry is invalid: allocates and zeroes a fresh
// table page and links it in when `allocate` is true, or returns 0
// (without touching anything) when it's false -- the no-allocate path
// unmap_page()/resolve() use, since there's nothing to unmap/resolve
// under a table that was never created.
uint64_t next_table(uint64_t table_phys, uint32_t index, bool allocate) {
    uint64_t *table = table_virt(table_phys);
    uint64_t entry = table[index];
    if ((entry & 1) != 0) {
        return entry & kAddrMask;
    }
    if (!allocate) {
        return 0;
    }

    uint64_t new_phys = pmm::alloc();
    ASSERT(new_phys != 0);
    memset(table_virt(new_phys), 0, pmm::kPageSize);
    table[index] = (new_phys & kAddrMask) | kDescValid;
    return new_phys;
}

} // namespace

namespace pagetable {

uint64_t new_address_space() {
    uint64_t l1_phys = pmm::alloc();
    ASSERT(l1_phys != 0);
    uint64_t *l1 = table_virt(l1_phys);
    memset(l1, 0, pmm::kPageSize);
    l1[0] = mmu::device_l1_descriptor();
    return l1_phys;
}

void destroy_address_space(uint64_t ttbr0_phys) {
    uint64_t *l1 = table_virt(ttbr0_phys);
    // Index 0 is the shared device descriptor, a value copied in rather
    // than a table this address space owns -- skip it, start at 1.
    for (uint32_t i1 = 1; i1 < 512; i1++) {
        if ((l1[i1] & 1) == 0) {
            continue;
        }
        uint64_t l2_phys = l1[i1] & kAddrMask;
        uint64_t *l2 = table_virt(l2_phys);
        for (uint32_t i2 = 0; i2 < 512; i2++) {
            if ((l2[i2] & 1) == 0) {
                continue;
            }
            uint64_t l3_phys = l2[i2] & kAddrMask;
            uint64_t *l3 = table_virt(l3_phys);
            for (uint32_t i3 = 0; i3 < 512; i3++) {
                if ((l3[i3] & 1) != 0) {
                    pmm::free(l3[i3] & kAddrMask);
                }
            }
            pmm::free(l3_phys);
        }
        pmm::free(l2_phys);
    }
    pmm::free(ttbr0_phys);
}

void map_page(uint64_t ttbr0_phys, uint64_t vaddr, uint64_t paddr, PageFlags flags) {
    ASSERT(vaddr % pmm::kPageSize == 0 && paddr % pmm::kPageSize == 0);

    uint64_t l2_phys = next_table(ttbr0_phys, l1_index(vaddr), /*allocate=*/true);
    uint64_t l3_phys = next_table(l2_phys, l2_index(vaddr), /*allocate=*/true);
    uint64_t *l3 = table_virt(l3_phys);

    uint64_t desc = kDescValid | kAttrIndxRam | kShInner | kAf | kUxn | (paddr & kAddrMask);
    desc |= has_flag(flags, PageFlags::kWrite) ? kApEl1Rw : kApEl1Ro;
    if (!has_flag(flags, PageFlags::kExec)) {
        desc |= kPxn;
    }

    l3[l3_index(vaddr)] = desc;
    invalidate(vaddr);
}

void unmap_page(uint64_t ttbr0_phys, uint64_t vaddr) {
    uint64_t l2_phys = next_table(ttbr0_phys, l1_index(vaddr), /*allocate=*/false);
    if (l2_phys == 0) {
        return;
    }
    uint64_t l3_phys = next_table(l2_phys, l2_index(vaddr), /*allocate=*/false);
    if (l3_phys == 0) {
        return;
    }
    table_virt(l3_phys)[l3_index(vaddr)] = 0;
    invalidate(vaddr);
}

uint64_t resolve(uint64_t ttbr0_phys, uint64_t vaddr) {
    uint64_t l2_phys = next_table(ttbr0_phys, l1_index(vaddr), /*allocate=*/false);
    if (l2_phys == 0) {
        return 0;
    }
    uint64_t l3_phys = next_table(l2_phys, l2_index(vaddr), /*allocate=*/false);
    if (l3_phys == 0) {
        return 0;
    }
    uint64_t entry = table_virt(l3_phys)[l3_index(vaddr)];
    return (entry & 1) != 0 ? (entry & kAddrMask) : 0;
}

void activate(uint64_t ttbr0_phys) {
    asm volatile(
        "msr ttbr0_el1, %0\n"
        "isb\n"
        "tlbi vmalle1\n"
        "dsb ish\n"
        "isb\n" ::"r"(ttbr0_phys)
        : "memory");
}

} // namespace pagetable
