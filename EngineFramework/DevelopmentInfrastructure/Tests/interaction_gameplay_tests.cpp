#include "allocation_fault_injection.h"
#include "pre_state_verification.h"
#include "restore_fault_sweep.h"
#include "Epidemic/GameFramework/Interaction/interaction.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#define CHECK(expr)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
        {                                                                                                              \
            std::cerr << "CHECK failed at line " << __LINE__ << ": " #expr << "\\n";                              \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::interaction;
struct Provider final : IInteractionProvider
{
    InteractionTypeId type;
    InteractionProviderId id = InteractionProviderId::FromString("test.provider");
    bool throw_on_collect = false;
    InteractionProviderId Id() const noexcept override
    {
        return id;
    }
    std::vector<InteractionCandidate> Collect(const InteractionContext &c) const override
    {
        if (throw_on_collect) throw std::runtime_error("provider failed");
        return {{type, c.actor, c.target, id, 10, InteractionAvailability::Available, {}, {}}};
    }
};
struct Exec final : IInteractionExecutor
{
    int commits = 0;
    epidemic::foundation::Result<void> Validate(const InteractionPlan &) const override
    {
        return epidemic::foundation::Result<void>::Success();
    }
    epidemic::foundation::Result<void> Commit(const InteractionPlan &, InteractionExecutionId) noexcept override
    {
        ++commits;
        return epidemic::foundation::Result<void>::Success();
    }
};
struct State final : IInteractionStateProvider
{
    bool materialized = true;
    bool IsMaterialized(GameplayObjectRef) const override
    {
        return materialized;
    }
    Revision RevisionOf(GameplayObjectRef) const override
    {
        return Revision{2};
    }
};
int main()
{
    InteractionService s;
    auto type = InteractionTypeId::FromString("game.use");
    InteractionDefinition d;
    d.type = type;
    d.canonical_name = "game.use";
    d.mode = InteractionExecutionMode::Timed;
    d.duration = GameplayDuration{10};
    d.persistence = InteractionPersistence::PersistentSession;
    CHECK(s.RegisterDefinition(d));
    Provider p;
    p.type = type;
    Exec x;
    State st;
    CHECK(s.RegisterProvider(p));
    CHECK(s.RegisterExecutor(type, x));
    s.SetStateProvider(&st);
    s.Freeze();
    InteractionContext c;
    c.actor = {GameplayDomainId::FromString("a"), GameplayObjectId::FromString("a.1")};
    c.target = {GameplayDomainId::FromString("b"), GameplayObjectId::FromString("b.1")};
    c.gameplay.time = GameplayTimePoint{5};
    c.actor_revision = {2};
    c.target_revision = {2};
    const auto before_provider_failure = s.CurrentRevision();
    p.throw_on_collect = true;
    CHECK(s.GetAvailableInteractions(c).empty());
    CHECK(s.CurrentRevision() == before_provider_failure);
    p.throw_on_collect = false;

    auto cs = s.GetAvailableInteractions(c);
    CHECK(cs.size() == 1);
    auto plan = s.Prepare(c, cs[0]);
    CHECK(plan);
    CHECK(plan.Value().context.actor_revision == Revision{2});
    CHECK(plan.Value().context.target_revision == Revision{2});

    // B21 regression: materialization is volatile and must be rechecked at commit,
    // independently from gameplay revisions.
    st.materialized = false;
    CHECK(!s.Commit(plan.Value()));
    CHECK(x.commits == 0);
    st.materialized = true;

    auto result = s.Commit(plan.Value());
    CHECK(result);
    CHECK(result.Value().state == InteractionSessionState::Active);
    CHECK(x.commits == 0);
    CHECK(s.FindActive(c.actor).size() == 1);
    CHECK(!s.SweepTimed(GameplayTimePoint{15}));
    CHECK(x.commits == 0);
    GameplayContext completion_context = c.gameplay;
    completion_context.time = GameplayTimePoint{15};
    CHECK(s.Complete(result.Value().execution, completion_context));
    CHECK(x.commits == 1);
    CHECK(s.FindActive(c.actor).empty());
    const auto allocation_before = s.CaptureSnapshot();
    const auto restore_report = epidemic::tests::restore_fault::RunObservedRestoreSweep(
        "Interaction.RestoreSnapshot",
        2,
        [&] { return allocation_before; },
        [&](auto snapshot) { return s.RestoreSnapshot(std::move(snapshot)); },
        [&] { return s.CaptureSnapshot(); },
        [&](const epidemic::tests::allocation_fault::SweepIteration &iteration, const auto &allocation_baseline) {
            if (iteration.failure == epidemic::tests::allocation_fault::FailureKind::None)
                return true;
            const auto allocation_after = s.CaptureSnapshot();
            const auto diagnostics = s.GetDiagnostics();
            const auto pre_state = epidemic::tests::pre_state::StateComparator("Interaction.RestoreSnapshot.pre_state")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::PrimaryRecords,
                                                     allocation_baseline.sessions.size(), allocation_after.sessions.size(),
                                                     "session count")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::RecordPayloads,
                                                     allocation_baseline.sessions.size(), allocation_after.sessions.size(),
                                                     "session payload count")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::SecondaryIndexes,
                                                     s.FindActive(c.actor).size(), std::size_t{0},
                                                     "active session index")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::IdGenerators,
                                                     allocation_baseline.ids.next, allocation_after.ids.next,
                                                     "execution id generator next")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::Revisions,
                                                     allocation_baseline.revision, allocation_after.revision,
                                                     "revision")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::Journal,
                                                     s.ReadChangesSince(s.LatestChangeCursor()).changes.size(), std::size_t{0},
                                                     "latest cursor has no unread changes")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::JournalEpoch,
                                                     allocation_baseline.change_epoch, allocation_after.change_epoch,
                                                     "journal epoch")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::JournalSequence,
                                                     s.LatestChangeCursor(), s.LatestChangeCursor(),
                                                     "latest change cursor stable")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::JournalRetainedRecords,
                                                     diagnostics.sessions,
                                                     static_cast<std::uint64_t>(allocation_baseline.sessions.size()),
                                                     "retained session diagnostics")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::JournalLatestCursor,
                                                     s.LatestChangeCursor(), s.LatestChangeCursor(),
                                                     "latest cursor")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::PublicReadModels,
                                                     diagnostics.sessions,
                                                     static_cast<std::uint64_t>(allocation_baseline.sessions.size()),
                                                     "public diagnostics read model")
                                       .Finish();
            return pre_state.PassedAndCovers(epidemic::tests::pre_state::RequiredJournaledMutationFacets);
        });
    CHECK(epidemic::tests::restore_fault::PassedObservedRestoreSweep(restore_report));
    return 0;
}


