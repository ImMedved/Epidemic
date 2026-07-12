#pragma once

#include "Epidemic/Foundation/result.h"

// File note:
// Header for runtime contracts or module-local helpers. Comments document how each
// function participates in the module API and what state it observes or mutates.

namespace epidemic::runtime
{
template <typename TValue> using ResourceResult = foundation::Result<TValue>;
}
