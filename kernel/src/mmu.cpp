#include "mmu.hpp"

namespace {

alignas(4096) uint64_t g_l1_table[512];
uint64_t g_l1_table_phys = 0;

// MAIR_EL1 attribute index we use for device memory. Index 0 is guaranteed
// by the Limine spec to already be Normal Write-Back (used for RAM); index
// 1 is reserved for the framebuffer's caching type. All other indices are
// guaranteed unused, so index 2 is ours to define.
constexpr uint64_t kDeviceAttrIndex = 2;
constexpr uint64_t kMairDeviceNGnRnE = 0x00ULL; // Device-nGnRnE encoding

// Level-1 block descriptor fields (4KiB granule; a level-1 entry covers
// a 1GiB region when it's a block rather than a table).
constexpr uint64_t kDescValidBlock = 0b01ULL;              // bits[1:0]: valid + block
constexpr uint64_t kAttrIndx = kDeviceAttrIndex << 2;      // bits[4:2]
constexpr uint64_t kApEl1Rw = 0b00ULL << 6;                // bits[7:6]: EL1 rw, no EL0 access
constexpr uint64_t kShOuter = 0b10ULL << 8;                // bits[9:8]: outer shareable
constexpr uint64_t kAf = 1ULL << 10;                       // access flag, must be set
constexpr uint64_t kPxn = 1ULL << 53;                      // non-executable at EL1
constexpr uint64_t kUxn = 1ULL << 54;                      // non-executable at EL0

} // namespace

namespace mmu {

void init(uint64_t kernel_physical_base, uint64_t kernel_virtual_base) {
    g_l1_table[0] = kDescValidBlock | kAttrIndx | kApEl1Rw | kShOuter | kAf | kPxn | kUxn;

    uint64_t table_vaddr = reinterpret_cast<uint64_t>(g_l1_table);
    g_l1_table_phys = table_vaddr - kernel_virtual_base + kernel_physical_base;
}

void activate_this_core() {
    // MAIR_EL1, TCR_EL1 and TTBR0_EL1 are all per-core registers, so every
    // core -- the bootstrap processor and each secondary core started by
    // smp::init() -- must run this itself before it can touch device MMIO,
    // even though they all point at the one shared table built by init().
    uint64_t mair;
    asm volatile("mrs %0, mair_el1" : "=r"(mair));
    mair &= ~(0xFFULL << (kDeviceAttrIndex * 8));
    mair |= kMairDeviceNGnRnE << (kDeviceAttrIndex * 8);
    asm volatile("msr mair_el1, %0" ::"r"(mair));

    // Only touch the TTBR0-related fields (bits 0-15: T0SZ, EPD0, IRGN0,
    // ORGN0, SH0, TG0); leave the TTBR1 fields Limine already configured
    // for our own higher-half mapping untouched.
    uint64_t tcr;
    asm volatile("mrs %0, tcr_el1" : "=r"(tcr));
    tcr &= ~0xFFFFULL;
    tcr |= 25ULL; // T0SZ = 25 -> 39-bit input address space, walk starts at level 1
    asm volatile("msr tcr_el1, %0" ::"r"(tcr));

    asm volatile("msr ttbr0_el1, %0" ::"r"(g_l1_table_phys));

    asm volatile(
        "isb\n"
        "tlbi vmalle1\n"
        "dsb ish\n"
        "isb\n" ::
            : "memory");
}

} // namespace mmu
