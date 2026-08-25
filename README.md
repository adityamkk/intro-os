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
