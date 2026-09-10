#include "Epidemic/GameFramework/SaveGame/save_game.h"
#include "Epidemic/Foundation/error.h"
#include <algorithm>
#include <exception>
#include <functional>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace epidemic::gameplay::savegame
{
namespace
{
foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

foundation::Error CallbackError(std::string_view phase, const std::exception *exception = nullptr)
{
    std::string detail(phase);
    if (exception != nullptr)
    {
        detail += ": ";
        detail += exception->what();
    }
    return foundation::Error::Create("gameplay.save.callback_exception", "save callback threw an exception",
                                     std::move(detail));
}

bool AddWithinLimit(std::size_t &total, std::size_t value, std::size_t limit) noexcept
{
    if (value > limit || total > limit - value)
    {
        return false;
    }
    total += value;
    return true;
}

std::vector<SaveParticipantId> SortedUniqueCopy(std::vector<SaveParticipantId> value)
{
    std::sort(value.begin(), value.end());
    value.erase(std::unique(value.begin(), value.end()), value.end());
    return value;
}

bool IsValidRequirement(SaveParticipantRequirement requirement) noexcept
{
    switch (requirement)
    {
    case SaveParticipantRequirement::Required:
    case SaveParticipantRequirement::Optional:
        return true;
    }
    return false;
}

bool IsValidCompatibilityPolicy(ContentCompatibilityPolicy policy) noexcept
{
    switch (policy)
    {
    case ContentCompatibilityPolicy::ExactRequired:
    case ContentCompatibilityPolicy::CompatibleVersion:
    case ContentCompatibilityPolicy::MigrationRequired:
    case ContentCompatibilityPolicy::BestEffort:
    case ContentCompatibilityPolicy::DeveloperMode:
        return true;
    }
    return false;
}

void SaturatingIncrement(std::uint64_t &value) noexcept
{
    if (value != std::numeric_limits<std::uint64_t>::max())
        ++value;
}

} // namespace

DataHash SaveGameOrchestrator::HashBytes(std::span<const std::byte> bytes) noexcept
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto byte : bytes)
    {
        hash ^= std::to_integer<std::uint8_t>(byte);
        hash *= 1099511628211ull;
    }
    return hash;
}

void SaveGameOrchestrator::MarkFailure(SavePhase phase, SaveParticipantId participant,
                                       SaveSchemaVersion schema) noexcept
{
    diagnostics_.failure_phase = phase;
    diagnostics_.last_failed_participant = participant;
    diagnostics_.last_failed_schema_version = schema;
    diagnostics_.phase = SavePhase::Failed;
}

foundation::Result<void> SaveGameOrchestrator::ValidateFormatConfiguration() const
{
    if (format_.current == 0 || format_.minimum_supported == 0 || format_.maximum_supported == 0 ||
        format_.minimum_supported > format_.maximum_supported || format_.current < format_.minimum_supported ||
        format_.current > format_.maximum_supported)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.save.invalid_format_support", "invalid save format support range"));
    }
    if (limits_.max_participants == 0 || limits_.max_section_bytes == 0 || limits_.max_total_payload_bytes == 0 ||
        limits_.max_dependencies_per_participant == 0 || limits_.max_section_bytes > limits_.max_total_payload_bytes)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.save.invalid_resource_limits", "invalid save resource limits"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> SaveGameOrchestrator::SetBarrier(ISaveBarrier &barrier) noexcept
{
    barrier_ = &barrier;
    return foundation::Result<void>::Success();
}

