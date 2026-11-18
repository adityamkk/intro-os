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

    /*
        TODO: Insert a VmeNode into the VmeNode Linked List
        1. How do the VmeNodes tell you where to insert the new node?
        2. What is the runtime complexity of this insertion operation?
        3. Is there a better data structure other than a sorted linked list?
    */
}

} // namespace vme
