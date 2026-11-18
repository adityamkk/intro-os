#pragma once

#include <cstdint>

#include "fat32.hpp"
#include "pagetable.hpp"

// A VME ("virtual memory extent") describes one kmmap()'d, page-aligned
// range of a thread's own address space: [start, end), what protection it
// has, and which file (and offset into it) backs it. A TCB's VME list
// (vme_head) is these, kept as a singly linked list sorted ascending by
// `start` -- simple to walk for find()/overlaps(), and simple to splice
// nodes into/out of for kmmap()/kmunmap()'s insert/delete/split.
//
// Only ever touched by its own thread's kmmap()/kmunmap() calls and by
// the page-fault handler running synchronously as that same thread (a
// same-EL exception never changes which thread is executing) -- so,
// unlike most of this kernel's shared state, nothing here needs a lock.
struct VmeNode {
    uint64_t start; // page-aligned
    uint64_t end;   // page-aligned, exclusive
    pagetable::PageFlags flags;
    File backing_file; // this VME's own copy -- see File's own docs
    uint64_t file_offset; // file offset corresponding to `start`
    VmeNode *next;
};

namespace vme {

// The node covering `vaddr`, or nullptr if none does.
VmeNode *find(VmeNode *head, uint64_t vaddr);

// True if any node overlaps [start, end).
bool overlaps(VmeNode *head, uint64_t start, uint64_t end);

// Splices `node` into the list rooted at `head`, keeping it sorted
// ascending by `start`. Does not check for overlap -- callers check that
// themselves first (see overlaps()) since what to do about one isn't
// this function's job.
void insert(VmeNode *&head, VmeNode *node);

} // namespace vme
