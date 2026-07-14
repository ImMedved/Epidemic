#pragma once

#include "Epidemic/Foundation/string_id.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Foundation/runtime_time.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace epidemic::runtime
{
struct TombstoneRecord
{
    PersistentObjectId persistent_id{};
    GameTimePoint deleted_game_time{};
    foundation::StringId reason{};
    std::uint64_t revision = 0;
};

class ITombstoneStore
{
  public:
    virtual ~ITombstoneStore() = default;

    [[nodiscard]] virtual std::optional<TombstoneRecord> FindTombstone(PersistentObjectId id) const = 0;
    [[nodiscard]] virtual bool IsTombstoned(PersistentObjectId id) const = 0;
    [[nodiscard]] virtual std::vector<TombstoneRecord> ListTombstones() const = 0;
};
} // namespace epidemic::runtime
