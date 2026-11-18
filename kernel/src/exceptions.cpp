#include "exceptions.hpp"

#include "console.hpp"
#include "hhdm.hpp"
#include "panic.hpp"
#include "pagetable.hpp"
#include "percore.hpp"
#include "pmm.hpp"
#include "smp.hpp"
#include "string.hpp"
#include "tcb.hpp"
#include "vme.hpp"

namespace {

// Data Abort, taken from the same Exception Level it's handled at --
// the one exception class this kernel's EL1-only, no-interrupts-yet
// world ever expects to see (ESR_EL1 bits[31:26]).
constexpr uint64_t kEcDataAbortSameEl = 0b100101;

// Reads FAR_EL1 (the faulting virtual address) and either pages the
// faulting address in (if the current thread's VME list covers it) or
// KPANICs with a clear reason -- there's no recovery for either "this
// address was never kmmap()'d" or "it's resident but still faulted"
// (the latter would mean a write to a read-only mapping; this kernel
// has no copy-on-write or permission-upgrade path, so that's a bug, not
// something to retry).
void handle_page_fault(uint64_t far) {
    TCB *curr = percore::g_table[smp::me()].curr;
    uint64_t page = far & ~(pmm::kPageSize - 1);

    VmeNode *v = vme::find(curr->vme_head, far);
    if (v == nullptr) {
        // TODO: What should we do if vme::find returns a nullptr for the faulting address?
    }
    if (pagetable::is_resident(curr->ttbr0_phys, page)) {
        KPANIC("page fault on an already-resident page (no write-protection support)");
    }

    uint64_t phys = pmm::alloc();
    if (phys == 0) {
        KPANIC("out of physical memory while handling a page fault");
    }
    uint8_t *dst = hhdm::to_virt<uint8_t>(phys);
    // Zero before anything else touches this page: whatever was here
    // before (another thread's freed data, a since-freed page table) must
    // never be observable through this mapping -- this is exactly what
    // keeps two threads' private mappings from leaking into each other.
    memset(dst, 0, pmm::kPageSize);

    /*
        TODO: Read the contents of the backing file into the page.
        Things to keep in mind:
        1. What file do we need to read into this page (if any)? Where do we get this info from?
        2. Where in the file do we need to read into? How do we calculate this offset?
        The method `File::read` in `fat32.cpp` may be useful here.
        While reading directly from disk is technically valid, there are some issues with doing this. Can you spot them?
    */

    /*
        TODO: Map this physical page in the page table for this thread
        Things to keep in mind:
        1. Where is the page table for this thread? How do we find it?
        2. What is the virtual page and physical page we want to map?
        3. What flags do we want to map this page with? Where is this information located?
        The function `pagetable::map_page` in `pagetable.cpp` may be useful here.
    */
}

} // namespace

extern "C" void handle_sync_exception() {
    uint64_t esr;
    uint64_t far;
    asm volatile("mrs %0, esr_el1" : "=r"(esr));
    asm volatile("mrs %0, far_el1" : "=r"(far));

    uint64_t ec = (esr >> 26) & 0x3F;
    if (ec != kEcDataAbortSameEl) {
        KPANIC("unhandled synchronous exception class (not a same-EL data abort)");
    }
    handle_page_fault(far);
}

extern "C" [[noreturn]] void exceptions_unhandled_vector(uint64_t vector) {
    char line[64];
    char *p = line;
    p = console::append(p, "unhandled exception vector ");
    p = console::append_uint(p, vector);
    *p = '\0';
    KPANIC(line);
}

extern "C" const uint8_t vector_table[]; // vectors.S

namespace {

// AArch64 always forces SPSel=1 (switches to SP_ELx, i.e. SP_EL1 for an
// EL1 target) on taking ANY exception, regardless of which SP was in use
// by whatever got interrupted -- this is fixed by the architecture, not
// a choice made here. This kernel's threads all run with SPSel=0 (SP_EL0;
// that's what context_switch.S's plain `mov sp, ...` has been
// manipulating all along), so SP_EL1 is otherwise never touched and sits
// at its boot-time value of 0 -- exactly what sync_elx's very first
// instruction (vectors.S's `stp x0, x1, [sp, #-272]!`) would fault on
// itself if nothing set it up first. Each core gets its own small,
// static (not heap-allocated -- this runs before heap::init() would be
// convenient to depend on, and doesn't need to be anyway) exception
// stack, used only transiently while handling one exception at a time --
// it's never a thread's "real" stack, so a few KiB is plenty.
constexpr size_t kExceptionStackSize = 4096;
alignas(16) uint8_t g_exception_stacks[percore::kMaxCores][kExceptionStackSize];

} // namespace

namespace exceptions {

void install_vectors() {
    asm volatile("msr vbar_el1, %0" ::"r"(reinterpret_cast<uint64_t>(vector_table)) : "memory");

    uint32_t core = smp::me();
    ASSERT(core < percore::kMaxCores);
    uint64_t stack_top = reinterpret_cast<uint64_t>(g_exception_stacks[core] + kExceptionStackSize);

    // SP_EL1 can't be written by name directly from EL1 -- "MSR SP_EL1,
    // Xt" is only valid from EL2 and above; from EL1 itself, the only way
    // to reach "the other" banked SP is the standard trick below: flip
    // SPSel to 1 so plain "sp" now aliases SP_EL1 (instead of this
    // kernel's usual SP_EL0), write it with an ordinary "mov sp, ...",
    // then flip SPSel back to 0. This all has to be one atomic asm block
    // with nothing else in between: while SPSel=1, "sp" is NOT this
    // function's own real stack, so the compiler must not be given the
    // chance to spill/read anything stack-relative until SPSel is back
    // to 0 -- which, inside a single asm volatile string, it can't.
    asm volatile(
        "msr spsel, #1\n"
        "mov sp, %0\n"
        "msr spsel, #0\n"
        "isb\n" ::"r"(stack_top)
        : "memory");
}

} // namespace exceptions
