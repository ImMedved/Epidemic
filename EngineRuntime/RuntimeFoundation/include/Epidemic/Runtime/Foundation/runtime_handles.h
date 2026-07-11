#pragma once

#include "Epidemic/Foundation/handle.h"

namespace epidemic::runtime
{
template <typename Tag> using RuntimeHandle = foundation::Handle<Tag>;
} // namespace epidemic::runtime
