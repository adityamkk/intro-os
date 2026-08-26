#include <cstdint>

#include <limine.h>

#include "console.hpp"
#include "heap.hpp"
#include "mmu.hpp"
#include "percore.hpp"
#include "smp.hpp"
#include "threads.hpp"

extern "C" {

// Tell Limine we speak base revision 6, the latest at time of writing.
__attribute__((used, section(".limine_requests")))
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

// Multiprocessor: hands us the list of cores and a way to start them.
__attribute__((used, section(".limine_requests")))
static volatile limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST_ID,
    .revision = 0,
    .response = nullptr,
    .flags = 0,
};

// Executable Address: tells us where our own image is loaded in physical
// memory, which we need to point TTBR0_EL1 at a page table of ours (see
// mmu.cpp) -- Limine's own mappings don't cover device MMIO like the UART.
__attribute__((used, section(".limine_requests")))
static volatile limine_executable_address_request executable_address_request = {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST_ID,
    .revision = 0,
    .response = nullptr,
};

__attribute__((used, section(".limine_requests_start")))
static volatile uint64_t limine_requests_start_marker[] = LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end")))
static volatile uint64_t limine_requests_end_marker[] = LIMINE_REQUESTS_END_MARKER;

} // extern "C"

namespace {

[[noreturn]] void hang() {
    for (;;) {
        asm volatile("wfi");
    }
}

} // namespace

// The kernel's actual test/workload entry point: kmain() schedules this
// (see the bottom of kmain() below) once boot is done. A specific
// tests/*.cpp translation unit is expected to define its own strong,
// extern "C" `main()`, which overrides this weak default at link time --
// see the Makefile's test harness. Plain `make`/`make run` (no test
// linked in) falls back to this, which does nothing at all -- go()
// stops the thread automatically once main() returns.
extern "C" __attribute__((weak)) void main() {}

extern "C" [[noreturn]] void kmain() {
    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        hang();
    }

    if (mp_request.response == nullptr || executable_address_request.response == nullptr) {
        hang();
    }

    mmu::init(executable_address_request.response->physical_base,
              executable_address_request.response->virtual_base);
    mmu::activate_this_core();

    // Must run before smp::init() releases the other cores: heap::init()
    // isn't safe to race against a concurrent malloc()/free().
    heap::init();

    // Also must run before release: builds every core's idle TCB up
    // front so no core needs to synchronize on percore::g_table just to
    // find its own entry.
    percore::init();

    smp::init(const_cast<limine_mp_response *>(mp_request.response));
    console::print("\n");

    // The firmware/bootloader may leave the cursor mid-line (e.g. after a
    // carriage-return-terminated progress message), so force a fresh line
    // before our own output.
    console::print("Hello, World!\n");

    char line[64];
    char *p = line;
    p = console::append(p, "Booted on core ");
    p = console::append_uint(p, smp::me());
    p = console::append(p, " of ");
    p = console::append_uint(p, mp_request.response->cpu_count);
    p = console::append(p, " total.\n");
    *p = '\0';
    console::print(line);

    threads::go(main);
    threads::stop();
}
