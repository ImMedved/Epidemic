#pragma once

#include "Epidemic/Foundation/string_id.h"
#include "Epidemic/Runtime/Foundation/runtime_time.h"
#include "Epidemic/Runtime/World/object_materialization.h"
#include "Epidemic/Runtime/World/world_object.h"

#include <cstdint>
#include <optional>

namespace epidemic::runtime
{
struct WorldCommandResult
{
    RuntimeObjectId runtime_id{};
    std::optional<WorldObjectSnapshot> before;
    std::optional<WorldObjectSnapshot> after;
};

struct CreateObjectCommand
{
    WorldObjectRecord record{};
};

struct ChangePlacementCommand
{
    RuntimeObjectId runtime_id{};
    std::uint64_t expected_revision = 0;
    ObjectPlacement placement{HiddenPlacement{}};
};

struct ChangeResidencyCommand
{
    RuntimeObjectId runtime_id{};
    std::uint64_t expected_revision = 0;
    ResidencyState residency = ResidencyState::Unloaded;
};

struct PromotePersistenceTierCommand
{
    RuntimeObjectId runtime_id{};
    std::uint64_t expected_revision = 0;
    PersistenceTier tier = PersistenceTier::Disposable;
};

struct MaterializeObjectCommand
{
    MaterializationRequest request{};
    std::uint64_t expected_revision = 0;
};

struct DemoteObjectCommand
{
    DemotionRequest request{};
    std::uint64_t expected_revision = 0;
};

struct DestroyObjectCommand
{
    RuntimeObjectId runtime_id{};
    std::uint64_t expected_revision = 0;
    GameTimePoint destroyed_at{};
    foundation::StringId reason{};
};
} // namespace epidemic::runtime
