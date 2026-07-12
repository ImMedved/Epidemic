#pragma once

#include "Epidemic/Foundation/handle.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

namespace epidemic::runtime
{
template <typename Tag> using RuntimeHandle = foundation::Handle<Tag>;
} // namespace epidemic::runtime
