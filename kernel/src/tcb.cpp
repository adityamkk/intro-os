#include "tcb.hpp"

#include "context_switch.hpp"

namespace {

uint64_t g_next_tid = 0;

} // namespace

TCB::TCB(void *initial_sp, void *stack_base)
    : sp(initial_sp),
      tid(__atomic_fetch_add(&g_next_tid, 1, __ATOMIC_RELAXED)),
      stack_base(stack_base) {}

TCB::~TCB() { delete[] static_cast<uint8_t *>(stack_base); }

void switch_to(TCB *prev, TCB *next) {
    if (prev == nullptr) {
        // No real predecessor -- discard the outgoing sp into a scratch
        // local instead of dereferencing a null TCB.
        void *scratch;
        context_switch(&scratch, &next->sp);
        return;
    }
    context_switch(&prev->sp, &next->sp);
}
