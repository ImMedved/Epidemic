#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Foundation/runtime_time.h"

#include <cstdint>

namespace epidemic::runtime
{
enum class LazyRuleKind
{
    Decay,
    Theft,
    Cleanup,
};

enum class LazyRuleState
{
    Pending,
    Evaluated,
    Applied,
    Cancelled,
    Expired,
};

struct LazyRuleRecord
{
    LazyRuleId rule_id{};
    PersistentObjectId target_id{};
    LazyRuleKind kind = LazyRuleKind::Decay;
    LazyRuleState state = LazyRuleState::Pending;
    GameTimePoint created_game_time{};
    GameTimePoint evaluate_after_game_time{};
    std::uint64_t rule_seed = 0;
    std::uint64_t revision = 0;
};
} // namespace epidemic::runtime
