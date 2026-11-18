#include "hhdm.hpp"

#include "panic.hpp"

namespace {

uint64_t g_offset = 0;
bool g_initialized = false;

} // namespace

namespace hhdm {

void init(uint64_t offset) {
    g_offset = offset;
    g_initialized = true;
}

uint64_t offset() {
    ASSERT(g_initialized);
    return g_offset;
}

} // namespace hhdm
