#include "smp.hpp"

#include <limine.h>

#include "console.hpp"
#include "mmu.hpp"
#include "threads.hpp"

namespace {

// We stash each core's assigned id in TPIDR_EL1. Limine leaves this
// register free for the kernel to use, and it is banked per-core, so each
// core reads back exactly the value it was given.
inline void write_core_id(uint64_t id) {
    asm volatile("msr tpidr_el1, %0" ::"r"(id) : "memory");
}

inline uint64_t read_core_id() {
    uint64_t id;
    asm volatile("mrs %0, tpidr_el1" : "=r"(id));
    return id;
}

extern "C" void ap_entry(limine_mp_info *info) {
    write_core_id(info->extra_argument);
    // MAIR_EL1/TCR_EL1/TTBR0_EL1 are per-core; this core needs its own copy
    // of the identity map mmu::init() built before it can reach the UART.
    mmu::activate_this_core();

    char line[48];
    char *p = line;
    p = console::append(p, "Hello from core ");
    p = console::append_uint(p, smp::me());
    p = console::append(p, ", online!\n");
    *p = '\0';
    console::print(line);

    for(;;) {
        asm volatile("wfi");
    }
}

} // namespace

namespace smp {

void init(limine_mp_response *mp) {
    // Find our own (the bootstrap processor's) index in the cpus array so
    // that me() is consistent across every core.
    uint64_t bsp_index = 0;
    for (uint64_t i = 0; i < mp->cpu_count; i++) {
        if (mp->cpus[i]->mpidr == mp->bsp_mpidr) {
            bsp_index = i;
            break;
        }
    }
    write_core_id(bsp_index);

    for (uint64_t i = 0; i < mp->cpu_count; i++) {
        if (i == bsp_index) {
            continue;
        }

        limine_mp_info *cpu = mp->cpus[i];
        cpu->extra_argument = i;
        // Publish the entry point last, with release semantics, per spec:
        // the parked core observes this with acquire semantics before it
        // jumps, so extra_argument above is guaranteed visible to it.
        __atomic_store_n(&cpu->goto_address, &ap_entry, __ATOMIC_SEQ_CST);
    }
}

uint32_t me() { return static_cast<uint32_t>(read_core_id()); }

} // namespace smp
