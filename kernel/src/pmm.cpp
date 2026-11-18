#include "pmm.hpp"

#include <limine.h>

#include "hhdm.hpp"
#include "panic.hpp"
#include "spinlock.hpp"

namespace {

Spinlock g_lock;

// The physical address of the first free page, or 0 if there are none.
// Physical address 0 is never handed out by init() below (nothing
// LIMINE_MEMMAP_USABLE starts anywhere near it on QEMU's "virt" machine,
// where RAM begins at 0x40000000), so 0 safely doubles as "empty".
uint64_t g_free_head = 0;

} // namespace

namespace pmm {

void init(const limine_memmap_response *memmap) {
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        const limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }
        // Limine guarantees every entry is page-aligned; this is cheap
        // insurance against silently mis-stepping through memory if that
        // were ever violated.
        ASSERT(entry->base % kPageSize == 0 && entry->length % kPageSize == 0);

        for (uint64_t off = 0; off < entry->length; off += kPageSize) {
            free(entry->base + off);
        }
    }
}

uint64_t alloc() {
    g_lock.lock();
    uint64_t page = g_free_head;
    if (page != 0) {
        g_free_head = *hhdm::to_virt<uint64_t>(page);
    }
    g_lock.unlock();
    return page;
}

void free(uint64_t phys_addr) {
    g_lock.lock();
    // The freed page's own first 8 bytes become its free-list link --
    // reached through its HHDM mapping, so this needs no storage of its
    // own anywhere else. This is also exactly why a freshly alloc()'d
    // page's contents can't be trusted: its first 8 bytes are leftover
    // free-list bookkeeping, and the rest may be whatever its previous
    // owner (another thread's page, a now-freed page table) left there.
    *hhdm::to_virt<uint64_t>(phys_addr) = g_free_head;
    g_free_head = phys_addr;
    g_lock.unlock();
}

} // namespace pmm
