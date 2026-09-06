#pragma once
#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::savegame
{
struct SaveParticipantId
{
    TypeId value{};
    static constexpr SaveParticipantId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const SaveParticipantId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const SaveParticipantId &) const noexcept = default;
};
struct IdHash
{
    [[nodiscard]] std::size_t operator()(SaveParticipantId id) const noexcept
    {
        return std::hash<TypeId>{}(id.value);
    }
};
using SaveSchemaVersion = std::uint32_t;
using SaveGameVersion = std::uint32_t;
using DataHash = std::uint64_t;

enum class ContentCompatibilityPolicy
{
    // Requires exact save/content/engine/framework metadata, participant set and schemas.
    ExactRequired,
    // Requires the same content version and a save produced by no newer engine/framework.
    // Participant schema migrations are allowed; unknown optional save participants are ignored.
    CompatibleVersion,
    // Allows an older content version and requires every non-current participant schema to
    // reach the registered schema through an explicit migration chain.
    MigrationRequired,
    // Ignores content/engine/framework metadata mismatches. Required participants still must
    // restore successfully; optional participants may be absent or skipped after a recoverable
    // migration/validation/staging failure.
    BestEffort,
    // Development-only structural restore. Unknown save participants may be ignored regardless
    // of their old requirement marker, but every currently registered required participant must
    // still be present and valid.
    DeveloperMode
};
enum class SaveParticipantRequirement
{
    Required,
    Optional
};
enum class SavePhase
{
    Idle,
    Barrier,
    Capturing,
    Validating,
    Staging,
    Committing,
    Complete,
    Failed
};
struct SaveSection
{
    SaveParticipantId participant{};
    SaveSchemaVersion schema_version = 0;
    std::vector<std::byte> payload;
    DataHash payload_hash = 0;
};
struct SaveParticipantManifest
{
    SaveParticipantId id{};
    SaveSchemaVersion schema_version = 0;
    DataHash payload_hash = 0;
    SaveParticipantRequirement requirement = SaveParticipantRequirement::Required;
    std::vector<SaveParticipantId> dependencies;
};
struct SaveGameManifest
{
    SaveGameVersion save_version = 1;
    std::uint64_t content_version = 0;
    DataHash content_hash = 0;
    std::uint64_t engine_version = 0;
    std::uint64_t framework_version = 0;
    GameplayTimePoint saved_at{};
    std::vector<SaveParticipantManifest> participants;
};
struct SaveGameImage
{
    SaveGameManifest manifest;
    std::vector<SaveSection> sections;
};
struct SaveContext
{
    GameplayTimePoint now{};
    std::uint64_t content_version = 0;
    DataHash content_hash = 0;
    std::uint64_t engine_version = 0;
    std::uint64_t framework_version = 0;
};
struct RestoreContext
{
    ContentCompatibilityPolicy compatibility = ContentCompatibilityPolicy::ExactRequired;
    std::uint64_t current_content_version = 0;
    DataHash current_content_hash = 0;
    std::uint64_t current_engine_version = 0;
    std::uint64_t current_framework_version = 0;
    bool suppress_gameplay_events = true;
};
struct SaveFormatSupport
{
    SaveGameVersion current = 1;
    SaveGameVersion minimum_supported = 1;
    SaveGameVersion maximum_supported = 1;
};
struct SaveResourceLimits
{
    std::size_t max_participants = 1024;
    std::size_t max_section_bytes = 64ull * 1024ull * 1024ull;
    std::size_t max_total_payload_bytes = 512ull * 1024ull * 1024ull;
    std::size_t max_dependencies_per_participant = 256;
};
class ISaveBarrierLease
{
  public:
    virtual ~ISaveBarrierLease() = default;
};
class ISaveBarrier
{
  public:
    virtual ~ISaveBarrier() = default;
    [[nodiscard]] virtual foundation::Result<std::unique_ptr<ISaveBarrierLease>> AcquireCaptureLease() = 0;
    [[nodiscard]] virtual foundation::Result<std::unique_ptr<ISaveBarrierLease>> AcquireRestoreLease() = 0;
};
class IRestoreStage
{
  public:
    virtual ~IRestoreStage() = default;
};
class ISaveParticipant
{
  public:
    virtual ~ISaveParticipant() = default;
    [[nodiscard]] virtual SaveParticipantId Id() const noexcept = 0;
    [[nodiscard]] virtual SaveSchemaVersion SchemaVersion() const noexcept = 0;
    [[nodiscard]] virtual SaveParticipantRequirement Requirement() const noexcept
    {
        return SaveParticipantRequirement::Required;
    }
    [[nodiscard]] virtual std::vector<SaveParticipantId> Dependencies() const = 0;
    [[nodiscard]] virtual foundation::Result<SaveSection> CaptureSnapshot(const SaveContext &) const = 0;
    [[nodiscard]] virtual foundation::Result<void> ValidateSnapshot(const SaveSection &,
                                                                    const RestoreContext &) const = 0;
    [[nodiscard]] virtual foundation::Result<std::unique_ptr<IRestoreStage>> StageRestore(const SaveSection &,
                                                                                          const RestoreContext &) = 0;
    // StageRestore must perform every operation that can fail. CommitRestore is the final
    // allocation-free/noexcept publication of the already validated state.
    virtual void CommitRestore(IRestoreStage &) noexcept = 0;
};
class ISaveMigration
{
  public:
    virtual ~ISaveMigration() = default;
    [[nodiscard]] virtual SaveParticipantId Participant() const noexcept = 0;
    [[nodiscard]] virtual SaveSchemaVersion FromVersion() const noexcept = 0;
    [[nodiscard]] virtual SaveSchemaVersion ToVersion() const noexcept = 0;
    [[nodiscard]] virtual foundation::Result<SaveSection> Migrate(const SaveSection &) const = 0;
};
struct SaveGameDiagnostics
{
    SavePhase phase = SavePhase::Idle;
    SavePhase failure_phase = SavePhase::Idle;
    SaveParticipantId last_failed_participant{};
    SaveSchemaVersion last_failed_schema_version = 0;
    std::uint64_t captures = 0;
    std::uint64_t restores = 0;
    std::uint64_t migrations = 0;
    std::uint64_t validation_failures = 0;
    std::uint64_t optional_participants_skipped = 0;
};
class SaveGameOrchestrator
{
  public:
    explicit SaveGameOrchestrator(SaveFormatSupport format = {}, SaveResourceLimits limits = {}) noexcept
        : format_(format), limits_(limits)
    {
    }
    [[nodiscard]] foundation::Result<void> SetBarrier(ISaveBarrier &barrier) noexcept;
    [[nodiscard]] foundation::Result<void> RegisterParticipant(ISaveParticipant &participant);
    [[nodiscard]] foundation::Result<void> RegisterMigration(const ISaveMigration &migration);
    [[nodiscard]] foundation::Result<void> FreezeRegistry();
    [[nodiscard]] bool RegistryFrozen() const noexcept
    {
        return registry_frozen_;
    }
    [[nodiscard]] foundation::Result<std::vector<SaveParticipantId>> ResolveOrder() const;
    [[nodiscard]] foundation::Result<SaveGameImage> Capture(const SaveContext &context);
    [[nodiscard]] foundation::Result<void> Restore(SaveGameImage image, const RestoreContext &context);
    [[nodiscard]] SaveGameDiagnostics GetDiagnostics() const noexcept
    {
        return diagnostics_;
    }
    [[nodiscard]] static DataHash HashBytes(std::span<const std::byte> bytes) noexcept;

