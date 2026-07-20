#pragma once

#include "Epidemic/Runtime/World/chunk.h"
#include "Epidemic/Runtime/World/object_materialization.h"
#include "Epidemic/Runtime/World/region.h"
#include "Epidemic/Runtime/World/world_object_registry.h"
#include "Epidemic/Runtime/World/world_query.h"
#include "Epidemic/Runtime/World/world_state.h"

#include <unordered_map>

namespace epidemic::runtime
{
class WorldRuntime : public IRegionRegistry, public IChunkRegistry, public IWorldObjectRegistry, public IObjectMaterializer, public IDemotionCommitAuthority, public IWorldQuery
{
  public:
    [[nodiscard]] foundation::Result<void> RegisterRegion(RegionDescriptor region) override;
    [[nodiscard]] std::optional<RegionDescriptor> FindRegion(RegionId id) const override;

    [[nodiscard]] foundation::Result<void> RegisterChunk(ChunkDescriptor chunk) override;
    [[nodiscard]] std::optional<ChunkDescriptor> FindChunk(ChunkId id) const override;
    [[nodiscard]] foundation::Result<ChunkSnapshot> GetChunkSnapshot(ChunkId id) const override;
    [[nodiscard]] foundation::Result<void> SetChunkState(ChangeChunkStateCommand command) override;

    [[nodiscard]] foundation::Result<WorldCommandResult> Apply(const CreateObjectCommand& command) override;
    [[nodiscard]] foundation::Result<WorldCommandResult> Apply(const ChangePlacementCommand& command) override;
    [[nodiscard]] foundation::Result<WorldCommandResult> Apply(const ChangeResidencyCommand& command) override;
    [[nodiscard]] foundation::Result<WorldCommandResult> Apply(const PromotePersistenceTierCommand& command) override;
    [[nodiscard]] foundation::Result<WorldCommandResult> Apply(const MaterializeObjectCommand& command);
    [[nodiscard]] foundation::Result<WorldCommandResult> Apply(const DemoteObjectCommand& command);
    [[nodiscard]] foundation::Result<WorldCommandResult> Apply(const DestroyObjectCommand& command) override;

    [[nodiscard]] foundation::Result<RuntimeObjectId> CreateObject(WorldObjectRecord record);
    [[nodiscard]] foundation::Result<void> DestroyObject(RuntimeObjectId id);
    [[nodiscard]] std::optional<WorldObjectRecord> FindObject(RuntimeObjectId id) const override;
    [[nodiscard]] std::vector<WorldObjectRecord> FindObjectsInRegion(RegionId region) const override;
    [[nodiscard]] std::vector<WorldObjectRecord> FindObjectsInChunk(ChunkId chunk) const override;
    [[nodiscard]] std::vector<WorldObjectRecord> FindObjectsByReality(ObjectRealityLevel reality) const override;
    [[nodiscard]] foundation::Result<void> SetPlacement(RuntimeObjectId id, ObjectPlacement placement);
    [[nodiscard]] foundation::Result<void> SetResidency(RuntimeObjectId id, ResidencyState state);

    [[nodiscard]] foundation::Result<RuntimeObjectId> Materialize(const MaterializationRequest& request) override;
    [[nodiscard]] foundation::Result<void> Demote(const DemotionRequest& request) override;
    [[nodiscard]] foundation::Result<DemotionCommitToken> IssueDemotionCommitToken(DemotionSnapshot snapshot) override;
    [[nodiscard]] foundation::Result<void> RevokeDemotionCommitToken(std::uint64_t token_id) override;

    foundation::Result<void> SetChunkState(ChunkId id, ChunkState state);

  protected:
    struct ChunkRecord
    {
        ChunkDescriptor descriptor{};
        ChunkState state = ChunkState::Unloaded;
        std::uint64_t revision = 1;
    };

    std::unordered_map<RegionId, RegionDescriptor> regions_;
    std::unordered_map<ChunkId, ChunkRecord> chunks_;
    std::unordered_map<RuntimeObjectId, WorldObjectRecord> world_objects_;
    std::unordered_map<PersistentObjectId, RuntimeObjectId> persistent_to_runtime_;
    std::unordered_map<std::uint64_t, DemotionCommitToken> issued_demotion_tokens_;
    std::uint64_t next_runtime_object_value_ = 1;
    std::uint64_t next_demotion_token_value_ = 1;

  private:
    void InvalidateDemotionTokensForObject(RuntimeObjectId object);
};
} 
