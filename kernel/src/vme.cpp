#include "vme.hpp"

namespace vme {

VmeNode *find(VmeNode *head, uint64_t vaddr) {
    for (VmeNode *v = head; v != nullptr; v = v->next) {
        if (vaddr >= v->start && vaddr < v->end) {
            return v;
        }
    }
    return nullptr;
}

bool overlaps(VmeNode *head, uint64_t start, uint64_t end) {
    for (VmeNode *v = head; v != nullptr; v = v->next) {
        if (start < v->end && v->start < end) {
            return true;
        }
    }
    return false;
}

void insert(VmeNode *&head, VmeNode *node) {
    VmeNode **slot = &head;
    while (*slot != nullptr && (*slot)->start < node->start) {
        slot = &(*slot)->next;
    }
    node->next = *slot;
    *slot = node;
}

} // namespace vme
