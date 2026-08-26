#pragma once

#include <cstddef>

// Freestanding kernels need to provide these themselves: the compiler is
// free to lower struct/array zeroing, copies, and comparisons down to
// calls to these exact symbols regardless of whether any C++ code calls
// them directly (as happened for a large aggregate zero-init), and there's
// no libc linked in (-nostdlib) to supply them.
extern "C" {

void *memset(void *dst, int value, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);

} // extern "C"
