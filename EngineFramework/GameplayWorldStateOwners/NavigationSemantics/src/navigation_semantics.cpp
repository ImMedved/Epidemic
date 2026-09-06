#include "Epidemic/GameFramework/NavigationSemantics/navigation_semantics.h"

#include "Epidemic/Foundation/error.h"

#include <iterator>
#include <limits>
#include <utility>

namespace epidemic::gameplay::navigation_semantics
{
namespace
{
foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

[[nodiscard]] Fixed AddSat(Fixed a, Fixed b) noexcept
{
    if (b > 0 && a > std::numeric_limits<Fixed>::max() - b)
        return std::numeric_limits<Fixed>::max();
    if (b < 0 && a < std::numeric_limits<Fixed>::min() - b)
        return std::numeric_limits<Fixed>::min();
    return a + b;
}

[[nodiscard]] Fixed MulSat(Fixed a, Fixed b) noexcept
{
    if (a == 0 || b == 0)
        return 0;
    if ((a == -1 && b == std::numeric_limits<Fixed>::min()) ||
        (b == -1 && a == std::numeric_limits<Fixed>::min()))
        return std::numeric_limits<Fixed>::max();
    if (a > 0)
    {
        if (b > 0 && a > std::numeric_limits<Fixed>::max() / b)
            return std::numeric_limits<Fixed>::max();
        if (b < 0 && b < std::numeric_limits<Fixed>::min() / a)
            return std::numeric_limits<Fixed>::min();
    }
    else
    {
        if (b > 0 && a < std::numeric_limits<Fixed>::min() / b)
            return std::numeric_limits<Fixed>::min();
        if (b < 0 && a < std::numeric_limits<Fixed>::max() / b)
            return std::numeric_limits<Fixed>::max();
    }
    return a * b;
}

[[nodiscard]] Fixed MulMicro(Fixed a, Fixed b) noexcept
{
    constexpr Fixed scale = 1'000'000;
    const auto aq = a / scale;
    const auto ar = a % scale;
    const auto bq = b / scale;
    const auto br = b % scale;
    auto result = MulSat(MulSat(aq, bq), scale);
    result = AddSat(result, MulSat(aq, br));
    result = AddSat(result, MulSat(ar, bq));
    result = AddSat(result, MulSat(ar, br) / scale);
    return result;
}

[[nodiscard]] bool HasAllExact(const GameplayTagSet& candidate, const GameplayTagSet& required) noexcept
{
    for (const auto tag : required.Values())
        if (!candidate.HasExact(tag))
            return false;
    return true;
}

[[nodiscard]] bool LayersContainAll(const std::vector<NavigationSemanticLayer>& layers,
                                    const GameplayTagSet& required) noexcept
{
    if (required.Values().empty())
        return true;
    GameplayTagSet aggregate;
    for (const auto& layer : layers)
        for (const auto tag : layer.tags.Values())
            aggregate.Add(tag);
    return HasAllExact(aggregate, required);
}

void ApplyCost(NavigationSemanticCost& cost, Fixed additive, Fixed multiplier) noexcept
{
    cost.additive_cost_micro = AddSat(cost.additive_cost_micro, additive);
    cost.base_multiplier_micro = MulMicro(cost.base_multiplier_micro, multiplier);
}

[[nodiscard]] bool IsExpired(const NavigationSemanticLayer& layer, const GameplayContext& context) noexcept
{
    return layer.lifetime == NavigationLayerLifetime::Timed && layer.expires_at &&
           layer.expires_at->ticks <= context.time.ticks;
}

[[nodiscard]] bool IsValidLayer(const NavigationSemanticLayer& layer) noexcept
{
    return layer.id.IsValid() && layer.area.IsValid() && layer.type.IsValid() && layer.multiplier_micro >= 0 &&
           layer.required_parameter_micro >= 0 &&
           (layer.lifetime != NavigationLayerLifetime::Timed || layer.expires_at.has_value()) &&
           ((layer.decision != NavigationDecisionKind::RequireCapability &&
             layer.decision != NavigationDecisionKind::RequireFact) ||
            layer.requirement.IsValid());
}

[[nodiscard]] bool IsValidLink(const NavigationSemanticLink& link) noexcept
{
    return link.id.IsValid() && link.from_area.IsValid() && link.to_area.IsValid() && link.type.IsValid() &&
           link.from_area != link.to_area &&
           (link.state != LinkState::Conditional || link.required_fact.IsValid());
}
}

NavigationSemanticsService::NavigationSemanticsService()
    : layer_ids_(TypeId::FromString("framework.navigation.layer").Raw())
{
}

foundation::Result<void> NavigationSemanticsService::RegisterDomain(NavigationDomainDefinition definition)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "navigation registry frozen"));
    if (definition.canonical_name.empty())
        return foundation::Result<void>::Failure(
            Error("gameplay.navigation.invalid_domain", "navigation domain name is required"));
    const auto expected = NavigationDomainId::FromString(definition.canonical_name);
    if (!definition.id.IsValid())
        definition.id = expected;
    if (definition.id != expected || domains_.contains(definition.id))
        return foundation::Result<void>::Failure(
            Error("gameplay.navigation.invalid_domain", "invalid or duplicate navigation domain"));
    domains_.emplace(definition.id, std::move(definition));
    return foundation::Result<void>::Success();
}

