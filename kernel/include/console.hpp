#pragma once

#include <cstdint>

// Minimal console driver on top of the PL011 UART exposed by QEMU's "virt"
// machine, reached through the identity map mmu::identity_map_low_1gib()
// sets up (Limine's own mappings don't cover device MMIO).
namespace console {

// Callers must not print before mmu::identity_map_low_1gib() has run, since
// that's what makes the UART's physical address dereferenceable.

// Writes a NUL-terminated string to the console. Safe to call concurrently
// from multiple cores. Each call is atomic with respect to other calls, but
// a multi-part message built from several print()/print_uint() calls can
// still interleave with another core's -- use append()/append_uint() to
// build such a message in a buffer and print() it in one call instead.
void print(const char *str);

// Writes an unsigned integer in decimal to the console.
void print_uint(uint64_t value);

// Appends `str` at `dst`, returning the position just past what was
// written (not NUL-terminated). Caller must ensure enough room.
char *append(char *dst, const char *str);

// Appends the decimal digits of `value` at `dst`, returning the position
// just past what was written (not NUL-terminated).
char *append_uint(char *dst, uint64_t value);

} // namespace console
