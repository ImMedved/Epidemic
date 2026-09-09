#include "Epidemic/GameFramework/Ownership/ownership.h"
#include "Epidemic/Foundation/error.h"

#include <limits>
#include <utility>

namespace epidemic::gameplay::ownership
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}

template <class T> void EraseId(std::vector<T> &ids, const T &id)
{
    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
}

template <class WrappedId>
void AdvanceGeneratorPastExplicitId(MonotonicIdGenerator<GameplayObjectId> &generator, const WrappedId &id) noexcept
{
    if (!id.IsValid())
        return;
    auto snapshot = generator.GetSnapshot();
    if (id.value.High() != snapshot.scope || snapshot.next == 0 || id.value.Low() < snapshot.next)
        return;
    snapshot.next = id.value.Low() == std::numeric_limits<std::uint64_t>::max() ? 0 : id.value.Low() + 1;
    generator.Restore(snapshot);
}

template <class WrappedId>
void UpdateMaxLow(const WrappedId &id, IdScopeId scope, std::uint64_t &max_low) noexcept
{
    if (id.IsValid() && id.value.High() == scope.Raw() && id.value.Low() > max_low)
        max_low = id.value.Low();
}

[[nodiscard]] int PermissionStrengthRank(PermissionStrength strength) noexcept
{
    switch (strength)
    {
    case PermissionStrength::Weak:
        return 0;
    case PermissionStrength::Normal:
        return 1;
    case PermissionStrength::Strong:
        return 2;
    case PermissionStrength::Absolute:
        return 3;
    }
    return 0;
}

[[nodiscard]] int RuleTieBreakRank(AccessDecision decision) noexcept
{
    switch (decision)
    {
    case AccessDecision::CrimeIfViolated:
        return 8;
    case AccessDecision::TrespassIfViolated:
        return 7;
    case AccessDecision::Deny:
    case AccessDecision::Private:
        return 6;
    case AccessDecision::RequireOwner:
        return 5;
    case AccessDecision::RequireRole:
        return 4;
    case AccessDecision::RequirePermission:
        return 3;
    case AccessDecision::Allow:
    case AccessDecision::Public:
        return 2;
    }
    return 0;
}

[[nodiscard]] bool IsGrantExpired(const PermissionGrant &grant, GameplayTimePoint now) noexcept
{
    return grant.expires_at.has_value() && now >= *grant.expires_at;
}

[[nodiscard]] bool IsRuleStructurallyValid(const AccessRule &rule) noexcept
{
    if (!rule.id.IsValid() || !rule.property.IsValid() || !rule.right.IsValid())
        return false;
    if (rule.decision == AccessDecision::RequireRole && !rule.required_role.IsValid())
        return false;
    return true;
}
} // namespace

OwnershipRecord *OwnershipService::FindMutableOwnership(OwnershipRecordId id) noexcept
{
    auto it = records_.find(id);
    return it == records_.end() ? nullptr : &it->second;
}

void OwnershipService::IndexOwnership(const OwnershipRecord &record)
{
    ownership_by_property_[record.property].push_back(record.id);
    ownership_by_owner_[record.owner].push_back(record.id);
    if (record.strength == OwnershipStrength::Owned)
        canonical_owners_[OwnerKey{record.property, record.domain}] = record.id;
}

void OwnershipService::UnindexOwnership(const OwnershipRecord &record)
{
    if (auto it = ownership_by_property_.find(record.property); it != ownership_by_property_.end())
    {
        EraseId(it->second, record.id);
        if (it->second.empty())
            ownership_by_property_.erase(it);
    }
    if (auto it = ownership_by_owner_.find(record.owner); it != ownership_by_owner_.end())
    {
        EraseId(it->second, record.id);
        if (it->second.empty())
            ownership_by_owner_.erase(it);
    }
    if (record.strength == OwnershipStrength::Owned)
    {
        const OwnerKey key{record.property, record.domain};
        if (auto it = canonical_owners_.find(key); it != canonical_owners_.end() && it->second == record.id)
            canonical_owners_.erase(it);
    }
}

void OwnershipService::IndexGrant(const PermissionGrant &grant)
{
    grants_by_subject_[grant.subject].push_back(grant.id);
}

void OwnershipService::UnindexGrant(const PermissionGrant &grant)
{
    if (auto it = grants_by_subject_.find(grant.subject); it != grants_by_subject_.end())
    {
        EraseId(it->second, grant.id);
        if (it->second.empty())
            grants_by_subject_.erase(it);
    }
}

