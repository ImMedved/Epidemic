#include "Epidemic/GameFramework/Interaction/interaction.h"
#include <cstdlib>
#include <iostream>
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
    InteractionProviderId Id() const noexcept override
    {
        return id;
    }
    std::vector<InteractionCandidate> Collect(const InteractionContext &c) const override
    {
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
    epidemic::foundation::Result<void> Commit(const InteractionPlan &, InteractionExecutionId) override
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
    return 0;
}


