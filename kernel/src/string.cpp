#include "string.hpp"

void *memset(void *dst, int value, size_t n) {
    auto *d = static_cast<unsigned char *>(dst);
    for (size_t i = 0; i < n; i++) {
        d[i] = static_cast<unsigned char>(value);
    }
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    auto *d = static_cast<unsigned char *>(dst);
    const auto *s = static_cast<const unsigned char *>(src);
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    auto *d = static_cast<unsigned char *>(dst);
    const auto *s = static_cast<const unsigned char *>(src);
    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = n; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const auto *pa = static_cast<const unsigned char *>(a);
    const auto *pb = static_cast<const unsigned char *>(b);
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) {
            return static_cast<int>(pa[i]) - static_cast<int>(pb[i]);
        }
    }
    return 0;
}
