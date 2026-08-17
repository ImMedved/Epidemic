#include "Epidemic/GameFramework/NavigationSemantics/navigation_semantics.h"

#include <iterator>
#include <utility>

namespace epidemic::gameplay::navigation_semantics
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}
} // namespace
NavigationSemanticsService::NavigationSemanticsService()
    : layer_ids_(TypeId::FromString("framework.navigation.layer").Raw())
{
}
foundation::Result<void> NavigationSemanticsService::RegisterDomain(NavigationDomainDefinition d)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "navigation registry frozen"));
    if (!d.id.IsValid() || d.canonical_name.empty() || domains_.contains(d.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.navigation.invalid_domain", "invalid or duplicate navigation domain"));
    domains_.emplace(d.id, std::move(d));
    return foundation::Result<void>::Success();
}
foundation::Result<void> NavigationSemanticsService::RegisterRule(NavigationRuleDefinition r)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "navigation registry frozen"));
    if (!r.id.IsValid() || !domains_.contains(r.domain))
        return foundation::Result<void>::Failure(Error("gameplay.navigation.invalid_rule", "invalid navigation rule"));
    if (std::any_of(rules_.begin(), rules_.end(), [&](const auto &x) { return x.id == r.id; }))
        return foundation::Result<void>::Failure(
            Error("gameplay.navigation.duplicate_rule", "duplicate navigation rule"));
    rules_.push_back(std::move(r));
    std::sort(rules_.begin(), rules_.end(), [](const auto &a, const auto &b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        return a.id < b.id;
    });
    return foundation::Result<void>::Success();
}
foundation::Result<void> NavigationSemanticsService::SetProfile(NavigationSemanticProfile p, GameplayContext context)
{
    if (!p.subject.IsValid() || !domains_.contains(p.domain))
        return foundation::Result<void>::Failure(
            Error("gameplay.navigation.invalid_profile", "invalid navigation profile"));
    Bump();
    p.revision = revision_;
    profiles_[p.subject] = p;
    Record({0, NavigationChangeKind::ProfileChanged, p.subject, {}, {}, revision_, context});
    return foundation::Result<void>::Success();
}
foundation::Result<NavigationLayerId> NavigationSemanticsService::AddLayer(NavigationSemanticLayer l,
                                                                           GameplayContext context)
{
    if (!l.id.IsValid())
        l.id = NavigationLayerId{layer_ids_.Next()};
    if (!l.id.IsValid() || !l.area.IsValid() || !l.type.IsValid() || layers_.contains(l.id))
        return foundation::Result<NavigationLayerId>::Failure(
            Error("gameplay.navigation.invalid_layer", "invalid navigation layer"));
    Bump();
    l.revision = revision_;
    layers_.emplace(l.id, l);
    ++dynamic_updates_;
    Record({0, NavigationChangeKind::LayerAdded, {}, l.id, {}, revision_, context});
    return foundation::Result<NavigationLayerId>::Success(l.id);
}
foundation::Result<void> NavigationSemanticsService::RemoveLayer(NavigationLayerId id, GameplayContext context)
{
    auto it = layers_.find(id);
    if (it == layers_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.navigation.layer_missing", "navigation layer missing"));
    layers_.erase(it);
    Bump();
    ++dynamic_updates_;
    Record({0, NavigationChangeKind::LayerRemoved, {}, id, {}, revision_, context});
    return foundation::Result<void>::Success();
}
foundation::Result<void> NavigationSemanticsService::AddOrUpdateLink(NavigationSemanticLink link,
                                                                     GameplayContext context)
{
    if (!link.id.IsValid() || !link.from_area.IsValid() || !link.to_area.IsValid() || !link.type.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.navigation.invalid_link", "invalid navigation link"));
    Bump();
    link.revision = revision_;
    links_[link.id] = link;
    ++dynamic_updates_;
    Record({0, NavigationChangeKind::LinkChanged, {}, {}, link.id, revision_, context});
    return foundation::Result<void>::Success();
}
foundation::Result<void> NavigationSemanticsService::SetLinkState(NavigationLinkId id, LinkState state,
                                                                  GameplayContext context)
{
    auto it = links_.find(id);
    if (it == links_.end())
        return foundation::Result<void>::Failure(Error("gameplay.navigation.link_missing", "navigation link missing"));
    Bump();
    it->second.state = state;
    it->second.revision = revision_;
    ++dynamic_updates_;
    Record({0, NavigationChangeKind::LinkChanged, {}, {}, id, revision_, context});
    return foundation::Result<void>::Success();
}
NavigationPermissionResult NavigationSemanticsService::CanEnterArea(GameplayObjectRef subject,
                                                                    GameplayObjectRef area) const
{
    ++permission_queries_;
    auto result = EvaluateAreaForProfile(FindProfile(subject), area);
    if (result.decision == NavigationDecisionKind::Deny)
        ++denied_queries_;
    if (result.cost.additive_cost_micro != 0 || result.cost.base_multiplier_micro != 1'000'000)
        ++cost_modified_queries_;
    return result;
}
NavigationPermissionResult NavigationSemanticsService::CanUseLink(GameplayObjectRef subject,
                                                                  NavigationLinkId link_id) const
{
    ++permission_queries_;
    NavigationPermissionResult out;
    out.revision = revision_;
    auto link = links_.find(link_id);
    if (link == links_.end())
    {
        out.decision = NavigationDecisionKind::Deny;
        out.reasons.push_back(
            {NavigationReasonId::FromString("navigation.link_missing"), {}, {}, link_id, NavigationDecisionKind::Deny});
        ++denied_queries_;
        return out;
    }
    const auto &l = link->second;
    if (l.state != LinkState::Open && l.state != LinkState::Conditional)
    {
        out.decision = NavigationDecisionKind::Deny;
        out.reasons.push_back(
            {NavigationReasonId::FromString("navigation.link_blocked"), {}, {}, link_id, NavigationDecisionKind::Deny});
        ++denied_queries_;
        return out;
    }
    auto dest = CanEnterArea(subject, l.to_area);
    dest.reasons.insert(dest.reasons.begin(), out.reasons.begin(), out.reasons.end());
    return dest;
}
NavigationPermissionResult NavigationSemanticsService::EvaluatePath(NavigationPermissionQuery q) const
{
    auto r = CanEnterArea(q.subject, q.to_area);
    (void)q.from_area;
    (void)q.traversal_mode;
    (void)q.context;
    return r;
}
NavigationPermissionResult NavigationSemanticsService::EvaluateAreaForProfile(const NavigationSemanticProfile *profile,
                                                                              GameplayObjectRef area) const
{
    NavigationPermissionResult out;
    out.revision = revision_;
    out.decision = NavigationDecisionKind::Allow;
    if (!profile || !area.IsValid())
    {
        out.decision = NavigationDecisionKind::Deny;
        out.reasons.push_back(
            {NavigationReasonId::FromString("navigation.no_profile"), {}, {}, {}, NavigationDecisionKind::Deny});
        return out;
    }
    auto area_layers = FindLayersInArea(area);
    std::sort(area_layers.begin(), area_layers.end(), [](const auto &a, const auto &b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        return a.id < b.id;
    });
    for (const auto &layer : area_layers)
    {
        if (layer.decision == NavigationDecisionKind::Deny)
        {
            out.decision = NavigationDecisionKind::Deny;
            out.reasons.push_back({NavigationReasonId::FromString("navigation.layer_denied"),
                                   {},
                                   layer.id,
                                   {},
                                   NavigationDecisionKind::Deny});
            return out;
        }
        if (layer.decision == NavigationDecisionKind::AddCost || layer.decision == NavigationDecisionKind::Avoid)
        {
            out.cost.additive_cost_micro += layer.additive_cost_micro;
            out.cost.base_multiplier_micro = (out.cost.base_multiplier_micro * layer.multiplier_micro) / 1'000'000;
            out.reasons.push_back(
                {NavigationReasonId::FromString("navigation.layer_cost"), {}, layer.id, {}, layer.decision});
        }
    }
    for (const auto &rule : rules_)
    {
        if (rule.domain != profile->domain)
            continue;
        if (rule.decision == NavigationDecisionKind::Deny)
        {
            out.decision = NavigationDecisionKind::Deny;
            out.reasons.push_back(
                {rule.reason.IsValid() ? rule.reason : NavigationReasonId::FromString("navigation.rule_denied"),
                 rule.id,
                 {},
                 {},
                 NavigationDecisionKind::Deny});
            return out;
        }
        if (rule.decision == NavigationDecisionKind::AddCost || rule.decision == NavigationDecisionKind::Avoid)
        {
            out.cost.additive_cost_micro += rule.additive_cost_micro;
            out.cost.base_multiplier_micro = (out.cost.base_multiplier_micro * rule.multiplier_micro) / 1'000'000;
            out.reasons.push_back({rule.reason, rule.id, {}, {}, rule.decision});
        }
    }
    return out;
}
std::vector<NavigationSemanticLayer> NavigationSemanticsService::FindLayersInArea(GameplayObjectRef area) const
{
    std::vector<NavigationSemanticLayer> out;
    for (const auto &[id, l] : layers_)
    {
        (void)id;
        if (l.area == area)
            out.push_back(l);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
std::vector<NavigationSemanticLink> NavigationSemanticsService::FindLinksBetween(GameplayObjectRef from,
                                                                                 GameplayObjectRef to) const
{
    std::vector<NavigationSemanticLink> out;
    for (const auto &[id, l] : links_)
    {
        (void)id;
        if (l.from_area == from && l.to_area == to)
            out.push_back(l);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
const NavigationSemanticProfile *NavigationSemanticsService::FindProfile(GameplayObjectRef subject) const noexcept
{
    auto it = profiles_.find(subject);
    return it == profiles_.end() ? nullptr : &it->second;
}
const NavigationSemanticLink *NavigationSemanticsService::FindLink(NavigationLinkId id) const noexcept
{
    auto it = links_.find(id);
    return it == links_.end() ? nullptr : &it->second;
}
const NavigationSemanticLayer *NavigationSemanticsService::FindLayer(NavigationLayerId id) const noexcept
{
    auto it = layers_.find(id);
    return it == layers_.end() ? nullptr : &it->second;
}
NavigationSnapshot NavigationSemanticsService::CaptureSnapshot() const
{
    NavigationSnapshot s;
    for (const auto &[ref, p] : profiles_)
    {
        (void)ref;
        s.profiles.push_back(p);
    }
    for (const auto &[id, l] : layers_)
    {
        (void)id;
        if (l.lifetime == NavigationLayerLifetime::Persistent || l.lifetime == NavigationLayerLifetime::Session)
            s.layers.push_back(l);
    }
    for (const auto &[id, l] : links_)
    {
        (void)id;
        s.links.push_back(l);
    }
    std::sort(s.profiles.begin(), s.profiles.end(), [](auto &a, auto &b) { return a.subject < b.subject; });
    std::sort(s.layers.begin(), s.layers.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.links.begin(), s.links.end(), [](auto &a, auto &b) { return a.id < b.id; });
    s.layer_ids = layer_ids_.GetSnapshot();
    s.revision = revision_;
    return s;
}
foundation::Result<void> NavigationSemanticsService::RestoreSnapshot(NavigationSnapshot s)
{
    profiles_.clear();
    layers_.clear();
    links_.clear();
    for (const auto &p : s.profiles)
    {
        if (!p.subject.IsValid() || !domains_.contains(p.domain))
            return foundation::Result<void>::Failure(
                Error("gameplay.navigation.restore_invalid", "invalid profile snapshot"));
        profiles_.emplace(p.subject, p);
    }
    for (const auto &l : s.layers)
    {
        if (!l.id.IsValid() || !l.area.IsValid() || !l.type.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.navigation.restore_invalid", "invalid layer snapshot"));
        layers_.emplace(l.id, l);
    }
    for (const auto &l : s.links)
    {
        if (!l.id.IsValid() || !l.from_area.IsValid() || !l.to_area.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.navigation.restore_invalid", "invalid link snapshot"));
        links_.emplace(l.id, l);
    }
    layer_ids_.Restore(s.layer_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    return foundation::Result<void>::Success();
}
std::vector<NavigationChange> NavigationSemanticsService::ChangesSince(std::uint64_t seq) const
{
    std::vector<NavigationChange> out;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(out),
                 [seq](const auto &c) { return c.sequence > seq; });
    return out;
}
NavigationDiagnostics NavigationSemanticsService::GetDiagnostics() const noexcept
{
    return {domains_.size(),     rules_.size(),   layers_.size(),         links_.size(),
            permission_queries_, denied_queries_, cost_modified_queries_, dynamic_updates_};
}
void NavigationSemanticsService::Record(NavigationChange c)
{
    c.sequence = next_change_sequence_++;
    changes_.push_back(c);
}
} // namespace epidemic::gameplay::navigation_semantics
