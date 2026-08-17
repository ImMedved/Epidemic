#include "Epidemic/GameFramework/SaveGame/save_game.h"
#include "Epidemic/Foundation/error.h"
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <utility>
namespace epidemic::gameplay::savegame
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}
} // namespace
DataHash SaveGameOrchestrator::HashBytes(std::span<const std::byte> bytes) noexcept
{
    std::uint64_t h = 14695981039346656037ull;
    for (auto b : bytes)
    {
        h ^= std::to_integer<std::uint8_t>(b);
        h *= 1099511628211ull;
    }
    return h;
}
foundation::Result<void> SaveGameOrchestrator::RegisterParticipant(ISaveParticipant &p)
{
    const auto id = p.Id();
    if (!id.IsValid() || participants_.contains(id))
        return foundation::Result<void>::Failure(
            Error("gameplay.save.invalid_participant", "invalid or duplicate save participant"));
    participants_[id] = &p;
    return foundation::Result<void>::Success();
}
foundation::Result<void> SaveGameOrchestrator::RegisterMigration(const ISaveMigration &m)
{
    if (!m.Participant().IsValid() || m.ToVersion() <= m.FromVersion())
        return foundation::Result<void>::Failure(Error("gameplay.save.invalid_migration", "invalid save migration"));
    MigrationKey k{m.Participant(), m.FromVersion()};
    if (migrations_.contains(k))
        return foundation::Result<void>::Failure(
            Error("gameplay.save.duplicate_migration", "duplicate migration edge"));
    migrations_[k] = &m;
    return foundation::Result<void>::Success();
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
    std::vector<SaveParticipantId> out;
    std::function<foundation::Result<void>(SaveParticipantId)> visit =
        [&](SaveParticipantId id) -> foundation::Result<void> {
        auto pit = participants_.find(id);
        if (pit == participants_.end())
            return foundation::Result<void>::Failure(
                Error("gameplay.save.dependency_missing", "save participant dependency missing"));
        auto mark = marks[id];
        if (mark == Mark::Done)
            return foundation::Result<void>::Success();
        if (mark == Mark::Visiting)
            return foundation::Result<void>::Failure(
                Error("gameplay.save.dependency_cycle", "save participant dependency cycle"));
        marks[id] = Mark::Visiting;
        auto deps = pit->second->Dependencies();
        std::sort(deps.begin(), deps.end());
        for (auto dep : deps)
        {
            auto r = visit(dep);
            if (!r)
                return r;
        }
        marks[id] = Mark::Done;
        out.push_back(id);
        return foundation::Result<void>::Success();
    };
    std::vector<SaveParticipantId> ids;
    for (const auto &[id, p] : participants_)
    {
        (void)p;
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    for (auto id : ids)
    {
        auto r = visit(id);
        if (!r)
            return foundation::Result<std::vector<SaveParticipantId>>::Failure(r.GetError());
    }
    return foundation::Result<std::vector<SaveParticipantId>>::Success(std::move(out));
}
foundation::Result<SaveGameImage> SaveGameOrchestrator::Capture(const SaveContext &c)
{
    diagnostics_.phase = SavePhase::Barrier;
    auto order = ResolveOrder();
    if (!order)
    {
        diagnostics_.phase = SavePhase::Failed;
        return foundation::Result<SaveGameImage>::Failure(order.GetError());
    }
    SaveGameImage image;
    image.manifest.content_version = c.content_version;
    image.manifest.content_hash = c.content_hash;
    image.manifest.engine_version = c.engine_version;
    image.manifest.framework_version = c.framework_version;
    image.manifest.saved_at = c.now;
    diagnostics_.phase = SavePhase::Capturing;
    for (auto id : order.Value())
    {
        auto *p = participants_.at(id);
        auto section = p->CaptureSnapshot(c);
        if (!section)
        {
            diagnostics_.phase = SavePhase::Failed;
            return foundation::Result<SaveGameImage>::Failure(section.GetError());
        }
        section.Value().participant = id;
        section.Value().schema_version = p->SchemaVersion();
        section.Value().payload_hash = HashBytes(section.Value().payload);
        image.manifest.participants.push_back(
            {id, p->SchemaVersion(), section.Value().payload_hash, p->Dependencies()});
        image.sections.push_back(std::move(section.Value()));
    }
    ++diagnostics_.captures;
    diagnostics_.phase = SavePhase::Complete;
    return foundation::Result<SaveGameImage>::Success(std::move(image));
}
foundation::Result<SaveSection> SaveGameOrchestrator::MigrateToCurrent(SaveSection s, const ISaveParticipant &p)
{
    std::unordered_set<std::uint32_t> seen;
    while (s.schema_version < p.SchemaVersion())
    {
        if (!seen.insert(s.schema_version).second)
            return foundation::Result<SaveSection>::Failure(
                Error("gameplay.save.migration_cycle", "migration chain cycles"));
        auto it = migrations_.find({p.Id(), s.schema_version});
        if (it == migrations_.end())
            return foundation::Result<SaveSection>::Failure(
                Error("gameplay.save.migration_missing", "required save migration missing"));
        auto r = it->second->Migrate(s);
        if (!r)
            return r;
        s = std::move(r.Value());
        if (s.schema_version <= it->second->FromVersion())
            return foundation::Result<SaveSection>::Failure(
                Error("gameplay.save.migration_invalid_result", "migration did not advance schema"));
        ++diagnostics_.migrations;
    }
    if (s.schema_version != p.SchemaVersion())
        return foundation::Result<SaveSection>::Failure(
            Error("gameplay.save.schema_newer", "save section schema is newer than runtime"));
    s.payload_hash = HashBytes(s.payload);
    return foundation::Result<SaveSection>::Success(std::move(s));
}
foundation::Result<void> SaveGameOrchestrator::Restore(SaveGameImage image, const RestoreContext &c)
{
    diagnostics_.phase = SavePhase::Validating;
    if (c.compatibility == ContentCompatibilityPolicy::ExactRequired &&
        (image.manifest.content_version != c.current_content_version ||
         image.manifest.content_hash != c.current_content_hash))
    {
        ++diagnostics_.validation_failures;
        diagnostics_.phase = SavePhase::Failed;
        return foundation::Result<void>::Failure(
            Error("gameplay.save.content_mismatch", "save content does not match current content"));
    }
    auto order = ResolveOrder();
    if (!order)
    {
        diagnostics_.phase = SavePhase::Failed;
        return foundation::Result<void>::Failure(order.GetError());
    }
    std::unordered_map<SaveParticipantId, SaveSection, IdHash> sections;
    for (auto &s : image.sections)
    {
        if (!s.participant.IsValid() || sections.contains(s.participant) || HashBytes(s.payload) != s.payload_hash)
        {
            ++diagnostics_.validation_failures;
            diagnostics_.phase = SavePhase::Failed;
            return foundation::Result<void>::Failure(
                Error("gameplay.save.corrupt_section", "save section invalid or corrupt"));
        }
        sections.emplace(s.participant, std::move(s));
    }
    for (auto id : order.Value())
    {
        auto sit = sections.find(id);
        if (sit == sections.end())
        {
            ++diagnostics_.validation_failures;
            diagnostics_.phase = SavePhase::Failed;
            return foundation::Result<void>::Failure(
                Error("gameplay.save.section_missing", "save participant section missing"));
        }
        auto migrated = MigrateToCurrent(std::move(sit->second), *participants_.at(id));
        if (!migrated)
        {
            diagnostics_.phase = SavePhase::Failed;
            return foundation::Result<void>::Failure(migrated.GetError());
        }
        sit->second = std::move(migrated.Value());
        auto valid = participants_.at(id)->ValidateSnapshot(sit->second, c);
        if (!valid)
        {
            ++diagnostics_.validation_failures;
            diagnostics_.phase = SavePhase::Failed;
            return valid;
        }
    }
    diagnostics_.phase = SavePhase::Staging;
    struct StageRec
    {
        ISaveParticipant *p = nullptr;
        std::unique_ptr<IRestoreStage> stage;
    };
    std::vector<StageRec> stages;
    for (auto id : order.Value())
    {
        auto *p = participants_.at(id);
        auto staged = p->StageRestore(sections.at(id), c);
        if (!staged)
        {
            diagnostics_.phase = SavePhase::Failed;
            return foundation::Result<void>::Failure(staged.GetError());
        }
        stages.push_back({p, std::move(staged.Value())});
    }
    diagnostics_.phase = SavePhase::Committing;
    for (auto &rec : stages)
    {
        auto r = rec.p->CommitRestore(*rec.stage);
        if (!r)
        {
            diagnostics_.phase = SavePhase::Failed;
            return r;
        }
    }
    ++diagnostics_.restores;
    diagnostics_.phase = SavePhase::Complete;
    return foundation::Result<void>::Success();
}
} // namespace epidemic::gameplay::savegame