foundation::Result<void> SaveGameOrchestrator::RegisterParticipant(ISaveParticipant &participant)
{
    if (registry_frozen_)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.save.registry_frozen", "save participant registry is frozen"));
    }
    const auto id = participant.Id();
    const auto schema = participant.SchemaVersion();
    const auto requirement = participant.Requirement();
    if (!IsValidRequirement(requirement))
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.save.invalid_requirement", "invalid save participant requirement"));
    }
    if (!id.IsValid() || schema == 0 || participants_.contains(id) || participants_.size() >= limits_.max_participants)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.save.invalid_participant", "invalid or duplicate save participant"));
    }

    std::vector<SaveParticipantId> dependencies;
    try
    {
        dependencies = participant.Dependencies();
    }
    catch (const std::exception &exception)
    {
        return foundation::Result<void>::Failure(CallbackError("participant dependencies", &exception));
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(CallbackError("participant dependencies"));
    }
    if (dependencies.size() > limits_.max_dependencies_per_participant)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.save.too_many_dependencies", "save participant dependency limit exceeded"));
    }
    for (const auto dependency : dependencies)
    {
        if (!dependency.IsValid() || dependency == id)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.save.invalid_dependency", "save participant has invalid dependency"));
        }
    }
    const auto unique_dependencies = SortedUniqueCopy(dependencies);
    if (unique_dependencies.size() != dependencies.size())
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.save.duplicate_dependency", "save participant contains duplicate dependency"));
    }

    participants_.emplace(id, ParticipantRegistration{&participant, schema, requirement,
                                                       std::move(dependencies)});
    return foundation::Result<void>::Success();
}

foundation::Result<void> SaveGameOrchestrator::RegisterMigration(const ISaveMigration &migration)
{
    if (registry_frozen_)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.save.registry_frozen", "save migration registry is frozen"));
    }
    if (!migration.Participant().IsValid() || migration.FromVersion() == 0 || migration.ToVersion() == 0 ||
        migration.ToVersion() <= migration.FromVersion())
    {
        return foundation::Result<void>::Failure(Error("gameplay.save.invalid_migration", "invalid save migration"));
    }
    const MigrationKey key{migration.Participant(), migration.FromVersion()};
    if (migrations_.contains(key))
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.save.duplicate_migration", "duplicate migration edge"));
    }
    migrations_[key] = &migration;
    return foundation::Result<void>::Success();
}

foundation::Result<void> SaveGameOrchestrator::FreezeRegistry()
{
    if (registry_frozen_)
    {
        return foundation::Result<void>::Success();
    }
    auto format = ValidateFormatConfiguration();
    if (!format)
    {
        return format;
    }
    if (participants_.size() > limits_.max_participants)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.save.too_many_participants", "save participant limit exceeded"));
    }

    for (const auto &[id, registration] : participants_)
    {
        if (!IsValidRequirement(registration.requirement))
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.save.invalid_requirement", "invalid save participant requirement"));
        }
        for (const auto dependency : registration.dependencies)
        {
            const auto dependency_it = participants_.find(dependency);
            if (dependency_it == participants_.end())
            {
                return foundation::Result<void>::Failure(
                    Error("gameplay.save.dependency_missing", "save participant dependency missing"));
            }
            if (registration.requirement == SaveParticipantRequirement::Required &&
                dependency_it->second.requirement == SaveParticipantRequirement::Optional)
            {
                return foundation::Result<void>::Failure(Error(
                    "gameplay.save.required_depends_on_optional",
                    "required save participant cannot depend on an optional participant"));
            }
        }
    }
    for (const auto &[key, migration] : migrations_)
    {
        const auto participant_it = participants_.find(key.participant);
        if (participant_it == participants_.end() || migration == nullptr ||
            migration->ToVersion() > participant_it->second.schema_version)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.save.invalid_migration_target", "save migration targets an invalid participant/schema"));
        }
    }

    auto order = ResolveOrder();
    if (!order)
    {
        return foundation::Result<void>::Failure(order.GetError());
    }
    registry_frozen_ = true;
    return foundation::Result<void>::Success();
}

foundation::Result<void> SaveGameOrchestrator::EnsureFrozen()
{
    if (registry_frozen_)
    {
        return foundation::Result<void>::Success();
    }
    return FreezeRegistry();
}

