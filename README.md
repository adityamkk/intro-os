# intro-os

A minimal "Hello, World!" kernel for aarch64, booted by [Limine](https://limine-bootloader.org/)
and run on QEMU.

## Features

- `console::print()` (`kernel/src/console.cpp`) -- writes to the console via
  the PL011 UART QEMU's `virt` machine exposes.
- `smp::me()` (`kernel/src/smp.cpp`) -- returns the calling core's id.
  Bringup of secondary cores uses Limine's MP feature; each core prints its
  own greeting once it comes online.
- `mmu.cpp` -- a small TTBR0_EL1 identity map for the low 1GiB of physical
  memory. Limine's own mappings don't cover device MMIO (like the UART), so
  each core builds/loads this itself before touching the console. See the
  comments in `kernel/src/mmu.cpp` for why.
- `heap::malloc()`/`heap::free()` (`kernel/src/heap.cpp`) -- a first-fit,
  coalescing allocator backing global `new`/`delete`.
- `threads::go()`/`threads::stop()` (`kernel/src/threads.cpp`) -- a
  cooperative thread/scheduler layer: `context_switch()`
  (`kernel/src/context_switch.S`) switches stacks, `scheduler.cpp` is a
  spinlock-guarded FIFO ready queue, and each core (`percore.hpp`) falls
  back to its own idle thread whenever that queue is empty.
- `ASSERT()`/`KPANIC()` (`kernel/include/panic.hpp`) -- halts with a
  message identifying the failed invariant or unreachable code path.

## Building and running

```sh
make deps   # fetch the cross-compiler, QEMU, Limine and OVMF firmware (once)
make        # build kernel/kernel.elf and an ISO
make run    # boot it in QEMU (Ctrl-A X to quit)
```

`make deps` needs no root: it fetches package archives with `apt-get
download` (which only downloads, never installs) and unpacks them with
`dpkg-deb -x` into `./toolchain`, alongside a Limine release and OVMF's
aarch64 firmware. Nothing is installed system-wide.

Useful variables: `SMP=n make run` (core count, default 4), `MEM=... make
run` (default 256M).

`make clean` removes build output; `make distclean` also removes the
vendored toolchain.

## Testing

Each file in `tests/` is a standalone testcase: it defines its own
`extern "C" void main()` (see `tests/test_util.hpp`), which `kmain()`
schedules and runs as a real thread, in place of the kernel's normal
boot-only behavior. A test reports its result by calling exactly one of:

- `test::pass()` -- prints `TEST PASS` and stops the thread.
- `test::fail("reason")` -- prints `TEST FAIL: reason` and stops the thread.

```sh
make test              # build, boot, and check every test in tests/
make test heap         # just tests/heap.cpp
make test heap hello   # multiple tests by name
```

Under the hood, each test is linked into its own kernel ELF -- the exact
same `kernel/src/*.cpp` objects as the normal build, plus that one test's
object, whose `main()` overrides the kernel's weak default -- booted under
QEMU with a timeout (`TEST_TIMEOUT=n make test ...`, default 10s), with its
console output grepped for `TEST PASS`. A test that panics, calls
`test::fail()`, hangs, or exceeds the timeout is reported as a failure,
with its full console output printed for debugging.

To add a test, drop a new `tests/<name>.cpp` defining `main()` --
`make test` picks it up automatically, no other wiring needed. See the
existing tests for examples: `hello.cpp` is a minimal smoke test,
`heap.cpp` exercises the allocator, and `multithreading.cpp` exercises
concurrent `threads::go()`/`context_switch()` across cores.
