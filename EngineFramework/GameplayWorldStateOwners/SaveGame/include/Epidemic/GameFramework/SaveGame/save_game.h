#pragma once
#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
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
    ExactRequired,
    CompatibleVersion,
    MigrationRequired,
    BestEffort,
    DeveloperMode
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
    bool suppress_gameplay_events = true;
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
    [[nodiscard]] virtual std::vector<SaveParticipantId> Dependencies() const = 0;
    [[nodiscard]] virtual foundation::Result<SaveSection> CaptureSnapshot(const SaveContext &) const = 0;
    [[nodiscard]] virtual foundation::Result<void> ValidateSnapshot(const SaveSection &,
                                                                    const RestoreContext &) const = 0;
    [[nodiscard]] virtual foundation::Result<std::unique_ptr<IRestoreStage>> StageRestore(const SaveSection &,
                                                                                          const RestoreContext &) = 0;
    [[nodiscard]] virtual foundation::Result<void> CommitRestore(IRestoreStage &) = 0;
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
    std::uint64_t captures = 0;
    std::uint64_t restores = 0;
    std::uint64_t migrations = 0;
    std::uint64_t validation_failures = 0;
};
class SaveGameOrchestrator
{
  public:
    [[nodiscard]] foundation::Result<void> RegisterParticipant(ISaveParticipant &participant);
    [[nodiscard]] foundation::Result<void> RegisterMigration(const ISaveMigration &migration);
    [[nodiscard]] foundation::Result<std::vector<SaveParticipantId>> ResolveOrder() const;
    [[nodiscard]] foundation::Result<SaveGameImage> Capture(const SaveContext &context);
    [[nodiscard]] foundation::Result<void> Restore(SaveGameImage image, const RestoreContext &context);
    [[nodiscard]] SaveGameDiagnostics GetDiagnostics() const noexcept
    {
        return diagnostics_;
    }
    [[nodiscard]] static DataHash HashBytes(std::span<const std::byte> bytes) noexcept;

  private:
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
    [[nodiscard]] foundation::Result<SaveSection> MigrateToCurrent(SaveSection section,
                                                                   const ISaveParticipant &participant);
    std::unordered_map<SaveParticipantId, ISaveParticipant *, IdHash> participants_;
    std::unordered_map<MigrationKey, const ISaveMigration *, MigrationKeyHash> migrations_;
    SaveGameDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::savegame
