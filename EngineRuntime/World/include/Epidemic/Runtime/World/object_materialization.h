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
    std::uint64_t token_id = 0;
    RuntimeObjectId object{};
    ObjectRealityLevel target_reality = ObjectRealityLevel::Logical;
    ObjectPlacement collapsed_placement{HiddenPlacement{}};
    foundation::StringId collapse_record_id{};
    std::uint64_t source_revision = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return token_id != 0 && object.IsValid() && collapse_record_id.IsValid() && source_revision != 0;
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

class IDemotionCommitAuthority
{
  public:
    virtual ~IDemotionCommitAuthority() = default;

    [[nodiscard]] virtual foundation::Result<DemotionCommitToken> IssueDemotionCommitToken(DemotionSnapshot snapshot) = 0;
    [[nodiscard]] virtual foundation::Result<void> RevokeDemotionCommitToken(std::uint64_t token_id) = 0;
};
} 
