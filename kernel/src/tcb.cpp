#include "tcb.hpp"

namespace {

uint64_t g_next_tid = 0;

} // namespace

TCB::TCB(void *initial_sp)
    : sp(initial_sp), tid(__atomic_fetch_add(&g_next_tid, 1, __ATOMIC_RELAXED)) {}
