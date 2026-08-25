#pragma once

#include <cstdint>

struct limine_mp_response;

// Brings up secondary cores using Limine's MP (multiprocessor) feature and
// exposes each core's identity to itself.
namespace smp {

// Called once from the bootstrap processor with the Limine MP response.
// Assigns every core (including the caller) an id in [0, cpu_count) and
// releases the secondary cores to run ap_main.
void init(limine_mp_response *mp);

// Returns the id (0-based) of the core this is called on. Valid on any
// core after it has been released by init().
uint32_t me();

} // namespace smp
