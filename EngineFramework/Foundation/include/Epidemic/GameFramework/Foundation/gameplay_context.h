#pragma once

#include "Epidemic/GameFramework/Foundation/ids.h"
#include "Epidemic/GameFramework/Foundation/time_types.h"

namespace epidemic::gameplay
{
struct GameplayContext
{
    GameplayTickId tick{};
    GameplayTimePoint time{};
    OperationId operation{};
    CorrelationId correlation{};
    GameplayObjectRef actor{};
    GameplayObjectRef instigator{};
    GameplayObjectRef source{};
};
} // namespace epidemic::gameplay