foundation::Result<std::vector<SaveParticipantId>> SaveGameOrchestrator::ResolveOrder() const
{
    enum class Mark
    {
        None,
        Visiting,
        Done
    };
    std::unordered_map<SaveParticipantId, Mark, IdHash> marks;
    std::vector<SaveParticipantId> output;
    std::function<foundation::Result<void>(SaveParticipantId)> visit =
        [&](SaveParticipantId id) -> foundation::Result<void> {
        const auto participant_it = participants_.find(id);
        if (participant_it == participants_.end())
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.save.dependency_missing", "save participant dependency missing"));
        }
        const auto mark = marks[id];
        if (mark == Mark::Done)
        {
            return foundation::Result<void>::Success();
        }
        if (mark == Mark::Visiting)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.save.dependency_cycle", "save participant dependency cycle"));
        }
        marks[id] = Mark::Visiting;
        auto dependencies = participant_it->second.dependencies;
        std::sort(dependencies.begin(), dependencies.end());
        for (const auto dependency : dependencies)
        {
            auto result = visit(dependency);
            if (!result)
            {
                return result;
            }
        }
        marks[id] = Mark::Done;
        output.push_back(id);
        return foundation::Result<void>::Success();
    };

    std::vector<SaveParticipantId> ids;
    ids.reserve(participants_.size());
    for (const auto &[id, registration] : participants_)
    {
        (void)registration;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    for (const auto id : ids)
    {
        auto result = visit(id);
        if (!result)
        {
            return foundation::Result<std::vector<SaveParticipantId>>::Failure(result.GetError());
        }
    }
    return foundation::Result<std::vector<SaveParticipantId>>::Success(std::move(output));
}

foundation::Result<SaveGameImage> SaveGameOrchestrator::Capture(const SaveContext &context)
{
    diagnostics_.failure_phase = SavePhase::Idle;
    diagnostics_.last_failed_participant = {};
    diagnostics_.last_failed_schema_version = 0;
    diagnostics_.phase = SavePhase::Barrier;

    if (barrier_ == nullptr)
    {
        MarkFailure(SavePhase::Barrier);
        return foundation::Result<SaveGameImage>::Failure(
            Error("gameplay.save.barrier_missing", "save capture requires a configured save barrier"));
    }

    std::unique_ptr<ISaveBarrierLease> lease;
    try
    {
        auto acquired = barrier_->AcquireCaptureLease();
        if (!acquired || !acquired.Value())
        {
            MarkFailure(SavePhase::Barrier);
            return foundation::Result<SaveGameImage>::Failure(
                acquired ? Error("gameplay.save.barrier_invalid", "save barrier returned an empty capture lease")
                         : acquired.GetError());
        }
        lease = std::move(acquired.Value());
    }
    catch (const std::exception &exception)
    {
        MarkFailure(SavePhase::Barrier);
        return foundation::Result<SaveGameImage>::Failure(CallbackError("capture barrier", &exception));
    }
    catch (...)
    {
        MarkFailure(SavePhase::Barrier);
        return foundation::Result<SaveGameImage>::Failure(CallbackError("capture barrier"));
    }

    auto frozen = EnsureFrozen();
    if (!frozen)
    {
        MarkFailure(SavePhase::Barrier);
        return foundation::Result<SaveGameImage>::Failure(frozen.GetError());
    }

    auto order = ResolveOrder();
    if (!order)
    {
        MarkFailure(SavePhase::Barrier);
        return foundation::Result<SaveGameImage>::Failure(order.GetError());
    }

    SaveGameImage image;
    image.manifest.save_version = format_.current;
    image.manifest.content_version = context.content_version;
    image.manifest.content_hash = context.content_hash;
    image.manifest.engine_version = context.engine_version;
    image.manifest.framework_version = context.framework_version;
    image.manifest.saved_at = context.now;
    image.manifest.participants.reserve(order.Value().size());
    image.sections.reserve(order.Value().size());

    std::size_t total_payload_bytes = 0;
    diagnostics_.phase = SavePhase::Capturing;
    for (const auto id : order.Value())
    {
        const auto &registration = participants_.at(id);
        foundation::Result<SaveSection> section = foundation::Result<SaveSection>::Failure(
            Error("gameplay.save.capture_failed", "save participant capture failed"));
        try
        {
            section = registration.participant->CaptureSnapshot(context);
        }
        catch (const std::exception &exception)
        {
            MarkFailure(SavePhase::Capturing, id, registration.schema_version);
            return foundation::Result<SaveGameImage>::Failure(CallbackError("participant capture", &exception));
        }
        catch (...)
        {
            MarkFailure(SavePhase::Capturing, id, registration.schema_version);
            return foundation::Result<SaveGameImage>::Failure(CallbackError("participant capture"));
        }
        if (!section)
        {
            MarkFailure(SavePhase::Capturing, id, registration.schema_version);
            return foundation::Result<SaveGameImage>::Failure(section.GetError());
        }
        if (!AddWithinLimit(total_payload_bytes, section.Value().payload.size(), limits_.max_total_payload_bytes) ||
            section.Value().payload.size() > limits_.max_section_bytes)
        {
            MarkFailure(SavePhase::Capturing, id, registration.schema_version);
            return foundation::Result<SaveGameImage>::Failure(
                Error("gameplay.save.payload_limit", "captured save payload exceeds configured resource limits"));
        }

        section.Value().participant = id;
        section.Value().schema_version = registration.schema_version;
        section.Value().payload_hash = HashBytes(section.Value().payload);
        image.manifest.participants.push_back({id, registration.schema_version, section.Value().payload_hash,
                                               registration.requirement, registration.dependencies});
        image.sections.push_back(std::move(section.Value()));
    }

    SaturatingIncrement(diagnostics_.captures);
    diagnostics_.phase = SavePhase::Complete;
    return foundation::Result<SaveGameImage>::Success(std::move(image));
}

