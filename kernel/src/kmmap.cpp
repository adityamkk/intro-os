#include "kmmap.hpp"

#include "percore.hpp"
#include "pmm.hpp"
#include "smp.hpp"
#include "tcb.hpp"
#include "vme.hpp"

namespace kmmap {

bool kmmap(uint64_t vaddr, uint64_t length, File file, uint64_t file_offset,
           pagetable::PageFlags flags) {
    if (vaddr % pmm::kPageSize != 0 || length % pmm::kPageSize != 0 || length == 0) {
        return false;
    }
    uint64_t end = vaddr + length;
    if (end <= vaddr || vaddr < pagetable::kUserBase || end > pagetable::kUserEnd) {
        return false; // misaligned length (overflow) or outside the kmmap-able window
    }

    TCB *curr = percore::g_table[smp::me()].curr;
    if (vme::overlaps(curr->vme_head, vaddr, end)) {
        return false;
    }

    VmeNode *node = new VmeNode{
        .start = vaddr,
        .end = end,
        .flags = flags,
        .backing_file = file,
        .file_offset = file_offset,
        .next = nullptr,
    };
    vme::insert(curr->vme_head, node);
    return true;
}

void kmunmap(uint64_t vaddr, uint64_t length) {
    if (length == 0) {
        return;
    }
    uint64_t end = vaddr + length;
    TCB *curr = percore::g_table[smp::me()].curr;

    VmeNode **slot = &curr->vme_head;
    while (*slot != nullptr) {
        VmeNode *v = *slot;
        uint64_t i0 = v->start > vaddr ? v->start : vaddr;
        uint64_t i1 = v->end < end ? v->end : end;
        if (i0 >= i1) {
            // This node doesn't overlap [vaddr, end) at all.
            slot = &v->next;
            continue;
        }

        // Free every physical page actually resident in [i0, i1) --
        // pages never faulted in have nothing to undo at this level.
        for (uint64_t page = i0; page < i1; page += pmm::kPageSize) {
            uint64_t phys = pagetable::resolve(curr->ttbr0_phys, page);
            if (phys != 0) {
                pagetable::unmap_page(curr->ttbr0_phys, page);
                pmm::free(phys);
            }
        }

        if (i0 == v->start && i1 == v->end) {
            // Fully covered: drop the whole node, then re-examine
            // whatever is now at *slot (the node that used to follow
            // it) without advancing -- nothing to skip.
            *slot = v->next;
            delete v;
            continue;
        }
        if (i0 == v->start) {
            // Prefix removed: what's left starts later, further into
            // the file by exactly how much was chopped off the front.
            v->file_offset += i1 - v->start;
            v->start = i1;
        } else if (i1 == v->end) {
            // Suffix removed: what's left is unchanged except shorter.
            v->end = i0;
        } else {
            // A hole in the middle: split into two surviving VMEs. The
            // new right-hand piece starts right after the hole and
            // keeps going to the original end; its file offset is
            // shifted the same way a prefix-removal's would be.
            VmeNode *right = new VmeNode{
                .start = i1,
                .end = v->end,
                .flags = v->flags,
                .backing_file = v->backing_file,
                .file_offset = v->file_offset + (i1 - v->start),
                .next = v->next,
            };
            v->end = i0;
            v->next = right;
        }
        slot = &v->next;
    }
}

} // namespace kmmap
