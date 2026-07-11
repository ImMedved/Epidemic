#pragma once

#include "Epidemic/Foundation/result.h"

namespace epidemic::runtime
{
template <typename TValue> using ResourceResult = foundation::Result<TValue>;
}
