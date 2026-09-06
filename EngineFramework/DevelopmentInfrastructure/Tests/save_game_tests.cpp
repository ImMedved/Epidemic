#include "Epidemic/Foundation/error.h"
#include "Epidemic/GameFramework/SaveGame/save_game.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::savegame;

namespace
{
void Check(bool value, const char *message)
{
    if (!value)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

struct BarrierLease final : ISaveBarrierLease
{
    explicit BarrierLease(bool &active) : active_(&active)
    {
        active = true;
    }
    ~BarrierLease() override
    {
        *active_ = false;
    }
    bool *active_ = nullptr;
};

class Barrier final : public ISaveBarrier
{
  public:
    bool active = false;
    bool fail_capture = false;
    bool fail_restore = false;
    int capture_acquires = 0;
    int restore_acquires = 0;

    epidemic::foundation::Result<std::unique_ptr<ISaveBarrierLease>> AcquireCaptureLease() override
    {
        ++capture_acquires;
        if (fail_capture)
        {
            return epidemic::foundation::Result<std::unique_ptr<ISaveBarrierLease>>::Failure(
                epidemic::foundation::Error::Create("barrier", "capture failed"));
        }
        return epidemic::foundation::Result<std::unique_ptr<ISaveBarrierLease>>::Success(
            std::make_unique<BarrierLease>(active));
    }

    epidemic::foundation::Result<std::unique_ptr<ISaveBarrierLease>> AcquireRestoreLease() override
    {
        ++restore_acquires;
        if (fail_restore)
        {
            return epidemic::foundation::Result<std::unique_ptr<ISaveBarrierLease>>::Failure(
                epidemic::foundation::Error::Create("barrier", "restore failed"));
        }
        return epidemic::foundation::Result<std::unique_ptr<ISaveBarrierLease>>::Success(
            std::make_unique<BarrierLease>(active));
    }
};

struct Stage final : IRestoreStage
{
    int value = 0;
};

class Part final : public ISaveParticipant
{
  public:
    SaveParticipantId id{};
    SaveSchemaVersion version = 1;
    SaveParticipantRequirement requirement = SaveParticipantRequirement::Required;
    std::vector<SaveParticipantId> deps;
    int value = 0;
    bool fail_stage = false;
    bool throw_validate = false;
    bool throw_capture = false;
    bool *barrier_active = nullptr;
    std::vector<SaveParticipantId> *commit_order = nullptr;

    SaveParticipantId Id() const noexcept override
    {
        return id;
    }
    SaveSchemaVersion SchemaVersion() const noexcept override
    {
        return version;
    }
    SaveParticipantRequirement Requirement() const noexcept override
    {
        return requirement;
    }
    std::vector<SaveParticipantId> Dependencies() const override
    {
        return deps;
    }
    epidemic::foundation::Result<SaveSection> CaptureSnapshot(const SaveContext &) const override
    {
        if (throw_capture)
        {
            throw std::runtime_error("capture exception");
        }
        if (barrier_active != nullptr && !*barrier_active)
        {
            return epidemic::foundation::Result<SaveSection>::Failure(
                epidemic::foundation::Error::Create("barrier", "capture outside barrier"));
        }
        SaveSection section;
        section.participant = id;
        section.schema_version = version;
        section.payload.resize(sizeof(value));
        std::memcpy(section.payload.data(), &value, sizeof(value));
        return epidemic::foundation::Result<SaveSection>::Success(std::move(section));
    }
    epidemic::foundation::Result<void> ValidateSnapshot(const SaveSection &section,
                                                        const RestoreContext &) const override
    {
        if (throw_validate)
        {
            throw std::runtime_error("validation exception");
        }
        return section.payload.size() == sizeof(int)
                   ? epidemic::foundation::Result<void>::Success()
                   : epidemic::foundation::Result<void>::Failure(
                         epidemic::foundation::Error::Create("invalid", "invalid payload"));
    }
    epidemic::foundation::Result<std::unique_ptr<IRestoreStage>> StageRestore(const SaveSection &section,
                                                                              const RestoreContext &) override
    {
        if (fail_stage)
        {
            return epidemic::foundation::Result<std::unique_ptr<IRestoreStage>>::Failure(
                epidemic::foundation::Error::Create("stage", "failed"));
        }
        auto stage = std::make_unique<Stage>();
        std::memcpy(&stage->value, section.payload.data(), sizeof(int));
        return epidemic::foundation::Result<std::unique_ptr<IRestoreStage>>::Success(std::move(stage));
    }
    void CommitRestore(IRestoreStage &stage) noexcept override
    {
        if (barrier_active != nullptr && !*barrier_active)
        {
            std::abort();
        }
        value = static_cast<Stage &>(stage).value;
        if (commit_order != nullptr)
        {
            commit_order->push_back(id);
        }
    }
};

class Migration final : public ISaveMigration
{
  public:
    SaveParticipantId id{};
    SaveSchemaVersion from = 1;
    SaveSchemaVersion to = 2;
    bool corrupt_identity = false;

    SaveParticipantId Participant() const noexcept override
    {
        return id;
    }
    SaveSchemaVersion FromVersion() const noexcept override
    {
        return from;
    }
    SaveSchemaVersion ToVersion() const noexcept override
    {
        return to;
    }
    epidemic::foundation::Result<SaveSection> Migrate(const SaveSection &source) const override
    {
        auto result = source;
        result.schema_version = to;
        if (corrupt_identity)
        {
            result.participant = SaveParticipantId::FromString("wrong");
        }
        return epidemic::foundation::Result<SaveSection>::Success(std::move(result));
    }
};

SaveContext SaveCtx()
{
    SaveContext context;
    context.content_version = 4;
    context.content_hash = 99;
    context.engine_version = 7;
    context.framework_version = 8;
    return context;
}

RestoreContext ExactRestoreCtx()
{
    RestoreContext context;
    context.compatibility = ContentCompatibilityPolicy::ExactRequired;
    context.current_content_version = 4;
    context.current_content_hash = 99;
    context.current_engine_version = 7;
    context.current_framework_version = 8;
    return context;
}

void Rehash(SaveGameImage &image, std::size_t index)
{
    auto &section = image.sections[index];
    section.payload_hash = SaveGameOrchestrator::HashBytes(section.payload);
    auto manifest_it = std::find_if(image.manifest.participants.begin(), image.manifest.participants.end(),
                                    [&](const SaveParticipantManifest &entry) {
                                        return entry.id == section.participant;
                                    });
    Check(manifest_it != image.manifest.participants.end(), "manifest entry found for rehash");
    manifest_it->payload_hash = section.payload_hash;
    manifest_it->schema_version = section.schema_version;
}
} // namespace

int main()
{
    Barrier barrier;
    std::vector<SaveParticipantId> commit_order;
    Part a;
    a.id = SaveParticipantId::FromString("a");
    a.value = 11;
    a.barrier_active = &barrier.active;
    a.commit_order = &commit_order;
    Part b;
    b.id = SaveParticipantId::FromString("b");
    b.value = 22;
    b.deps = {a.id};
    b.barrier_active = &barrier.active;
    b.commit_order = &commit_order;

    SaveGameOrchestrator orchestrator;
    Check(static_cast<bool>(orchestrator.SetBarrier(barrier)), "set barrier");
    Check(static_cast<bool>(orchestrator.RegisterParticipant(a)), "register a");
    Check(static_cast<bool>(orchestrator.RegisterParticipant(b)), "register b");
    auto order = orchestrator.ResolveOrder();
    Check(static_cast<bool>(order) && order.Value().size() == 2 && order.Value()[0] == a.id && order.Value()[1] == b.id,
          "dependency order deterministic");

    auto image = orchestrator.Capture(SaveCtx());
    Check(static_cast<bool>(image), "capture");
    Check(!barrier.active && barrier.capture_acquires == 1, "capture barrier lease released");
    Check(orchestrator.RegistryFrozen(), "registry freezes on first operation");
    Part late;
    late.id = SaveParticipantId::FromString("late");
    Check(!orchestrator.RegisterParticipant(late), "registration rejected after freeze");

    a.value = 100;
    b.value = 200;
    b.fail_stage = true;
    Check(!orchestrator.Restore(image.Value(), ExactRestoreCtx()), "staging failure");
    Check(a.value == 100 && b.value == 200, "staging failure leaves every participant untouched");
    Check(!barrier.active, "restore barrier released after failed stage");

    b.fail_stage = false;
    commit_order.clear();
    Check(static_cast<bool>(orchestrator.Restore(image.Value(), ExactRestoreCtx())), "restore");
    Check(a.value == 11 && b.value == 22, "values restored");
    Check(commit_order.size() == 2 && commit_order[0] == a.id && commit_order[1] == b.id,
          "commit ordering deterministic");
    Check(!barrier.active, "restore barrier released after commit");

    auto bad_metadata = image.Value();
    bad_metadata.manifest.framework_version = 9;
    Check(!orchestrator.Restore(std::move(bad_metadata), ExactRestoreCtx()), "exact metadata mismatch rejected");

    auto corrupt_manifest = image.Value();
    corrupt_manifest.manifest.participants[0].payload_hash ^= 1;
    Check(!orchestrator.Restore(std::move(corrupt_manifest), ExactRestoreCtx()), "manifest section mismatch rejected");

    auto unsupported = image.Value();
    unsupported.manifest.save_version = 2;
    Check(!orchestrator.Restore(std::move(unsupported), ExactRestoreCtx()), "unsupported save format rejected");

    Part throwing;
    throwing.id = SaveParticipantId::FromString("throwing");
    throwing.value = 5;
    throwing.throw_validate = true;
    Barrier exception_barrier;
    SaveGameOrchestrator exception_orchestrator;
    Check(static_cast<bool>(exception_orchestrator.SetBarrier(exception_barrier)), "exception barrier");
    Check(static_cast<bool>(exception_orchestrator.RegisterParticipant(throwing)), "register throwing participant");
    throwing.throw_validate = false;
    auto throwing_image = exception_orchestrator.Capture(SaveCtx());
    Check(static_cast<bool>(throwing_image), "capture throwing participant image");
    throwing.throw_validate = true;
    Check(!exception_orchestrator.Restore(std::move(throwing_image.Value()), ExactRestoreCtx()),
          "participant exception converted to error");

    Part migrated_part;
    migrated_part.id = SaveParticipantId::FromString("migrated");
    migrated_part.version = 2;
    migrated_part.value = 31;
    Migration migration;
    migration.id = migrated_part.id;
    Barrier migration_barrier;
    SaveGameOrchestrator migration_orchestrator;
    Check(static_cast<bool>(migration_orchestrator.SetBarrier(migration_barrier)), "migration barrier");
    Check(static_cast<bool>(migration_orchestrator.RegisterParticipant(migrated_part)), "register migrated participant");
    Check(static_cast<bool>(migration_orchestrator.RegisterMigration(migration)), "register migration");
    auto migrated_image = migration_orchestrator.Capture(SaveCtx());
    Check(static_cast<bool>(migrated_image), "capture migrated participant");
    migrated_image.Value().sections[0].schema_version = 1;
    Rehash(migrated_image.Value(), 0);
    migrated_part.value = 44;
    auto migration_context = ExactRestoreCtx();
    migration_context.compatibility = ContentCompatibilityPolicy::MigrationRequired;
    Check(static_cast<bool>(migration_orchestrator.Restore(migrated_image.Value(), migration_context)),
          "explicit migration chain restores old schema");
    Check(migrated_part.value == 31, "migrated value committed");

    auto bad_migration_image = migrated_image.Value();
    migration.corrupt_identity = true;
    migrated_part.value = 52;
    Check(!migration_orchestrator.Restore(std::move(bad_migration_image), migration_context),
          "migration cannot change participant identity");
    Check(migrated_part.value == 52, "failed migration leaves live state untouched");
    migration.corrupt_identity = false;

    Part required;
    required.id = SaveParticipantId::FromString("required");
    required.value = 61;
    Part optional;
    optional.id = SaveParticipantId::FromString("optional");
    optional.requirement = SaveParticipantRequirement::Optional;
    optional.value = 62;
    Barrier optional_barrier;
    SaveGameOrchestrator optional_orchestrator;
    Check(static_cast<bool>(optional_orchestrator.SetBarrier(optional_barrier)), "optional barrier");
    Check(static_cast<bool>(optional_orchestrator.RegisterParticipant(required)), "register required");
    Check(static_cast<bool>(optional_orchestrator.RegisterParticipant(optional)), "register optional");
    auto optional_image = optional_orchestrator.Capture(SaveCtx());
    Check(static_cast<bool>(optional_image), "capture optional image");
    optional_image.Value().sections.erase(
        std::remove_if(optional_image.Value().sections.begin(), optional_image.Value().sections.end(),
                       [&](const SaveSection &section) { return section.participant == optional.id; }),
        optional_image.Value().sections.end());
    optional_image.Value().manifest.participants.erase(
        std::remove_if(optional_image.Value().manifest.participants.begin(),
                       optional_image.Value().manifest.participants.end(),
                       [&](const SaveParticipantManifest &entry) { return entry.id == optional.id; }),
        optional_image.Value().manifest.participants.end());
    required.value = 70;
    optional.value = 71;
    auto best_effort_context = ExactRestoreCtx();
    best_effort_context.compatibility = ContentCompatibilityPolicy::BestEffort;
    Check(static_cast<bool>(optional_orchestrator.Restore(std::move(optional_image.Value()), best_effort_context)),
          "best effort permits missing optional participant");
    Check(required.value == 61 && optional.value == 71, "missing optional participant leaves its live state unchanged");

    SaveResourceLimits small_limits;
    small_limits.max_section_bytes = 2;
    small_limits.max_total_payload_bytes = 2;
    Barrier limited_barrier;
    Part limited;
    limited.id = SaveParticipantId::FromString("limited");
    SaveGameOrchestrator limited_orchestrator({}, small_limits);
    Check(static_cast<bool>(limited_orchestrator.SetBarrier(limited_barrier)), "limited barrier");
    Check(static_cast<bool>(limited_orchestrator.RegisterParticipant(limited)), "register limited");
    Check(!limited_orchestrator.Capture(SaveCtx()), "capture resource limit enforced");

    Part no_barrier_part;
    no_barrier_part.id = SaveParticipantId::FromString("no_barrier");
    SaveGameOrchestrator no_barrier;
    Check(static_cast<bool>(no_barrier.RegisterParticipant(no_barrier_part)), "register no barrier participant");
    Check(!no_barrier.Capture(SaveCtx()), "capture without quiescence barrier rejected");

    Part c;
    c.id = SaveParticipantId::FromString("c");
    c.deps = {SaveParticipantId::FromString("d")};
    Part d;
    d.id = SaveParticipantId::FromString("d");
    d.deps = {c.id};
    Barrier cycle_barrier;
    SaveGameOrchestrator cycle;
    Check(static_cast<bool>(cycle.SetBarrier(cycle_barrier)), "cycle barrier");
    Check(static_cast<bool>(cycle.RegisterParticipant(c)) && static_cast<bool>(cycle.RegisterParticipant(d)),
          "cycle participants");
    Check(!cycle.ResolveOrder(), "dependency cycle rejected");

    return 0;
}
