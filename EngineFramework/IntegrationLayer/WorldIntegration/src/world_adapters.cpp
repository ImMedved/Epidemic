#include "Epidemic/GameFramework/WorldIntegration/world_adapters.h"

namespace epidemic::gameplay::world_integration
{
foundation::Result<void> WorldQueryAdapter::RegisterProviders()
{
    const queries::QueryProviderCapabilities caps{true, false, false, false};
    auto a = queries_.RegisterProvider<AreasAtPositionQuery>(
        "framework.query.world.areas_at", caps, [this](const AreasAtPositionQuery &q, const queries::QueryContext &) {
            auto v = world_.FindAreasAt(q.position);
            const auto n = v.size();
            return foundation::Result<queries::QueryResponse<AreasAtPositionQuery::ResultType>>::Success(
                {std::move(v), {{}, world_.CurrentRevision(), queries::QueryCoverage::Complete, n, n, true}});
        });
    if (!a)
        return a;
    auto b = queries_.RegisterProvider<AlterationsInAreaQuery>(
        "framework.query.world.alterations", caps,
        [this](const AlterationsInAreaQuery &q, const queries::QueryContext &) {
            auto v = world_.FindAlterations(q.area, q.type);
            auto n = v.size();
            return foundation::Result<queries::QueryResponse<AlterationsInAreaQuery::ResultType>>::Success(
                {std::move(v), {{}, world_.CurrentRevision(), queries::QueryCoverage::Complete, n, n, true}});
        });
    if (!b)
        return b;
    auto c = queries_.RegisterProvider<EnvironmentSampleQuery>(
        "framework.query.environment.sample", caps,
        [this](const EnvironmentSampleQuery &q, const queries::QueryContext &) {
            auto v = environment_.Sample(q.position, q.time);
            return foundation::Result<queries::QueryResponse<EnvironmentSampleQuery::ResultType>>::Success(
                {std::move(v), {{}, environment_.CurrentRevision(), queries::QueryCoverage::Complete, 1, 1, true}});
        });
    if (!c)
        return c;
    return queries_.RegisterProvider<ActiveInteractionsQuery>(
        "framework.query.interaction.active", caps,
        [this](const ActiveInteractionsQuery &q, const queries::QueryContext &) {
            auto v = interaction_.FindActive(q.actor);
            auto n = v.size();
            return foundation::Result<queries::QueryResponse<ActiveInteractionsQuery::ResultType>>::Success(
                {std::move(v), {{}, {}, queries::QueryCoverage::Complete, n, n, true}});
        });
}
foundation::Result<void> WorldFactsAdapter::RegisterContracts()
{
    auto w = facts_.RegisterEventType<world::WorldChange>("framework.world.changed", world::WorldService::Domain(),
                                                          facts::HistoryPolicy::Recent, 4096);
    if (!w)
        return foundation::Result<void>::Failure(w.GetError());
    world_event_ = w.Value();
    auto e = facts_.RegisterEventType<environment::EnvironmentChange>(
        "framework.environment.changed", environment::EnvironmentService::Domain(), facts::HistoryPolicy::Recent, 4096);
    if (!e)
        return foundation::Result<void>::Failure(e.GetError());
    environment_event_ = e.Value();
    auto i = facts_.RegisterEventType<interaction::InteractionChange>(
        "framework.interaction.changed", interaction::InteractionService::Domain(), facts::HistoryPolicy::Recent, 4096);
    if (!i)
        return foundation::Result<void>::Failure(i.GetError());
    interaction_event_ = i.Value();
    return foundation::Result<void>::Success();
}
foundation::Result<std::uint64_t> WorldFactsAdapter::PublishPending(GameplayContext context)
{
    std::uint64_t n = 0;
    const auto producer = ProducerId::FromString("framework.world_integration");
    for (const auto &c : world_.ChangesSince(wc_))
    {
        auto r = facts_.Publish(world_event_, c.context.tick.IsValid() ? c.context : context,
                                GameplayObjectRef{world::WorldService::Domain(),
                                                  c.alteration.IsValid() ? c.alteration.value : c.feature.value},
                                c, producer);
        if (!r)
            return foundation::Result<std::uint64_t>::Failure(r.GetError());
        wc_ = c.sequence;
        ++n;
    }
    for (const auto &c : environment_.ChangesSince(ec_))
    {
        auto r =
            facts_.Publish(environment_event_, c.context.tick.IsValid() ? c.context : context,
                           GameplayObjectRef{environment::EnvironmentService::Domain(), c.layer.value}, c, producer);
        if (!r)
            return foundation::Result<std::uint64_t>::Failure(r.GetError());
        ec_ = c.sequence;
        ++n;
    }
    for (const auto &c : interaction_.ChangesSince(ic_))
    {
        auto r =
            facts_.Publish(interaction_event_, c.context.tick.IsValid() ? c.context : context, c.actor, c, producer);
        if (!r)
            return foundation::Result<std::uint64_t>::Failure(r.GetError());
        ic_ = c.sequence;
        ++n;
    }
    return foundation::Result<std::uint64_t>::Success(n);
}
bool EntityInteractionStateProvider::IsMaterialized(GameplayObjectRef object) const
{
    auto id = entities::EntityService::FromGameplayObjectRef(object);
    auto *r = entities_.Find(id);
    return r && r->materialization == entities::EntityMaterializationState::Materialized;
}
Revision EntityInteractionStateProvider::RevisionOf(GameplayObjectRef object) const
{
    auto id = entities::EntityService::FromGameplayObjectRef(object);
    auto *r = entities_.Find(id);
    return r ? r->revision : Revision{};
}
} // namespace epidemic::gameplay::world_integration