foundation::Result<void> SaveGameOrchestrator::ValidateCompatibility(const SaveGameImage &image,
                                                                      const RestoreContext &context) const
{
    if (!IsValidCompatibilityPolicy(context.compatibility))
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.save.invalid_compatibility_policy", "invalid save compatibility policy"));
    }
    const auto save_version = image.manifest.save_version;
    if (save_version < format_.minimum_supported || save_version > format_.maximum_supported)
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.save.unsupported_format", "save format version is not supported"));
    }

    switch (context.compatibility)
    {
    case ContentCompatibilityPolicy::ExactRequired:
        if (save_version != format_.current || image.manifest.content_version != context.current_content_version ||
            image.manifest.content_hash != context.current_content_hash ||
            image.manifest.engine_version != context.current_engine_version ||
            image.manifest.framework_version != context.current_framework_version)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.save.compatibility_mismatch", "save metadata does not exactly match runtime"));
        }
        break;
    case ContentCompatibilityPolicy::CompatibleVersion:
        if (image.manifest.content_version != context.current_content_version ||
            image.manifest.engine_version > context.current_engine_version ||
            image.manifest.framework_version > context.current_framework_version)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.save.compatibility_mismatch", "save is not compatible with current runtime version"));
        }
        break;
    case ContentCompatibilityPolicy::MigrationRequired:
        if (image.manifest.content_version > context.current_content_version ||
            image.manifest.engine_version > context.current_engine_version ||
            image.manifest.framework_version > context.current_framework_version)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.save.compatibility_mismatch", "save requires a newer runtime/content version"));
        }
        break;
    case ContentCompatibilityPolicy::BestEffort:
    case ContentCompatibilityPolicy::DeveloperMode:
        break;
    default:
        return foundation::Result<void>::Failure(
            Error("gameplay.save.invalid_compatibility_policy", "invalid save compatibility policy"));
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> SaveGameOrchestrator::ValidateImageStructure(const SaveGameImage &image,
                                                                       const RestoreContext &context) const
{
    if (image.manifest.participants.size() > limits_.max_participants || image.sections.size() > limits_.max_participants ||
        image.manifest.participants.size() != image.sections.size())
    {
        return foundation::Result<void>::Failure(
            Error("gameplay.save.manifest_structure", "save participant/section count is invalid"));
    }

    std::unordered_map<SaveParticipantId, const SaveParticipantManifest *, IdHash> manifest_entries;
    manifest_entries.reserve(image.manifest.participants.size());
    for (const auto &entry : image.manifest.participants)
    {
        if (!entry.id.IsValid() || entry.schema_version == 0 || !IsValidRequirement(entry.requirement) ||
            manifest_entries.contains(entry.id) ||
            entry.dependencies.size() > limits_.max_dependencies_per_participant)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.save.manifest_structure", "save participant manifest entry is invalid"));
        }
        std::unordered_set<SaveParticipantId, IdHash> dependencies;
        for (const auto dependency : entry.dependencies)
        {
            if (!dependency.IsValid() || dependency == entry.id || !dependencies.insert(dependency).second)
            {
                return foundation::Result<void>::Failure(
                    Error("gameplay.save.manifest_structure", "save participant manifest dependencies are invalid"));
            }
        }
        manifest_entries.emplace(entry.id, &entry);
    }

    std::unordered_map<SaveParticipantId, const SaveSection *, IdHash> sections;
    sections.reserve(image.sections.size());
    std::size_t total_payload_bytes = 0;
    for (const auto &section : image.sections)
    {
        if (!section.participant.IsValid() || section.schema_version == 0 || sections.contains(section.participant) ||
            section.payload.size() > limits_.max_section_bytes ||
            !AddWithinLimit(total_payload_bytes, section.payload.size(), limits_.max_total_payload_bytes) ||
            HashBytes(section.payload) != section.payload_hash)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.save.corrupt_section", "save section invalid, oversized or corrupt"));
        }
        const auto manifest_it = manifest_entries.find(section.participant);
        if (manifest_it == manifest_entries.end() || manifest_it->second->schema_version != section.schema_version ||
            manifest_it->second->payload_hash != section.payload_hash)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.save.manifest_section_mismatch", "save manifest does not match participant section"));
        }
        sections.emplace(section.participant, &section);
    }

    for (const auto &[id, registration] : participants_)
    {
        const auto manifest_it = manifest_entries.find(id);
        if (manifest_it == manifest_entries.end())
        {
            if (context.compatibility == ContentCompatibilityPolicy::ExactRequired ||
                registration.requirement == SaveParticipantRequirement::Required)
            {
                return foundation::Result<void>::Failure(
                    Error("gameplay.save.section_missing", "registered save participant section is missing"));
            }
            continue;
        }
        if (manifest_it->second->requirement != registration.requirement)
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.save.participant_contract_mismatch", "save participant requirement changed"));
        }
        if (SortedUniqueCopy(manifest_it->second->dependencies) != SortedUniqueCopy(registration.dependencies))
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.save.participant_contract_mismatch", "save participant dependencies changed"));
        }
    }

    for (const auto &[id, manifest] : manifest_entries)
    {
        if (participants_.contains(id))
        {
            continue;
        }
        if (context.compatibility == ContentCompatibilityPolicy::ExactRequired ||
            (context.compatibility != ContentCompatibilityPolicy::DeveloperMode &&
             manifest->requirement == SaveParticipantRequirement::Required))
        {
            return foundation::Result<void>::Failure(
                Error("gameplay.save.unknown_participant", "save contains an unknown required participant"));
        }
    }

    return foundation::Result<void>::Success();
}

