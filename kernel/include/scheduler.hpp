#pragma once

#include "tcb.hpp"

namespace scheduler {
    void schedule(TCB* t);

    TCB* next();
} // namespace scheduler