foundation::Result<void> NavigationSemanticsService::RegisterRule(NavigationRuleDefinition rule)
{
    if (frozen_)
        return foundation::Result<void>::Failure(Error("gameplay.registry_frozen", "navigation registry frozen"));
    if (!rule.id.IsValid() || !domains_.contains(rule.domain) || rule.multiplier_micro < 0 ||
        rule.required_parameter_micro < 0)
        return foundation::Result<void>::Failure(Error("gameplay.navigation.invalid_rule", "invalid navigation rule"));
    if ((rule.decision == NavigationDecisionKind::RequireCapability || rule.decision == NavigationDecisionKind::RequireFact) &&
        !rule.requirement.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.navigation.invalid_rule", "requirement rule must declare a requirement id"));
    if (std::any_of(rules_.begin(), rules_.end(), [&](const auto& current) { return current.id == rule.id; }))
        return foundation::Result<void>::Failure(Error("gameplay.navigation.duplicate_rule", "duplicate navigation rule"));
    rules_.push_back(std::move(rule));
    std::sort(rules_.begin(), rules_.end(), [](const auto& a, const auto& b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        return a.id < b.id;
    });
    return foundation::Result<void>::Success();
}

foundation::Result<void> NavigationSemanticsService::SetProfile(NavigationSemanticProfile profile,
                                                                 GameplayContext context)
{
    if (!profile.subject.IsValid() || !domains_.contains(profile.domain))
        return foundation::Result<void>::Failure(Error("gameplay.navigation.invalid_profile", "invalid navigation profile"));
    Bump();
    profile.revision = revision_;
    profiles_[profile.subject] = profile;
    Record({0, NavigationChangeKind::ProfileChanged, profile.subject, {}, {}, revision_, context});
    return foundation::Result<void>::Success();
}

foundation::Result<void> NavigationSemanticsService::ValidateAndAdvanceLayerId(NavigationLayerId id)
{
    if (!id.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.navigation.invalid_layer", "invalid navigation layer id"));
    const auto snapshot = layer_ids_.GetSnapshot();
    if (id.value.High() != snapshot.scope || snapshot.next == 0 || id.value.Low() < snapshot.next)
        return foundation::Result<void>::Success();
    auto advanced = snapshot;
    advanced.next = id.value.Low() == std::numeric_limits<std::uint64_t>::max() ? 0 : id.value.Low() + 1;
    layer_ids_.Restore(advanced);
    return foundation::Result<void>::Success();
}

void NavigationSemanticsService::IndexLayer(const NavigationSemanticLayer& layer)
{
    auto& ids = layer_ids_by_area_[layer.area];
    const auto pos = std::lower_bound(ids.begin(), ids.end(), layer.id);
    if (pos == ids.end() || *pos != layer.id)
        ids.insert(pos, layer.id);
}

void NavigationSemanticsService::UnindexLayer(const NavigationSemanticLayer& layer)
{
    const auto it = layer_ids_by_area_.find(layer.area);
    if (it == layer_ids_by_area_.end())
        return;
    auto& ids = it->second;
    const auto pos = std::lower_bound(ids.begin(), ids.end(), layer.id);
    if (pos != ids.end() && *pos == layer.id)
        ids.erase(pos);
    if (ids.empty())
        layer_ids_by_area_.erase(it);
}

void NavigationSemanticsService::IndexLink(const NavigationSemanticLink& link)
{
    auto& ids = link_ids_by_pair_[LinkPairKey{link.from_area, link.to_area}];
    const auto pos = std::lower_bound(ids.begin(), ids.end(), link.id);
    if (pos == ids.end() || *pos != link.id)
        ids.insert(pos, link.id);
}

void NavigationSemanticsService::UnindexLink(const NavigationSemanticLink& link)
{
    const LinkPairKey key{link.from_area, link.to_area};
    const auto it = link_ids_by_pair_.find(key);
    if (it == link_ids_by_pair_.end())
        return;
    auto& ids = it->second;
    const auto pos = std::lower_bound(ids.begin(), ids.end(), link.id);
    if (pos != ids.end() && *pos == link.id)
        ids.erase(pos);
    if (ids.empty())
        link_ids_by_pair_.erase(it);
}

void NavigationSemanticsService::RebuildIndexes()
{
    layer_ids_by_area_.clear();
    link_ids_by_pair_.clear();
    for (const auto& [id, layer] : layers_)
    {
        (void)id;
        IndexLayer(layer);
    }
    for (const auto& [id, link] : links_)
    {
        (void)id;
        IndexLink(link);
    }
}

foundation::Result<NavigationLayerId> NavigationSemanticsService::AddLayer(NavigationSemanticLayer layer,
                                                                           GameplayContext context)
{
    const bool caller_supplied_id = layer.id.IsValid();
    if (!caller_supplied_id)
        layer.id = NavigationLayerId{layer_ids_.Next()};
    if (!IsValidLayer(layer) || layers_.contains(layer.id))
        return foundation::Result<NavigationLayerId>::Failure(
            Error("gameplay.navigation.invalid_layer", "invalid navigation layer"));
    if (caller_supplied_id)
    {
        auto advanced = ValidateAndAdvanceLayerId(layer.id);
        if (!advanced)
            return foundation::Result<NavigationLayerId>::Failure(advanced.GetError());
    }
    Bump();
    layer.revision = revision_;
    const auto id = layer.id;
    auto [it, inserted] = layers_.emplace(id, std::move(layer));
    (void)inserted;
    IndexLayer(it->second);
    ++dynamic_updates_;
    Record({0, NavigationChangeKind::LayerAdded, {}, id, {}, revision_, context});
    return foundation::Result<NavigationLayerId>::Success(id);
}

foundation::Result<void> NavigationSemanticsService::UpdateLayer(NavigationLayerId id,
                                                                  NavigationSemanticLayer replacement,
                                                                  Revision expected_revision,
                                                                  GameplayContext context)
{
    const auto it = layers_.find(id);
    if (it == layers_.end())
        return foundation::Result<void>::Failure(Error("gameplay.navigation.layer_missing", "navigation layer missing"));
    if (it->second.revision != expected_revision)
        return foundation::Result<void>::Failure(Error("gameplay.navigation.layer_stale", "navigation layer revision changed"));
    replacement.id = it->second.id;
    replacement.source = it->second.source;
    replacement.lifetime = it->second.lifetime;
    if (!IsValidLayer(replacement))
        return foundation::Result<void>::Failure(Error("gameplay.navigation.invalid_layer", "invalid navigation layer update"));
    const auto old = it->second;
    Bump();
    replacement.revision = revision_;
    UnindexLayer(old);
    it->second = std::move(replacement);
    IndexLayer(it->second);
    ++dynamic_updates_;
    Record({0, NavigationChangeKind::LayerChanged, {}, id, {}, revision_, context});
    return foundation::Result<void>::Success();
}

foundation::Result<void> NavigationSemanticsService::RemoveLayer(NavigationLayerId id, GameplayContext context)
{
    const auto it = layers_.find(id);
    if (it == layers_.end())
        return foundation::Result<void>::Failure(Error("gameplay.navigation.layer_missing", "navigation layer missing"));
    UnindexLayer(it->second);
    layers_.erase(it);
    Bump();
    ++dynamic_updates_;
    Record({0, NavigationChangeKind::LayerRemoved, {}, id, {}, revision_, context});
    return foundation::Result<void>::Success();
}

std::uint64_t NavigationSemanticsService::RemoveLayersBySource(GameplayObjectRef source, GameplayContext context)
{
    if (!source.IsValid())
        return 0;
    std::vector<NavigationLayerId> ids;
    ids.reserve(layers_.size());
    for (const auto& [id, layer] : layers_)
        if (layer.source == source)
            ids.push_back(id);
    std::sort(ids.begin(), ids.end());
    for (const auto id : ids)
        (void)RemoveLayer(id, context);
    return ids.size();
}

std::uint64_t NavigationSemanticsService::ExpireLayers(GameplayTimePoint now, GameplayContext context)
{
    std::vector<NavigationLayerId> expired;
    for (const auto& [id, layer] : layers_)
        if (layer.lifetime == NavigationLayerLifetime::Timed && layer.expires_at && layer.expires_at->ticks <= now.ticks)
            expired.push_back(id);
    std::sort(expired.begin(), expired.end());
    for (const auto id : expired)
        (void)RemoveLayer(id, context);
    return expired.size();
}

foundation::Result<void> NavigationSemanticsService::AddOrUpdateLink(NavigationSemanticLink link,
                                                                     GameplayContext context)
{
    if (!IsValidLink(link))
        return foundation::Result<void>::Failure(Error("gameplay.navigation.invalid_link", "invalid navigation link"));
    const auto existing = links_.find(link.id);
    if (existing != links_.end())
        UnindexLink(existing->second);
    Bump();
    link.revision = revision_;
    links_[link.id] = link;
    IndexLink(links_.at(link.id));
    ++dynamic_updates_;
    Record({0, NavigationChangeKind::LinkChanged, {}, {}, link.id, revision_, context});
    return foundation::Result<void>::Success();
}

foundation::Result<void> NavigationSemanticsService::RemoveLink(NavigationLinkId id, GameplayContext context)
{
    const auto it = links_.find(id);
    if (it == links_.end())
        return foundation::Result<void>::Failure(Error("gameplay.navigation.link_missing", "navigation link missing"));
    UnindexLink(it->second);
    links_.erase(it);
    Bump();
    ++dynamic_updates_;
    Record({0, NavigationChangeKind::LinkChanged, {}, {}, id, revision_, context});
    return foundation::Result<void>::Success();
}

foundation::Result<void> NavigationSemanticsService::SetLinkState(NavigationLinkId id, LinkState state,
                                                                  GameplayContext context)
{
    const auto it = links_.find(id);
    if (it == links_.end())
        return foundation::Result<void>::Failure(Error("gameplay.navigation.link_missing", "navigation link missing"));
    if (state == LinkState::Conditional && !it->second.required_fact.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.navigation.invalid_link", "conditional link requires a fact id"));
    Bump();
    it->second.state = state;
    it->second.revision = revision_;
    ++dynamic_updates_;
    Record({0, NavigationChangeKind::LinkChanged, {}, {}, id, revision_, context});
    return foundation::Result<void>::Success();
}

NavigationPermissionResult NavigationSemanticsService::CanEnterArea(GameplayObjectRef subject,
                                                                    GameplayObjectRef area,
                                                                    TraversalModeSemanticId traversal_mode,
                                                                    const GameplayContext& context) const
{
    NavigationPermissionQuery query;
    query.subject = subject;
    query.to_area = area;
    query.traversal_mode = traversal_mode;
    query.context = context;
    return EvaluatePermission(query, std::nullopt);
}

NavigationPermissionResult NavigationSemanticsService::CanUseLink(GameplayObjectRef subject,
                                                                  NavigationLinkId link_id,
                                                                  TraversalModeSemanticId traversal_mode,
                                                                  const GameplayContext& context) const
{
    const auto link = links_.find(link_id);
    if (link == links_.end())
    {
        ++permission_queries_;
        ++denied_queries_;
        NavigationPermissionResult result;
        result.decision = NavigationDecisionKind::Deny;
        result.availability = NavigationAvailability::PhysicallyImpossible;
        result.revision = revision_;
        result.reasons.push_back({NavigationReasonId::FromString("navigation.link_missing"), {}, {}, link_id,
                                  NavigationDecisionKind::Deny});
        return result;
    }
    NavigationPermissionQuery query;
    query.subject = subject;
    query.from_area = link->second.from_area;
    query.to_area = link->second.to_area;
    query.traversal_mode = traversal_mode;
    query.context = context;
    return EvaluatePermission(query, link_id);
}

NavigationPermissionResult NavigationSemanticsService::EvaluatePath(NavigationPermissionQuery query) const
{
    return EvaluatePermission(query, std::nullopt);
}

NavigationPermissionResult NavigationSemanticsService::EvaluatePermission(const NavigationPermissionQuery& query,
                                                                            std::optional<NavigationLinkId> required_link) const
{
    ++permission_queries_;
    NavigationPermissionResult result;
    result.revision = revision_;
    if (!query.subject.IsValid() || !query.to_area.IsValid())
    {
        result.decision = NavigationDecisionKind::Deny;
        result.availability = NavigationAvailability::PhysicallyImpossible;
        result.reasons.push_back({NavigationReasonId::FromString("navigation.path_invalid"), {}, {}, {},
                                  NavigationDecisionKind::Deny});
        ++denied_queries_;
        return result;
    }

    result = EvaluateAreaForProfile(FindProfile(query.subject), query.from_area, query.to_area,
                                    query.traversal_mode, query.context);
    if (result.availability != NavigationAvailability::Available && result.availability != NavigationAvailability::Unsafe)
    {
        ++denied_queries_;
        return result;
    }

    std::vector<NavigationSemanticLink> candidates;
    if (required_link)
    {
        const auto it = links_.find(*required_link);
        if (it == links_.end() || it->second.from_area != query.from_area || it->second.to_area != query.to_area)
        {
            result.decision = NavigationDecisionKind::Deny;
            result.availability = NavigationAvailability::PhysicallyImpossible;
            result.reasons.push_back({NavigationReasonId::FromString("navigation.link_missing"), {}, {},
                                      required_link.value_or(NavigationLinkId{}), NavigationDecisionKind::Deny});
            ++denied_queries_;
            return result;
        }
        candidates.push_back(it->second);
    }
    else if (query.from_area.IsValid())
    {
        candidates = FindLinksBetween(query.from_area, query.to_area);
    }

    if (!candidates.empty())
    {
        bool usable = false;
        NavigationAvailability strongest_failure = NavigationAvailability::PhysicallyImpossible;
        NavigationReason failure_reason{};
        for (const auto& link : candidates)
        {
            if (link.state == LinkState::Open ||
                (link.state == LinkState::Conditional && facts_ != nullptr &&
                 facts_->HasFact(query.subject, link.required_fact, query.context)))
            {
                usable = true;
                break;
            }
            if (link.state == LinkState::Locked || link.state == LinkState::Conditional)
            {
                strongest_failure = NavigationAvailability::Forbidden;
                failure_reason = {NavigationReasonId::FromString("navigation.link_forbidden"), {}, {}, link.id,
                                  link.state == LinkState::Conditional ? NavigationDecisionKind::RequireFact
                                                                       : NavigationDecisionKind::Deny};
            }
            else if (strongest_failure != NavigationAvailability::Forbidden && link.state != LinkState::Destroyed)
            {
                strongest_failure = NavigationAvailability::TemporarilyBlocked;
                failure_reason = {NavigationReasonId::FromString("navigation.link_blocked"), {}, {}, link.id,
                                  NavigationDecisionKind::Deny};
            }
            else if (!failure_reason.id.IsValid())
            {
                failure_reason = {NavigationReasonId::FromString("navigation.link_destroyed"), {}, {}, link.id,
                                  NavigationDecisionKind::Deny};
            }
        }
        if (!usable)
        {
            result.decision = failure_reason.decision;
            result.availability = strongest_failure;
            result.reasons.push_back(failure_reason);
            ++denied_queries_;
            return result;
        }
    }

    if (result.cost.additive_cost_micro != 0 || result.cost.base_multiplier_micro != 1'000'000)
        ++cost_modified_queries_;
    return result;
}

NavigationPermissionResult NavigationSemanticsService::EvaluateAreaForProfile(
    const NavigationSemanticProfile* profile, GameplayObjectRef from_area, GameplayObjectRef area,
    TraversalModeSemanticId traversal_mode, const GameplayContext& context) const
{
    NavigationPermissionResult result;
    result.revision = revision_;
    if (!profile || !area.IsValid())
    {
        result.decision = NavigationDecisionKind::Deny;
        result.availability = NavigationAvailability::TraversalUnsupported;
        result.reasons.push_back({NavigationReasonId::FromString("navigation.no_profile"), {}, {}, {},
                                  NavigationDecisionKind::Deny});
        return result;
    }

    auto destination_layers = FindLayersInArea(area);
    auto source_layers = from_area.IsValid() ? FindLayersInArea(from_area) : std::vector<NavigationSemanticLayer>{};
    const auto live_layer = [&](const NavigationSemanticLayer& layer) { return !IsExpired(layer, context); };
    std::erase_if(destination_layers, [&](const auto& layer) { return !live_layer(layer); });
    std::erase_if(source_layers, [&](const auto& layer) { return !live_layer(layer); });
    std::sort(destination_layers.begin(), destination_layers.end(), [](const auto& a, const auto& b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        return a.id < b.id;
    });

    const auto apply_requirement = [&](NavigationDecisionKind decision, TypeId requirement, Fixed parameter,
                                       NavigationReason reason) -> bool {
        if (decision == NavigationDecisionKind::RequireCapability)
        {
            if (capabilities_ == nullptr || !capabilities_->HasCapability(profile->subject, requirement, parameter))
            {
                result.decision = decision;
                result.availability = NavigationAvailability::TraversalUnsupported;
                result.reasons.push_back(reason);
                return false;
            }
        }
        else if (decision == NavigationDecisionKind::RequireFact)
        {
            if (facts_ == nullptr || !facts_->HasFact(profile->subject, requirement, context))
            {
                result.decision = decision;
                result.availability = NavigationAvailability::Forbidden;
                result.reasons.push_back(reason);
                return false;
            }
        }
        return true;
    };

    for (const auto& layer : destination_layers)
    {
        const NavigationReason reason{NavigationReasonId::FromString("navigation.layer_rule"), {}, layer.id, {}, layer.decision};
        if (layer.decision == NavigationDecisionKind::Deny)
        {
            result.decision = NavigationDecisionKind::Deny;
            result.availability = (layer.lifetime == NavigationLayerLifetime::Transient ||
                                   layer.lifetime == NavigationLayerLifetime::Timed)
                                      ? NavigationAvailability::TemporarilyBlocked
                                      : NavigationAvailability::Forbidden;
            result.reasons.push_back(reason);
            return result;
        }
        if (!apply_requirement(layer.decision, layer.requirement, layer.required_parameter_micro, reason))
            return result;
        if (layer.decision == NavigationDecisionKind::AddCost || layer.decision == NavigationDecisionKind::Avoid ||
            layer.decision == NavigationDecisionKind::Prefer)
        {
            ApplyCost(result.cost, layer.additive_cost_micro, layer.multiplier_micro);
            result.reasons.push_back(reason);
            if (layer.decision == NavigationDecisionKind::Avoid)
                result.availability = NavigationAvailability::Unsafe;
        }
    }

    for (const auto& rule : rules_)
    {
        if (rule.domain != profile->domain || !HasAllExact(profile->navigation_tags, rule.required_subject_tags) ||
            !LayersContainAll(source_layers, rule.required_from_layer_tags) ||
            !LayersContainAll(destination_layers, rule.required_layer_tags) ||
            (rule.required_traversal_mode.IsValid() && rule.required_traversal_mode != traversal_mode))
            continue;

        const NavigationReason reason{rule.reason.IsValid() ? rule.reason : NavigationReasonId::FromString("navigation.rule"),
                                      rule.id, {}, {}, rule.decision};
        if (rule.decision == NavigationDecisionKind::Deny)
        {
            result.decision = NavigationDecisionKind::Deny;
            result.availability = NavigationAvailability::Forbidden;
            result.reasons.push_back(reason);
            return result;
        }
        if (!apply_requirement(rule.decision, rule.requirement, rule.required_parameter_micro, reason))
            return result;
        if (rule.decision == NavigationDecisionKind::AddCost || rule.decision == NavigationDecisionKind::Avoid ||
            rule.decision == NavigationDecisionKind::Prefer)
        {
            ApplyCost(result.cost, rule.additive_cost_micro, rule.multiplier_micro);
            result.reasons.push_back(reason);
            if (rule.decision == NavigationDecisionKind::Avoid)
                result.availability = NavigationAvailability::Unsafe;
        }
    }
    return result;
}

std::vector<NavigationSemanticLayer> NavigationSemanticsService::FindLayersInArea(GameplayObjectRef area) const
{
    std::vector<NavigationSemanticLayer> result;
    const auto indexed = layer_ids_by_area_.find(area);
    if (indexed == layer_ids_by_area_.end())
        return result;
    result.reserve(indexed->second.size());
    for (const auto id : indexed->second)
    {
        const auto it = layers_.find(id);
        if (it != layers_.end())
            result.push_back(it->second);
    }
    return result;
}

std::vector<NavigationSemanticLink> NavigationSemanticsService::FindLinksBetween(GameplayObjectRef from,
                                                                                 GameplayObjectRef to) const
{
    std::vector<NavigationSemanticLink> result;
    const auto indexed = link_ids_by_pair_.find(LinkPairKey{from, to});
    if (indexed == link_ids_by_pair_.end())
        return result;
    result.reserve(indexed->second.size());
    for (const auto id : indexed->second)
    {
        const auto it = links_.find(id);
        if (it != links_.end())
            result.push_back(it->second);
    }
    return result;
}

const NavigationSemanticProfile* NavigationSemanticsService::FindProfile(GameplayObjectRef subject) const noexcept
{
    const auto it = profiles_.find(subject);
    return it == profiles_.end() ? nullptr : &it->second;
}
const NavigationSemanticLink* NavigationSemanticsService::FindLink(NavigationLinkId id) const noexcept
{
    const auto it = links_.find(id);
    return it == links_.end() ? nullptr : &it->second;
}
const NavigationSemanticLayer* NavigationSemanticsService::FindLayer(NavigationLayerId id) const noexcept
{
    const auto it = layers_.find(id);
    return it == layers_.end() ? nullptr : &it->second;
}

NavigationSnapshot NavigationSemanticsService::CaptureSnapshot() const
{
    NavigationSnapshot snapshot;
    for (const auto& [ref, profile] : profiles_)
    {
        (void)ref;
        snapshot.profiles.push_back(profile);
    }
    for (const auto& [id, layer] : layers_)
    {
        (void)id;
        if (layer.lifetime != NavigationLayerLifetime::Transient && layer.lifetime != NavigationLayerLifetime::Session)
            snapshot.layers.push_back(layer);
    }
    for (const auto& [id, link] : links_)
    {
        (void)id;
        snapshot.links.push_back(link);
    }
    std::sort(snapshot.profiles.begin(), snapshot.profiles.end(), [](const auto& a, const auto& b) { return a.subject < b.subject; });
    std::sort(snapshot.layers.begin(), snapshot.layers.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(snapshot.links.begin(), snapshot.links.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    snapshot.layer_ids = layer_ids_.GetSnapshot();
    snapshot.revision = revision_;
    snapshot.journal.assign(changes_.begin(), changes_.end());
    snapshot.next_change_sequence = next_change_sequence_;
    return snapshot;
}

foundation::Result<void> NavigationSemanticsService::RestoreSnapshot(NavigationSnapshot snapshot)
{
    std::unordered_map<GameplayObjectRef, NavigationSemanticProfile, RefHash> restored_profiles;
    std::unordered_map<NavigationLayerId, NavigationSemanticLayer, IdHash> restored_layers;
    std::unordered_map<NavigationLinkId, NavigationSemanticLink, IdHash> restored_links;
    std::deque<NavigationChange> restored_changes;
    if (snapshot.journal.size() > kChangeJournalCapacity || snapshot.next_change_sequence == 0)
        return foundation::Result<void>::Failure(Error("gameplay.navigation.restore_invalid", "invalid journal snapshot"));

    for (const auto& profile : snapshot.profiles)
        if (!profile.subject.IsValid() || !domains_.contains(profile.domain) ||
            !restored_profiles.emplace(profile.subject, profile).second)
            return foundation::Result<void>::Failure(Error("gameplay.navigation.restore_invalid", "invalid profile snapshot"));

    std::uint64_t max_own_layer_low = 0;
    const auto expected_scope = TypeId::FromString("framework.navigation.layer").Raw();
    for (const auto& layer : snapshot.layers)
    {
        if (!IsValidLayer(layer) || layer.lifetime == NavigationLayerLifetime::Transient ||
            layer.lifetime == NavigationLayerLifetime::Session || !restored_layers.emplace(layer.id, layer).second)
            return foundation::Result<void>::Failure(Error("gameplay.navigation.restore_invalid", "invalid layer snapshot"));
        if (layer.id.value.High() == expected_scope)
            max_own_layer_low = std::max(max_own_layer_low, layer.id.value.Low());
    }
    if (snapshot.layer_ids.scope != expected_scope ||
        (snapshot.layer_ids.next != 0 && snapshot.layer_ids.next <= max_own_layer_low))
        return foundation::Result<void>::Failure(Error("gameplay.navigation.restore_invalid", "invalid layer generator snapshot"));

    for (const auto& link : snapshot.links)
        if (!IsValidLink(link) || !restored_links.emplace(link.id, link).second)
            return foundation::Result<void>::Failure(Error("gameplay.navigation.restore_invalid", "invalid link snapshot"));

    std::uint64_t previous = 0;
    for (const auto& change : snapshot.journal)
    {
        if (change.sequence == 0 || (previous != 0 && change.sequence <= previous) ||
            change.sequence >= snapshot.next_change_sequence)
            return foundation::Result<void>::Failure(Error("gameplay.navigation.restore_invalid", "invalid journal sequence"));
        restored_changes.push_back(change);
        previous = change.sequence;
    }

    profiles_.swap(restored_profiles);
    layers_.swap(restored_layers);
    links_.swap(restored_links);
    changes_.swap(restored_changes);
    layer_ids_.Restore(snapshot.layer_ids);
    revision_ = snapshot.revision;
    next_change_sequence_ = snapshot.next_change_sequence;
    RebuildIndexes();
    return foundation::Result<void>::Success();
}

std::vector<NavigationChange> NavigationSemanticsService::ChangesSince(std::uint64_t sequence) const
{
    return ReadChangesSince(sequence).changes;
}
NavigationChangeBatch NavigationSemanticsService::ReadChangesSince(std::uint64_t sequence) const
{
    NavigationChangeBatch batch;
    batch.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
    if (!changes_.empty() && sequence + 1 < changes_.front().sequence)
    {
        batch.snapshot_required = true;
        return batch;
    }
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(batch.changes),
                 [sequence](const auto& change) { return change.sequence > sequence; });
    return batch;
}

NavigationDiagnostics NavigationSemanticsService::GetDiagnostics() const noexcept
{
    return {domains_.size(), rules_.size(), layers_.size(), links_.size(), permission_queries_, denied_queries_,
            cost_modified_queries_, dynamic_updates_};
}

void NavigationSemanticsService::Record(NavigationChange change)
{
    change.sequence = next_change_sequence_++;
    changes_.push_back(std::move(change));
    while (changes_.size() > kChangeJournalCapacity)
        changes_.pop_front();
}
} // namespace epidemic::gameplay::navigation_semantics
