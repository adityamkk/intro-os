#pragma once

// AArch64 exception vectors and the page-fault handler they lead to.
// Nothing in this kernel used exceptions before this -- everything was
// purely cooperative (explicit context_switch() calls only). The one
// exception this kernel actually expects and handles is a Data Abort
// taken from EL1 on itself (a thread touching an address its own VME
// list covers but hasn't been paged in yet); every other vector exists
// only to fail loudly -- KPANIC with a clear message -- rather than
// silently corrupting state if something unexpected reaches it.
namespace exceptions {

// Points this core's VBAR_EL1 at the vector table (vectors.S). Per-core,
// like MAIR_EL1/TCR_EL1/TTBR0_EL1 -- called from kmain() and ap_entry(),
// right after each one's own mmu::activate_this_core() call.
void install_vectors();

} // namespace exceptions