void OwnershipService::IndexRule(const AccessRule &rule)
{
    rules_by_property_[rule.property].push_back(rule.id);
}

void OwnershipService::UnindexRule(const AccessRule &rule)
{
    if (auto it = rules_by_property_.find(rule.property); it != rules_by_property_.end())
    {
        EraseId(it->second, rule.id);
        if (it->second.empty())
            rules_by_property_.erase(it);
    }
}

void OwnershipService::IndexClaim(const PropertyClaim &claim)
{
    claims_by_property_[claim.property].push_back(claim.id);
}

void OwnershipService::UnindexClaim(const PropertyClaim &claim)
{
    if (auto it = claims_by_property_.find(claim.property); it != claims_by_property_.end())
    {
        EraseId(it->second, claim.id);
        if (it->second.empty())
            claims_by_property_.erase(it);
    }
}

foundation::Result<OwnershipRecordId> OwnershipService::AssignOwnership(OwnershipRecord r)
{
    if (!r.property.IsValid() || !r.owner.IsValid())
        return foundation::Result<OwnershipRecordId>::Failure(
            Error("gameplay.ownership.invalid_record", "invalid ownership record"));

    if (!r.id.IsValid())
    {
        r.id = OwnershipRecordId{ownership_ids_.Next()};
        if (!r.id.IsValid())
            return foundation::Result<OwnershipRecordId>::Failure(
                Error("gameplay.ownership.id_exhausted", "ownership record id generator exhausted"));
    }
    if (records_.contains(r.id))
        return foundation::Result<OwnershipRecordId>::Failure(
            Error("gameplay.ownership.duplicate_record", "duplicate ownership record"));

    if (r.strength == OwnershipStrength::Owned)
    {
        const OwnerKey key{r.property, r.domain};
        if (canonical_owners_.contains(key))
            return foundation::Result<OwnershipRecordId>::Failure(
                Error("gameplay.ownership.owner_conflict", "canonical owner already exists for property/domain"));
    }

    AdvanceGeneratorPastExplicitId(ownership_ids_, r.id);
    Bump();
    r.revision = revision_;
    const auto id = r.id;
    records_.emplace(id, r);
    IndexOwnership(r);
    Record({0, OwnershipChangeKind::OwnershipAssigned, r.property, {}, r.owner, {}, {}, revision_, r.domain});
    return foundation::Result<OwnershipRecordId>::Success(id);
}