  private:
    struct ParticipantRegistration
    {
        ISaveParticipant *participant = nullptr;
        SaveSchemaVersion schema_version = 0;
        SaveParticipantRequirement requirement = SaveParticipantRequirement::Required;
        std::vector<SaveParticipantId> dependencies;
    };
    struct MigrationKey
    {
        SaveParticipantId participant{};
        SaveSchemaVersion from = 0;
        [[nodiscard]] bool operator==(const MigrationKey &) const noexcept = default;
    };
    struct MigrationKeyHash
    {
        [[nodiscard]] std::size_t operator()(const MigrationKey &k) const noexcept
        {
            return IdHash{}(k.participant) ^ (std::hash<std::uint32_t>{}(k.from) << 1u);
        }
    };
    [[nodiscard]] foundation::Result<void> EnsureFrozen();
    [[nodiscard]] foundation::Result<void> ValidateFormatConfiguration() const;
    [[nodiscard]] foundation::Result<void> ValidateImageStructure(const SaveGameImage &image,
                                                                   const RestoreContext &context) const;
    [[nodiscard]] foundation::Result<void> ValidateCompatibility(const SaveGameImage &image,
                                                                  const RestoreContext &context) const;
    [[nodiscard]] foundation::Result<SaveSection> MigrateToCurrent(SaveSection section,
                                                                   const ParticipantRegistration &participant,
                                                                   ContentCompatibilityPolicy policy);
    void MarkFailure(SavePhase phase, SaveParticipantId participant = {}, SaveSchemaVersion schema = 0) noexcept;

    std::unordered_map<SaveParticipantId, ParticipantRegistration, IdHash> participants_;
    std::unordered_map<MigrationKey, const ISaveMigration *, MigrationKeyHash> migrations_;
    ISaveBarrier *barrier_ = nullptr;
    SaveFormatSupport format_{};
    SaveResourceLimits limits_{};
    bool registry_frozen_ = false;
    SaveGameDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::savegame
