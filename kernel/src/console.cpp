#include "console.hpp"

namespace {

// QEMU's "virt" machine maps the first PL011 UART at this physical address.
constexpr uint64_t kUartPhysBase = 0x09000000;

// PL011 register offsets we need.
constexpr uint32_t kDataReg = 0x00; // UARTDR
constexpr uint32_t kFlagReg = 0x18; // UARTFR
constexpr uint32_t kFlagTxFifoFull = 1u << 5; // TXFF

// Valid only after mmu::activate_this_core() has run on the calling core:
// low physical addresses are then identity-mapped, so this pointer works.
volatile uint8_t *g_uart_base = reinterpret_cast<volatile uint8_t *>(kUartPhysBase);

// Guards interleaved writes when multiple cores call print() at once.
volatile int g_lock = 0;

void lock() {
    while (__atomic_test_and_set(&g_lock, __ATOMIC_ACQUIRE)) {
        asm volatile("yield" ::: "memory");
    }
}

void unlock() { __atomic_clear(&g_lock, __ATOMIC_RELEASE); }

void putc_raw(char c) {
    while (*(g_uart_base + kFlagReg) & kFlagTxFifoFull) {
        asm volatile("yield" ::: "memory");
    }
    *(g_uart_base + kDataReg) = static_cast<uint8_t>(c);
}

} // namespace

namespace console {

void print(const char *str) {
    lock();
    while (*str) {
        if (*str == '\n') {
            putc_raw('\r');
        }
        putc_raw(*str++);
    }
    unlock();
}

void print_uint(uint64_t value) {
    char buf[21]; // max digits of a 64-bit value, plus NUL
    *append_uint(buf, value) = '\0';
    print(buf);
}

char *append(char *dst, const char *str) {
    while (*str) {
        *dst++ = *str++;
    }
    return dst;
}

char *append_uint(char *dst, uint64_t value) {
    char digits[20]; // max digits of a 64-bit value
    int n = 0;

    if (value == 0) {
        digits[n++] = '0';
    } else {
        while (value > 0) {
            digits[n++] = static_cast<char>('0' + (value % 10));
            value /= 10;
        }
    }

    while (n > 0) {
        *dst++ = digits[--n];
    }
    return dst;
}

} // namespace console