foundation::Result<void> OwnershipService::RemoveOwnership(OwnershipRecordId id, GameplayContext c)
{
    auto it = records_.find(id);
    if (it == records_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.ownership.record_missing", "ownership record missing"));
    const auto copy = it->second;
    UnindexOwnership(copy);
    records_.erase(it);
    Bump();
    Record({0, OwnershipChangeKind::OwnershipRemoved, copy.property, {}, copy.owner, {}, c, revision_, copy.domain});
    return foundation::Result<void>::Success();
}

foundation::Result<void> OwnershipService::TransferOwnership(TransferOwnershipRequest q)
{
    if (!q.property.IsValid() || !q.to_owner.IsValid())
        return foundation::Result<void>::Failure(Error("gameplay.ownership.invalid_transfer", "invalid transfer"));

    const auto *found = GetOwner(q.property, q.domain);
    if (!found)
    {
        if (q.from_owner.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.ownership.transfer_stale", "expected owner is missing"));
        OwnershipRecord r;
        r.property = q.property;
        r.owner = q.to_owner;
        r.domain = q.domain;
        r.strength = OwnershipStrength::Owned;
        r.acquired_at = q.context.time;
        auto added = AssignOwnership(std::move(r));
        if (!added)
            return foundation::Result<void>::Failure(std::move(added.GetError()));
        return foundation::Result<void>::Success();
    }

    if (q.from_owner.IsValid() && found->owner != q.from_owner)
        return foundation::Result<void>::Failure(
            Error("gameplay.ownership.transfer_owner_mismatch", "transfer owner mismatch"));
    if (q.expected_revision.value != 0 && found->revision != q.expected_revision)
        return foundation::Result<void>::Failure(
            Error("gameplay.ownership.transfer_stale_revision", "ownership record revision changed"));
    if (found->owner == q.to_owner)
        return foundation::Result<void>::Success();

    auto *current = FindMutableOwnership(found->id);
    if (!current)
        return foundation::Result<void>::Failure(
            Error("gameplay.ownership.record_missing", "canonical ownership record disappeared"));
    const auto previous = current->owner;
    UnindexOwnership(*current);
    Bump();
    current->owner = q.to_owner;
    // TransferOwnership always changes canonical ownership. Theft provenance belongs to the reason/change,
    // while OwnershipStrength::Stolen remains an auxiliary non-canonical relationship marker.
    current->strength = OwnershipStrength::Owned;
    current->acquired_at = q.context.time;
    current->revision = revision_;
    IndexOwnership(*current);
    ++diagnostics_.ownership_transfers;
    Record({0, OwnershipChangeKind::OwnershipTransferred, q.property, q.from_owner, q.to_owner, {}, q.context,
            revision_, q.domain, previous, q.reason});
    return foundation::Result<void>::Success();
}

foundation::Result<PermissionGrantId> OwnershipService::GrantPermission(PermissionGrant g)
{
    if (!g.subject.IsValid() || !g.property.IsValid() || !g.right.IsValid() ||
        (g.expires_at.has_value() && *g.expires_at < g.granted_at))
        return foundation::Result<PermissionGrantId>::Failure(
            Error("gameplay.ownership.invalid_grant", "invalid permission grant"));
    if (!g.id.IsValid())
    {
        g.id = PermissionGrantId{grant_ids_.Next()};
        if (!g.id.IsValid())
            return foundation::Result<PermissionGrantId>::Failure(
                Error("gameplay.ownership.id_exhausted", "permission grant id generator exhausted"));
    }
    if (grants_.contains(g.id))
        return foundation::Result<PermissionGrantId>::Failure(
            Error("gameplay.ownership.duplicate_grant", "duplicate permission grant"));
    AdvanceGeneratorPastExplicitId(grant_ids_, g.id);
    Bump();
    g.revision = revision_;
    const auto id = g.id;
    grants_.emplace(id, g);
    IndexGrant(g);
    Record({0, OwnershipChangeKind::PermissionGranted, g.property, g.subject, {}, g.right, {}, revision_});
    return foundation::Result<PermissionGrantId>::Success(id);
}

foundation::Result<void> OwnershipService::RevokePermission(PermissionGrantId id, GameplayContext c)
{
    auto it = grants_.find(id);
    if (it == grants_.end())
        return foundation::Result<void>::Failure(Error("gameplay.ownership.grant_missing", "permission grant missing"));
    const auto copy = it->second;
    UnindexGrant(copy);
    grants_.erase(it);
    Bump();
    Record({0, OwnershipChangeKind::PermissionRevoked, copy.property, copy.subject, {}, copy.right, c, revision_});
    return foundation::Result<void>::Success();
}

std::size_t OwnershipService::SweepExpiredPermissions(GameplayTimePoint now, GameplayContext context)
{
    std::vector<PermissionGrantId> expired;
    expired.reserve(grants_.size());
    for (const auto &[id, grant] : grants_)
        if (IsGrantExpired(grant, now))
            expired.push_back(id);
    std::sort(expired.begin(), expired.end());
    context.time = now;
    std::size_t removed = 0;
    for (const auto id : expired)
    {
        if (RevokePermission(id, context))
        {
            ++removed;
            ++diagnostics_.expired_permissions;
        }
    }
    return removed;
}

foundation::Result<AccessRuleId> OwnershipService::AddAccessRule(AccessRule r)
{
    if (!r.property.IsValid() || !r.right.IsValid() ||
        (r.decision == AccessDecision::RequireRole && !r.required_role.IsValid()))
        return foundation::Result<AccessRuleId>::Failure(
            Error("gameplay.ownership.invalid_rule", "invalid access rule"));
    if (!r.id.IsValid())
    {
        r.id = AccessRuleId{rule_ids_.Next()};
        if (!r.id.IsValid())
            return foundation::Result<AccessRuleId>::Failure(
                Error("gameplay.ownership.id_exhausted", "access rule id generator exhausted"));
    }
    if (rules_.contains(r.id))
        return foundation::Result<AccessRuleId>::Failure(
            Error("gameplay.ownership.duplicate_rule", "duplicate access rule"));
    AdvanceGeneratorPastExplicitId(rule_ids_, r.id);
    Bump();
    r.revision = revision_;
    const auto id = r.id;
    rules_.emplace(id, r);
    IndexRule(r);
    Record({0, OwnershipChangeKind::AccessRuleAdded, r.property, r.required_subject, {}, r.right, {}, revision_, r.domain});
    return foundation::Result<AccessRuleId>::Success(id);
}

foundation::Result<void> OwnershipService::UpdateAccessRule(AccessRule r, GameplayContext context)
{
    if (!IsRuleStructurallyValid(r))
        return foundation::Result<void>::Failure(Error("gameplay.ownership.invalid_rule", "invalid access rule"));
    auto it = rules_.find(r.id);
    if (it == rules_.end())
        return foundation::Result<void>::Failure(Error("gameplay.ownership.rule_missing", "access rule missing"));
    if (r.revision.value != 0 && r.revision != it->second.revision)
        return foundation::Result<void>::Failure(Error("gameplay.ownership.rule_stale", "access rule revision changed"));
    UnindexRule(it->second);
    Bump();
    r.revision = revision_;
    it->second = r;
    IndexRule(it->second);
    Record({0, OwnershipChangeKind::AccessRuleChanged, r.property, r.required_subject, {}, r.right, context, revision_, r.domain});
    return foundation::Result<void>::Success();
}

foundation::Result<void> OwnershipService::RemoveAccessRule(AccessRuleId id, GameplayContext context)
{
    auto it = rules_.find(id);
    if (it == rules_.end())
        return foundation::Result<void>::Failure(Error("gameplay.ownership.rule_missing", "access rule missing"));
    const auto copy = it->second;
    UnindexRule(copy);
    rules_.erase(it);
    Bump();
    Record({0, OwnershipChangeKind::AccessRuleRemoved, copy.property, copy.required_subject, {}, copy.right, context,
            revision_, copy.domain});
    return foundation::Result<void>::Success();
}

foundation::Result<PropertyClaimId> OwnershipService::CreateClaim(PropertyClaim c)
{
    if (!c.property.IsValid() || !c.claimant.IsValid())
        return foundation::Result<PropertyClaimId>::Failure(
            Error("gameplay.ownership.invalid_claim", "invalid property claim"));
    if (!c.id.IsValid())
    {
        c.id = PropertyClaimId{claim_ids_.Next()};
        if (!c.id.IsValid())
            return foundation::Result<PropertyClaimId>::Failure(
                Error("gameplay.ownership.id_exhausted", "property claim id generator exhausted"));
    }
    if (claims_.contains(c.id))
        return foundation::Result<PropertyClaimId>::Failure(
            Error("gameplay.ownership.duplicate_claim", "duplicate claim"));
    AdvanceGeneratorPastExplicitId(claim_ids_, c.id);
    Bump();
    c.revision = revision_;
    const auto id = c.id;
    claims_.emplace(id, c);
    IndexClaim(c);
    ++diagnostics_.claim_conflicts;
    Record({0, OwnershipChangeKind::PropertyClaimCreated, c.property, c.claimant, {}, {}, {}, revision_});
    return foundation::Result<PropertyClaimId>::Success(id);
}

foundation::Result<void> OwnershipService::ResolveClaim(PropertyClaimId id, GameplayContext c)
{
    auto it = claims_.find(id);
    if (it == claims_.end())
        return foundation::Result<void>::Failure(Error("gameplay.ownership.claim_missing", "claim missing"));
    const auto copy = it->second;
    UnindexClaim(copy);
    claims_.erase(it);
    Bump();
    Record({0, OwnershipChangeKind::PropertyClaimResolved, copy.property, copy.claimant, {}, {}, c, revision_});
    return foundation::Result<void>::Success();
}

const PermissionGrant *OwnershipService::FindBestActiveGrant(GameplayObjectRef subject, GameplayObjectRef property,
                                                              PropertyRightId right, GameplayTimePoint now) const noexcept
{
    const auto indexed = grants_by_subject_.find(subject);
    if (indexed == grants_by_subject_.end())
        return nullptr;
    const PermissionGrant *best = nullptr;
    for (const auto id : indexed->second)
    {
        const auto it = grants_.find(id);
        if (it == grants_.end())
            continue;
        const auto &grant = it->second;
        if (grant.property != property || grant.right != right || IsGrantExpired(grant, now))
            continue;
        if (!best || PermissionStrengthRank(grant.strength) > PermissionStrengthRank(best->strength) ||
            (grant.strength == best->strength && grant.id < best->id))
            best = &grant;
    }
    return best;
}

bool OwnershipService::IsPublicProperty(GameplayObjectRef property, PropertyDomainId domain) const noexcept
{
    const auto indexed = ownership_by_property_.find(property);
    if (indexed == ownership_by_property_.end())
        return false;
    for (const auto id : indexed->second)
    {
        const auto it = records_.find(id);
        if (it != records_.end() && it->second.domain == domain && it->second.strength == OwnershipStrength::Public)
            return true;
    }
    return false;
}

OwnershipPermissionResult OwnershipService::EvaluatePermission(OwnershipPermissionQuery q) const
{
    ++diagnostics_.permission_queries;
    OwnershipPermissionResult out;
    out.revision = revision_;
    if (!q.actor.IsValid() || !q.property.IsValid() || !q.right.IsValid())
    {
        out.decision = PermissionDecision::Unknown;
        return out;
    }

    const auto *owner = GetOwner(q.property, q.domain);
    if (owner)
        out.effective_owner = owner->owner;
    if (owner && owner->owner == q.actor)
    {
        out.decision = PermissionDecision::Allowed;
        out.reasons.push_back(TypeId::FromString("framework.ownership.reason.owner"));
        return out;
    }

    const auto *grant = FindBestActiveGrant(q.actor, q.property, q.right, q.context.time);
    if (grant && grant->strength == PermissionStrength::Absolute)
    {
        out.decision = PermissionDecision::Allowed;
        out.matched_grant = grant->id;
        out.reasons.push_back(TypeId::FromString("framework.ownership.reason.absolute_permission"));
        return out;
    }

    auto rules = FindAccessRules(q.property);
    std::sort(rules.begin(), rules.end(), [](const auto &a, const auto &b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        const auto ar = RuleTieBreakRank(a.decision);
        const auto br = RuleTieBreakRank(b.decision);
        if (ar != br)
            return ar > br;
        return a.id < b.id;
    });

    for (const auto &r : rules)
    {
        if (r.right != q.right || r.domain != q.domain)
            continue;
        if (r.required_subject.IsValid() && r.required_subject != q.actor)
            continue;
        out.matched_rule = r.id;
        switch (r.decision)
        {
        case AccessDecision::Allow:
        case AccessDecision::Public:
            out.decision = PermissionDecision::Allowed;
            out.reasons.push_back(TypeId::FromString("framework.ownership.reason.rule_allow"));
            return out;
        case AccessDecision::Deny:
        case AccessDecision::Private:
            ++diagnostics_.denied_permissions;
            out.decision = PermissionDecision::Denied;
            out.reasons.push_back(TypeId::FromString("framework.ownership.reason.rule_deny"));
            return out;
        case AccessDecision::TrespassIfViolated:
            ++diagnostics_.denied_permissions;
            out.decision = PermissionDecision::Trespass;
            out.reasons.push_back(TypeId::FromString("framework.ownership.reason.trespass"));
            return out;
        case AccessDecision::CrimeIfViolated:
            ++diagnostics_.crime_candidate_permissions;
            out.decision = PermissionDecision::CrimeCandidate;
            out.reasons.push_back(TypeId::FromString("framework.ownership.reason.crime"));
            return out;
        case AccessDecision::RequirePermission:
            if (grant && PermissionStrengthRank(grant->strength) >= PermissionStrengthRank(r.minimum_permission_strength))
            {
                out.decision = PermissionDecision::Allowed;
                out.matched_grant = grant->id;
                out.reasons.push_back(TypeId::FromString("framework.ownership.reason.permission_grant"));
            }
            else
            {
                ++diagnostics_.denied_permissions;
                out.decision = PermissionDecision::Denied;
                out.reasons.push_back(TypeId::FromString("framework.ownership.reason.permission_required"));
            }
            return out;
        case AccessDecision::RequireOwner:
            ++diagnostics_.denied_permissions;
            out.decision = PermissionDecision::Denied;
            out.reasons.push_back(TypeId::FromString("framework.ownership.reason.owner_required"));
            return out;
        case AccessDecision::RequireRole:
            if (!role_provider_)
            {
                out.decision = PermissionDecision::Unknown;
                out.reasons.push_back(TypeId::FromString("framework.ownership.reason.role_provider_unavailable"));
                return out;
            }
            if (role_provider_->HasRole(q.actor, r.required_role, q.context))
            {
                out.decision = PermissionDecision::Allowed;
                out.reasons.push_back(TypeId::FromString("framework.ownership.reason.role"));
            }
            else
            {
                ++diagnostics_.denied_permissions;
                out.decision = PermissionDecision::Denied;
                out.reasons.push_back(TypeId::FromString("framework.ownership.reason.role_required"));
            }
            return out;
        }
    }

    if (grant)
    {
        out.decision = PermissionDecision::Allowed;
        out.matched_grant = grant->id;
        out.reasons.push_back(TypeId::FromString("framework.ownership.reason.permission_grant"));
        return out;
    }
    if (IsPublicProperty(q.property, q.domain))
    {
        out.decision = PermissionDecision::Allowed;
        out.reasons.push_back(TypeId::FromString("framework.ownership.reason.public_property"));
        return out;
    }

    out.decision = owner ? PermissionDecision::CrimeCandidate : PermissionDecision::NotApplicable;
    if (out.decision == PermissionDecision::CrimeCandidate)
        ++diagnostics_.crime_candidate_permissions;
    return out;
}

const OwnershipRecord *OwnershipService::GetOwner(GameplayObjectRef property, PropertyDomainId domain) const noexcept
{
    const auto indexed = canonical_owners_.find(OwnerKey{property, domain});
    if (indexed == canonical_owners_.end())
        return nullptr;
    const auto it = records_.find(indexed->second);
    return it == records_.end() ? nullptr : &it->second;
}

const OwnershipRecord *OwnershipService::GetOwner(GameplayObjectRef property) const noexcept
{
    const auto indexed = ownership_by_property_.find(property);
    if (indexed == ownership_by_property_.end())
        return nullptr;
    const OwnershipRecord *only = nullptr;
    for (const auto id : indexed->second)
    {
        const auto it = records_.find(id);
        if (it == records_.end() || it->second.strength != OwnershipStrength::Owned)
            continue;
        if (only)
            return nullptr; // Multiple ownership domains require the explicit-domain overload.
        only = &it->second;
    }
    return only;
}

std::vector<OwnershipRecord> OwnershipService::GetOwnershipRecords(GameplayObjectRef p) const
{
    std::vector<OwnershipRecord> out;
    const auto indexed = ownership_by_property_.find(p);
    if (indexed == ownership_by_property_.end())
        return out;
    out.reserve(indexed->second.size());
    for (const auto id : indexed->second)
        if (const auto it = records_.find(id); it != records_.end())
            out.push_back(it->second);
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

std::vector<OwnershipRecord> OwnershipService::FindPropertiesOwnedBy(GameplayObjectRef o) const
{
    std::vector<OwnershipRecord> out;
    const auto indexed = ownership_by_owner_.find(o);
    if (indexed == ownership_by_owner_.end())
        return out;
    out.reserve(indexed->second.size());
    for (const auto id : indexed->second)
        if (const auto it = records_.find(id); it != records_.end())
            out.push_back(it->second);
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        if (a.property != b.property)
            return a.property < b.property;
        if (a.domain != b.domain)
            return a.domain < b.domain;
        return a.id < b.id;
    });
    return out;
}

std::vector<PermissionGrant> OwnershipService::FindPermissions(GameplayObjectRef s) const
{
    std::vector<PermissionGrant> out;
    const auto indexed = grants_by_subject_.find(s);
    if (indexed == grants_by_subject_.end())
        return out;
    out.reserve(indexed->second.size());
    for (const auto id : indexed->second)
        if (const auto it = grants_.find(id); it != grants_.end())
            out.push_back(it->second);
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

std::vector<PropertyClaim> OwnershipService::FindClaims(GameplayObjectRef p) const
{
    std::vector<PropertyClaim> out;
    const auto indexed = claims_by_property_.find(p);
    if (indexed == claims_by_property_.end())
        return out;
    out.reserve(indexed->second.size());
    for (const auto id : indexed->second)
        if (const auto it = claims_.find(id); it != claims_.end())
            out.push_back(it->second);
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

std::vector<AccessRule> OwnershipService::FindAccessRules(GameplayObjectRef p) const
{
    std::vector<AccessRule> out;
    const auto indexed = rules_by_property_.find(p);
    if (indexed == rules_by_property_.end())
        return out;
    out.reserve(indexed->second.size());
    for (const auto id : indexed->second)
        if (const auto it = rules_.find(id); it != rules_.end())
            out.push_back(it->second);
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        const auto ar = RuleTieBreakRank(a.decision);
        const auto br = RuleTieBreakRank(b.decision);
        if (ar != br)
            return ar > br;
        return a.id < b.id;
    });
    return out;
}

std::vector<OwnershipChange> OwnershipService::ChangesSinceSequence(std::uint64_t seq) const
{
    return ReadChangesSinceSequence(seq).changes;
}

OwnershipChangeBatch OwnershipService::ReadChangesSinceSequence(std::uint64_t seq) const
{
    OwnershipChangeBatch batch;
    batch.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;
    batch.latest_sequence = next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max() : next_change_sequence_ - 1;
    const bool history_gap = !changes_.empty() ? seq < changes_.front().sequence - 1
                                                : (batch.latest_sequence != 0 && seq < batch.latest_sequence);
    if (history_gap || seq > batch.latest_sequence)
    {
        batch.snapshot_required = true;
        return batch;
    }
    for (const auto &change : changes_)
        if (change.sequence > seq)
            batch.changes.push_back(change);
    return batch;
}

void OwnershipService::PruneChangesThrough(std::uint64_t sequence)
{
    while (!changes_.empty() && changes_.front().sequence <= sequence)
        changes_.pop_front();
}

OwnershipSnapshot OwnershipService::CaptureSnapshot() const
{
    OwnershipSnapshot s;
    s.records.reserve(records_.size());
    s.grants.reserve(grants_.size());
    s.rules.reserve(rules_.size());
    s.claims.reserve(claims_.size());
    for (const auto &[id, r] : records_)
    {
        (void)id;
        s.records.push_back(r);
    }
    for (const auto &[id, g] : grants_)
    {
        (void)id;
        s.grants.push_back(g);
    }
    for (const auto &[id, r] : rules_)
    {
        (void)id;
        s.rules.push_back(r);
    }
    for (const auto &[id, c] : claims_)
    {
        (void)id;
        s.claims.push_back(c);
    }
    std::sort(s.records.begin(), s.records.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.grants.begin(), s.grants.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.rules.begin(), s.rules.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(s.claims.begin(), s.claims.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    s.ownership_ids = ownership_ids_.GetSnapshot();
    s.grant_ids = grant_ids_.GetSnapshot();
    s.rule_ids = rule_ids_.GetSnapshot();
    s.claim_ids = claim_ids_.GetSnapshot();
    s.revision = revision_;
    s.change_epoch = journal_epoch_;
    return s;
}

foundation::Result<void> OwnershipService::RestoreSnapshot(OwnershipSnapshot s)
{
    const auto next_journal_epoch = CheckedNextChangeEpoch(s.change_epoch > journal_epoch_ ? s.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    }
    if (s.revision.value == 0 && (!s.records.empty() || !s.grants.empty() || !s.rules.empty() || !s.claims.empty()))
        return foundation::Result<void>::Failure(
            Error("gameplay.ownership.restore_invalid", "non-empty snapshot requires non-zero revision"));

    std::unordered_map<OwnershipRecordId, OwnershipRecord, IdHash> records;
    std::unordered_map<PermissionGrantId, PermissionGrant, IdHash> grants;
    std::unordered_map<AccessRuleId, AccessRule, IdHash> rules;
    std::unordered_map<PropertyClaimId, PropertyClaim, IdHash> claims;
    std::unordered_map<OwnerKey, OwnershipRecordId, OwnerKeyHash> canonical;
    std::unordered_map<GameplayObjectRef, std::vector<OwnershipRecordId>, RefHash> ownership_by_property;
    std::unordered_map<GameplayObjectRef, std::vector<OwnershipRecordId>, RefHash> ownership_by_owner;
    std::unordered_map<GameplayObjectRef, std::vector<PermissionGrantId>, RefHash> grants_by_subject;
    std::unordered_map<GameplayObjectRef, std::vector<AccessRuleId>, RefHash> rules_by_property;
    std::unordered_map<GameplayObjectRef, std::vector<PropertyClaimId>, RefHash> claims_by_property;

    records.reserve(s.records.size());
    grants.reserve(s.grants.size());
    rules.reserve(s.rules.size());
    claims.reserve(s.claims.size());

    std::uint64_t max_ownership_low = 0;
    std::uint64_t max_grant_low = 0;
    std::uint64_t max_rule_low = 0;
    std::uint64_t max_claim_low = 0;

    for (const auto &r : s.records)
    {
        if (!r.id.IsValid() || !r.property.IsValid() || !r.owner.IsValid() || r.revision.value > s.revision.value ||
            records.contains(r.id))
            return foundation::Result<void>::Failure(
                Error("gameplay.ownership.restore_invalid", "invalid ownership snapshot record"));
        if (r.strength == OwnershipStrength::Owned)
        {
            const OwnerKey key{r.property, r.domain};
            if (canonical.contains(key))
                return foundation::Result<void>::Failure(
                    Error("gameplay.ownership.restore_owner_conflict", "multiple canonical owners for property/domain"));
            canonical.emplace(key, r.id);
        }
        records.emplace(r.id, r);
        ownership_by_property[r.property].push_back(r.id);
        ownership_by_owner[r.owner].push_back(r.id);
        UpdateMaxLow(r.id, ownership_ids_.Scope(), max_ownership_low);
    }

    for (const auto &g : s.grants)
    {
        if (!g.id.IsValid() || !g.subject.IsValid() || !g.property.IsValid() || !g.right.IsValid() ||
            g.revision.value > s.revision.value || grants.contains(g.id) ||
            (g.expires_at.has_value() && *g.expires_at < g.granted_at))
            return foundation::Result<void>::Failure(
                Error("gameplay.ownership.restore_invalid", "invalid permission grant snapshot"));
        grants.emplace(g.id, g);
        grants_by_subject[g.subject].push_back(g.id);
        UpdateMaxLow(g.id, grant_ids_.Scope(), max_grant_low);
    }

    for (const auto &r : s.rules)
    {
        if (!IsRuleStructurallyValid(r) || r.revision.value > s.revision.value || rules.contains(r.id))
            return foundation::Result<void>::Failure(
                Error("gameplay.ownership.restore_invalid", "invalid access rule snapshot"));
        rules.emplace(r.id, r);
        rules_by_property[r.property].push_back(r.id);
        UpdateMaxLow(r.id, rule_ids_.Scope(), max_rule_low);
    }

    for (const auto &c : s.claims)
    {
        if (!c.id.IsValid() || !c.property.IsValid() || !c.claimant.IsValid() || c.revision.value > s.revision.value ||
            claims.contains(c.id))
            return foundation::Result<void>::Failure(
                Error("gameplay.ownership.restore_invalid", "invalid property claim snapshot"));
        claims.emplace(c.id, c);
        claims_by_property[c.property].push_back(c.id);
        UpdateMaxLow(c.id, claim_ids_.Scope(), max_claim_low);
    }

    const auto ownership_generator_ok = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        s.ownership_ids, ownership_ids_.Scope(), max_ownership_low);
    const auto grant_generator_ok = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        s.grant_ids, grant_ids_.Scope(), max_grant_low);
    const auto rule_generator_ok = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        s.rule_ids, rule_ids_.Scope(), max_rule_low);
    const auto claim_generator_ok = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        s.claim_ids, claim_ids_.Scope(), max_claim_low);
    if (!ownership_generator_ok || !grant_generator_ok || !rule_generator_ok || !claim_generator_ok)
        return foundation::Result<void>::Failure(
            Error("gameplay.ownership.restore_invalid_generator", "invalid ownership id generator snapshot"));

    records_ = std::move(records);
    grants_ = std::move(grants);
    rules_ = std::move(rules);
    claims_ = std::move(claims);
    canonical_owners_ = std::move(canonical);
    ownership_by_property_ = std::move(ownership_by_property);
    ownership_by_owner_ = std::move(ownership_by_owner);
    grants_by_subject_ = std::move(grants_by_subject);
    rules_by_property_ = std::move(rules_by_property);
    claims_by_property_ = std::move(claims_by_property);
    ownership_ids_.Restore(s.ownership_ids);
    grant_ids_.Restore(s.grant_ids);
    rule_ids_.Restore(s.rule_ids);
    claim_ids_.Restore(s.claim_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    diagnostics_ = {};
    journal_epoch_ = *next_journal_epoch;
    return foundation::Result<void>::Success();
}

OwnershipDiagnostics OwnershipService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_;
    d.ownership_records = records_.size();
    d.permission_grants = grants_.size();
    d.access_rules = rules_.size();
    d.property_claims = claims_.size();
    return d;
}

void OwnershipService::Record(OwnershipChange c)
{
    if (next_change_sequence_ == 0)
    {
        ++diagnostics_.journal_dropped_changes;
        return;
    }
    c.sequence = next_change_sequence_;
    if (next_change_sequence_ == std::numeric_limits<std::uint64_t>::max())
        next_change_sequence_ = 0;
    else
        ++next_change_sequence_;
    changes_.push_back(std::move(c));
    while (changes_.size() > kChangeRetention)
    {
        changes_.pop_front();
        ++diagnostics_.journal_dropped_changes;
    }
}
} // namespace epidemic::gameplay::ownership