foundation::Result<SaveSection> SaveGameOrchestrator::MigrateToCurrent(SaveSection section,
                                                                       const ParticipantRegistration &registration,
                                                                       ContentCompatibilityPolicy policy)
{
    const auto participant_id = registration.participant->Id();
    if (section.participant != participant_id)
    {
        return foundation::Result<SaveSection>::Failure(
            Error("gameplay.save.participant_mismatch", "save section belongs to a different participant"));
    }
    if (policy == ContentCompatibilityPolicy::ExactRequired)
    {
        if (section.schema_version != registration.schema_version)
        {
            return foundation::Result<SaveSection>::Failure(
                Error("gameplay.save.schema_mismatch", "exact restore requires the registered participant schema"));
        }
        return foundation::Result<SaveSection>::Success(std::move(section));
    }
    if (section.schema_version > registration.schema_version)
    {
        return foundation::Result<SaveSection>::Failure(
            Error("gameplay.save.schema_newer", "save section schema is newer than runtime"));
    }

    std::unordered_set<std::uint32_t> seen;
    while (section.schema_version < registration.schema_version)
    {
        if (!seen.insert(section.schema_version).second)
        {
            return foundation::Result<SaveSection>::Failure(
                Error("gameplay.save.migration_cycle", "migration chain cycles"));
        }
        const auto migration_it = migrations_.find({participant_id, section.schema_version});
        if (migration_it == migrations_.end())
        {
            return foundation::Result<SaveSection>::Failure(
                Error("gameplay.save.migration_missing", "required save migration missing"));
        }
        const auto *migration = migration_it->second;
        foundation::Result<SaveSection> migrated = foundation::Result<SaveSection>::Failure(
            Error("gameplay.save.migration_failed", "save migration failed"));
        try
        {
            migrated = migration->Migrate(section);
        }
        catch (const std::exception &exception)
        {
            return foundation::Result<SaveSection>::Failure(CallbackError("save migration", &exception));
        }
        catch (...)
        {
            return foundation::Result<SaveSection>::Failure(CallbackError("save migration"));
        }
        if (!migrated)
        {
            return migrated;
        }
        if (migrated.Value().participant != participant_id || migrated.Value().schema_version != migration->ToVersion() ||
            migrated.Value().schema_version <= section.schema_version ||
            migrated.Value().payload.size() > limits_.max_section_bytes)
        {
            return foundation::Result<SaveSection>::Failure(Error(
                "gameplay.save.migration_invalid_result",
                "migration changed participant identity, returned wrong schema or exceeded resource limits"));
        }
        section = std::move(migrated.Value());
        section.payload_hash = HashBytes(section.payload);
        SaturatingIncrement(diagnostics_.migrations);
    }
    if (section.schema_version != registration.schema_version)
    {
        return foundation::Result<SaveSection>::Failure(
            Error("gameplay.save.schema_mismatch", "migration did not reach registered participant schema"));
    }
    return foundation::Result<SaveSection>::Success(std::move(section));
}

