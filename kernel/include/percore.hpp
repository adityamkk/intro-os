#pragma once

#include <cstdint>

#include "tcb.hpp"

// Per-core scheduling state, indexed by smp::me().
namespace percore {

// Upper bound on the number of cores this kernel supports (see the SMP
// variable in the Makefile); bump if booted with more than this many.
constexpr uint32_t kMaxCores = 8;

struct PerCore {
    // The TCB most recently switched away from on this core. Written only
    // by idle_loop() (percore.cpp) right before it context_switch()es.
    TCB *prev = nullptr;

    // A completed / stopped TCB that is ready to delete.
    TCB *to_delete = nullptr;

    // A TCB that is ready to be scheduled.
    TCB *to_reschedule = nullptr;

    // The TCB currently running on this core (or, mid-switch, about to
    // be).
    TCB *curr = nullptr;

    // This core's idle thread: built once by init() below and never put
    // on the scheduler's ready queue. Runs forever, falling back to it
    // whenever the ready queue has nothing else for this core to run.
    TCB *idle = nullptr;
};

// g_table[smp::me()] is this core's own entry. `prev`/`curr` start
// nullptr; `idle` starts nullptr too until init() runs.
extern PerCore g_table[kMaxCores];

// Builds an idle TCB (same technique as threads::make_tcb()) for every
// entry in g_table. Must be called exactly once, by the bootstrap
// processor, after heap::init() and before any other core can read
// g_table -- i.e. before smp::init() releases them.
void init();

} // namespace percore
