#pragma once

#include <cstdint>

#include "fat32.hpp"
#include "pagetable.hpp"

// A simplified mmap/munmap: maps/unmaps file-backed, private regions of
// the CALLING thread's own address space (never another thread's -- see
// vme.hpp for why that's always safe without a lock). "Private" means
// writes are never written back to the file; "file-backed" means every
// mapping has one, so the page-fault handler's page-in path (see
// exceptions.cpp) is always just "read this File at this offset," with
// anything past the file's length left zero.
namespace kmmap {

// Maps [vaddr, vaddr+length) to `file` starting at `file_offset`, with
// the given protection. Both `vaddr` and `length` must be page-aligned
// and the whole range must lie within
// [pagetable::kUserBase, pagetable::kUserEnd). Nothing is actually paged
// in here -- that happens lazily, one page at a time, the first time
// each page is touched (see exceptions.cpp's handle_page_fault()).
//
// Returns false, changing nothing, if `vaddr`/`length` aren't
// page-aligned, the range falls outside the kmmap-able window, or it
// overlaps a range this same thread has already kmmap()'d.
bool kmmap(uint64_t vaddr, uint64_t length, File file, uint64_t file_offset,
           pagetable::PageFlags flags);

// Unmaps [vaddr, vaddr+length) (both must be page-aligned) from the
// calling thread's own address space: deletes, shrinks, or splits
// whichever of its VMEs overlap the range, freeing the physical page (if
// any) behind every page in it that had actually been faulted in. A
// sub-range that was never kmmap()'d (or already kmunmap()'d) is simply
// skipped, the same way real munmap(2) never fails just because part of
// a range wasn't mapped. Always succeeds.
void kmunmap(uint64_t vaddr, uint64_t length);

} // namespace kmmap
