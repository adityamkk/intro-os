#include "tcb.hpp"

#include "context_switch.hpp"
#include "pagetable.hpp"
#include "vme.hpp"

namespace {

uint64_t g_next_tid = 0;

} // namespace

TCB::TCB(void *initial_sp, void *stack_base)
    : sp(initial_sp),
      tid(__atomic_fetch_add(&g_next_tid, 1, __ATOMIC_RELAXED)),
      stack_base(stack_base),
      ttbr0_phys(pagetable::new_address_space()),
      vme_head(nullptr) {}

TCB::~TCB() {
    while (vme_head != nullptr) {
        VmeNode *next = vme_head->next;
        delete vme_head;
        vme_head = next;
    }
    pagetable::destroy_address_space(ttbr0_phys);
    delete[] static_cast<uint8_t *>(stack_base);
}

void switch_to(TCB *prev, TCB *next) {
    // Unconditionally, even if prev's and next's tables happen to be the
    // same physical address: nothing here manages ASIDs (every table is
    // implicitly ASID 0), so any time TTBR0 *does* change -- which this
    // call has no cheaper way to rule out than just checking -- skipping
    // the reload/flush risks serving a stale ASID-0 translation left over
    // from whichever table was active before.
    pagetable::activate(next->ttbr0_phys);

    if (prev == nullptr) {
        // No real predecessor -- discard the outgoing sp into a scratch
        // local instead of dereferencing a null TCB.
        void *scratch;
        context_switch(&scratch, &next->sp);
        return;
    }
    context_switch(&prev->sp, &next->sp);
}
