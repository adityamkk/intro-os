#pragma once

#include <cstdint>

// Limine's Higher Half Direct Map only covers RAM-like regions (usable,
// bootloader-reclaimable, executable/modules, framebuffer) -- it does not
// cover raw device MMIO such as the UART. To reach the UART we set up our
// own tiny page table, loaded into TTBR0_EL1, which the Limine spec leaves
// free for the kernel to use however it likes.
namespace mmu {

// Builds the (shared, one-copy) page table that identity-maps physical
// [0, 1GiB) as Device-nGnRnE memory. Call once, from the bootstrap
// processor, before any core calls activate_this_core().
// `kernel_physical_base` / `kernel_virtual_base` come from Limine's
// Executable Address feature, and are needed to translate the table's own
// (virtual) address into the physical address TTBR0_EL1 requires.
void init(uint64_t kernel_physical_base, uint64_t kernel_virtual_base);

// Points this core's TTBR0_EL1 at the table built by init(). MAIR_EL1,
// TCR_EL1 and TTBR0_EL1 are all per-core, so every core -- including each
// one started by smp::init() -- must call this itself before touching
// device MMIO.
void activate_this_core();

// Translates a virtual address inside the kernel image (any static or
// global object, including the heap's arena) to the physical address a
// DMA-capable device must be given. Only valid after init(), and only for
// addresses within the kernel image -- not for device MMIO or stack memory
// outside it.
uint64_t virt_to_phys(const void *vaddr);

} // namespace mmu