foundation::Result<void> SaveGameOrchestrator::Restore(SaveGameImage image, const RestoreContext &context)
{
    diagnostics_.failure_phase = SavePhase::Idle;
    diagnostics_.last_failed_participant = {};
    diagnostics_.last_failed_schema_version = 0;
    diagnostics_.phase = SavePhase::Barrier;

    if (barrier_ == nullptr)
    {
        MarkFailure(SavePhase::Barrier);
        return foundation::Result<void>::Failure(
            Error("gameplay.save.barrier_missing", "save restore requires a configured save barrier"));
    }

    std::unique_ptr<ISaveBarrierLease> lease;
    try
    {
        auto acquired = barrier_->AcquireRestoreLease();
        if (!acquired || !acquired.Value())
        {
            MarkFailure(SavePhase::Barrier);
            return foundation::Result<void>::Failure(
                acquired ? Error("gameplay.save.barrier_invalid", "save barrier returned an empty restore lease")
                         : acquired.GetError());
        }
        lease = std::move(acquired.Value());
    }
    catch (const std::exception &exception)
    {
        MarkFailure(SavePhase::Barrier);
        return foundation::Result<void>::Failure(CallbackError("restore barrier", &exception));
    }
    catch (...)
    {
        MarkFailure(SavePhase::Barrier);
        return foundation::Result<void>::Failure(CallbackError("restore barrier"));
    }

    auto frozen = EnsureFrozen();
    if (!frozen)
    {
        MarkFailure(SavePhase::Barrier);
        return foundation::Result<void>::Failure(frozen.GetError());
    }

    diagnostics_.phase = SavePhase::Validating;
    auto compatibility = ValidateCompatibility(image, context);
    if (!compatibility)
    {
        SaturatingIncrement(diagnostics_.validation_failures);
        MarkFailure(SavePhase::Validating);
        return compatibility;
    }
    auto structure = ValidateImageStructure(image, context);
    if (!structure)
    {
        SaturatingIncrement(diagnostics_.validation_failures);
        MarkFailure(SavePhase::Validating);
        return structure;
    }
    auto order = ResolveOrder();
    if (!order)
    {
        SaturatingIncrement(diagnostics_.validation_failures);
        MarkFailure(SavePhase::Validating);
        return foundation::Result<void>::Failure(order.GetError());
    }

    std::unordered_map<SaveParticipantId, SaveSection, IdHash> sections;
    sections.reserve(image.sections.size());
    for (auto &section : image.sections)
    {
        sections.emplace(section.participant, std::move(section));
    }

    std::unordered_set<SaveParticipantId, IdHash> skipped;
    std::unordered_map<SaveParticipantId, SaveSection, IdHash> migrated_sections;
    migrated_sections.reserve(participants_.size());
    std::size_t migrated_total_payload_bytes = 0;

    for (const auto id : order.Value())
    {
        const auto &registration = participants_.at(id);
        const auto section_it = sections.find(id);
        if (section_it == sections.end())
        {
            skipped.insert(id);
            SaturatingIncrement(diagnostics_.optional_participants_skipped);
            continue;
        }
        bool dependency_skipped = false;
        for (const auto dependency : registration.dependencies)
        {
            if (skipped.contains(dependency))
            {
                dependency_skipped = true;
                break;
            }
        }
        if (dependency_skipped)
        {
            if (registration.requirement == SaveParticipantRequirement::Required)
            {
                SaturatingIncrement(diagnostics_.validation_failures);
                MarkFailure(SavePhase::Validating, id, registration.schema_version);
                return foundation::Result<void>::Failure(
                    Error("gameplay.save.required_dependency_skipped", "required participant dependency was skipped"));
            }
            skipped.insert(id);
            SaturatingIncrement(diagnostics_.optional_participants_skipped);
            continue;
        }

        auto migrated = MigrateToCurrent(std::move(section_it->second), registration, context.compatibility);
        if (!migrated)
        {
            const bool may_skip = registration.requirement == SaveParticipantRequirement::Optional &&
                                  (context.compatibility == ContentCompatibilityPolicy::BestEffort ||
                                   context.compatibility == ContentCompatibilityPolicy::DeveloperMode);
            if (may_skip)
            {
                skipped.insert(id);
                SaturatingIncrement(diagnostics_.optional_participants_skipped);
                continue;
            }
            SaturatingIncrement(diagnostics_.validation_failures);
            MarkFailure(SavePhase::Validating, id, section_it->second.schema_version);
            return foundation::Result<void>::Failure(migrated.GetError());
        }

        if (!AddWithinLimit(migrated_total_payload_bytes, migrated.Value().payload.size(),
                            limits_.max_total_payload_bytes))
        {
            SaturatingIncrement(diagnostics_.validation_failures);
            MarkFailure(SavePhase::Validating, id, migrated.Value().schema_version);
            return foundation::Result<void>::Failure(
                Error("gameplay.save.payload_limit", "migrated save payload exceeds configured resource limits"));
        }

        foundation::Result<void> valid = foundation::Result<void>::Failure(
            Error("gameplay.save.validation_failed", "save participant validation failed"));
        try
        {
            valid = registration.participant->ValidateSnapshot(migrated.Value(), context);
        }
        catch (const std::exception &exception)
        {
            valid = foundation::Result<void>::Failure(CallbackError("participant validation", &exception));
        }
        catch (...)
        {
            valid = foundation::Result<void>::Failure(CallbackError("participant validation"));
        }
        if (!valid)
        {
            const bool may_skip = registration.requirement == SaveParticipantRequirement::Optional &&
                                  (context.compatibility == ContentCompatibilityPolicy::BestEffort ||
                                   context.compatibility == ContentCompatibilityPolicy::DeveloperMode);
            if (may_skip)
            {
                skipped.insert(id);
                SaturatingIncrement(diagnostics_.optional_participants_skipped);
                continue;
            }
            SaturatingIncrement(diagnostics_.validation_failures);
            MarkFailure(SavePhase::Validating, id, migrated.Value().schema_version);
            return valid;
        }
        migrated_sections.emplace(id, std::move(migrated.Value()));
    }

    diagnostics_.phase = SavePhase::Staging;
    struct StageRecord
    {
        SaveParticipantId id{};
        ISaveParticipant *participant = nullptr;
        std::unique_ptr<IRestoreStage> stage;
    };
    std::vector<StageRecord> stages;
    stages.reserve(migrated_sections.size());
    for (const auto id : order.Value())
    {
        if (skipped.contains(id))
        {
            continue;
        }
        const auto &registration = participants_.at(id);
        bool dependency_skipped = false;
        for (const auto dependency : registration.dependencies)
        {
            if (skipped.contains(dependency))
            {
                dependency_skipped = true;
                break;
            }
        }
        if (dependency_skipped)
        {
            if (registration.requirement == SaveParticipantRequirement::Required)
            {
                MarkFailure(SavePhase::Staging, id, registration.schema_version);
                return foundation::Result<void>::Failure(
                    Error("gameplay.save.required_dependency_skipped", "required participant dependency was skipped"));
            }
            skipped.insert(id);
            SaturatingIncrement(diagnostics_.optional_participants_skipped);
            continue;
        }

        foundation::Result<std::unique_ptr<IRestoreStage>> staged =
            foundation::Result<std::unique_ptr<IRestoreStage>>::Failure(
                Error("gameplay.save.stage_failed", "save participant staging failed"));
        try
        {
            staged = registration.participant->StageRestore(migrated_sections.at(id), context);
        }
        catch (const std::exception &exception)
        {
            staged = foundation::Result<std::unique_ptr<IRestoreStage>>::Failure(
                CallbackError("participant stage", &exception));
        }
        catch (...)
        {
            staged = foundation::Result<std::unique_ptr<IRestoreStage>>::Failure(CallbackError("participant stage"));
        }
        if (!staged || !staged.Value())
        {
            const bool may_skip = registration.requirement == SaveParticipantRequirement::Optional &&
                                  (context.compatibility == ContentCompatibilityPolicy::BestEffort ||
                                   context.compatibility == ContentCompatibilityPolicy::DeveloperMode);
            if (may_skip)
            {
                skipped.insert(id);
                SaturatingIncrement(diagnostics_.optional_participants_skipped);
                continue;
            }
            MarkFailure(SavePhase::Staging, id, registration.schema_version);
            return foundation::Result<void>::Failure(
                staged ? Error("gameplay.save.stage_invalid", "participant returned an empty restore stage")
                       : staged.GetError());
        }
        stages.push_back({id, registration.participant, std::move(staged.Value())});
    }

    diagnostics_.phase = SavePhase::Committing;
    for (auto &record : stages)
    {
        record.participant->CommitRestore(*record.stage);
    }

    SaturatingIncrement(diagnostics_.restores);
    diagnostics_.phase = SavePhase::Complete;
    return foundation::Result<void>::Success();
}
} // namespace epidemic::gameplay::savegame
