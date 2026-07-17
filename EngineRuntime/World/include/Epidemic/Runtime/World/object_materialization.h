#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/Foundation/string_id.h"
#include "Epidemic/Runtime/World/object_placement.h"
#include "Epidemic/Runtime/Foundation/runtime_ids.h"
#include "Epidemic/Runtime/Foundation/runtime_states.h"

#include <cstdint>

namespace epidemic::runtime
{
enum class MaterializationState
{
    NotMaterialized,
    Requested,
    Materializing,
    Materialized,
    Demoting,
    Demoted,
    Failed,
};

struct MaterializationRequest
{
    PersistentObjectId persistent_id{};
    ObjectRealityLevel target_reality = ObjectRealityLevel::Physical;

    [[nodiscard]] constexpr bool operator==(const MaterializationRequest&) const noexcept = default;
};

struct DemotionCommitToken
{
    RuntimeObjectId object{};
    ObjectRealityLevel target_reality = ObjectRealityLevel::Logical;
    ObjectPlacement collapsed_placement{HiddenPlacement{}};
    foundation::StringId collapse_record_id{};
    std::uint64_t source_revision = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return object.IsValid() && collapse_record_id.IsValid() && source_revision != 0;
    }

    [[nodiscard]] constexpr bool operator==(const DemotionCommitToken&) const noexcept = default;
};

struct DemotionSnapshot
{
    RuntimeObjectId object{};
    ObjectRealityLevel target_reality = ObjectRealityLevel::Logical;
    ObjectPlacement collapsed_placement{HiddenPlacement{}};
    foundation::StringId collapse_record_id{};
    std::uint64_t source_revision = 0;

    [[nodiscard]] constexpr bool operator==(const DemotionSnapshot&) const noexcept = default;
};

struct DemotionRequest
{
    RuntimeObjectId runtime_id{};
    ObjectRealityLevel target_reality = ObjectRealityLevel::Logical;
    DemotionCommitToken commit_token{};

    [[nodiscard]] constexpr bool operator==(const DemotionRequest&) const noexcept = default;
};

class IObjectMaterializer
{
  public:
    virtual ~IObjectMaterializer() = default;

    [[nodiscard]] virtual foundation::Result<RuntimeObjectId> Materialize(const MaterializationRequest& request) = 0;
    [[nodiscard]] virtual foundation::Result<void> Demote(const DemotionRequest& request) = 0;
};
} 
