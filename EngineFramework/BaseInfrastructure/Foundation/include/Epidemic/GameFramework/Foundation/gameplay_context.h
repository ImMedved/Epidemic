#pragma once

#include "Epidemic/GameFramework/Foundation/ids.h"
#include "Epidemic/GameFramework/Foundation/time_types.h"

namespace epidemic::gameplay
{
struct GameplayContext
{
    GameplayTickId tick{};
    GameplayTimePoint time{};

    // Unique identity of this operation and the wider causal chain it belongs to.
    OperationId operation{};
    CorrelationId correlation{};

    // Actor performs this operation; instigator is the original responsible subject; source is the immediate effect source.
    GameplayObjectRef actor{};
    GameplayObjectRef instigator{};
    GameplayObjectRef source{};

    // Immediate predecessor, if this operation was caused by another operation or event.
    OperationId parent_operation{};
    EventId cause_event{};
};
} // namespace epidemic::gameplay