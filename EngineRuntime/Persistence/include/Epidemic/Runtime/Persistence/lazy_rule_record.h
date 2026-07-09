#pragma once

#include "Epidemic/Runtime/Foundation/runtime_ids.h"

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
    PersistentObjectId target_id{};
    LazyRuleKind kind = LazyRuleKind::Decay;
    LazyRuleState state = LazyRuleState::Pending;
    std::uint64_t created_game_time = 0;
    std::uint64_t evaluate_after_game_time = 0;
    std::uint64_t rule_seed = 0;
};
} // namespace epidemic::runtime
