#include "Epidemic/GameFramework/Ownership/ownership.h"

using namespace epidemic::gameplay;
using namespace epidemic::gameplay::ownership;

static GameplayObjectRef Ref(const char *n)
{
    return {GameplayDomainId::FromString("test"), GameplayObjectId::FromString(n)};
}

class TestRoles final : public IOwnershipRoleProvider
{
  public:
    GameplayObjectRef member{};
    TypeId role{};
    bool HasRole(GameplayObjectRef subject, TypeId required, const GameplayContext &) const noexcept override
    {
        return subject == member && required == role;
    }
};

int main()
{
    OwnershipService o;
    const auto player = Ref("player");
    const auto npc = Ref("npc");
    const auto other = Ref("other");
    const auto item = Ref("item");
    const auto take = PropertyRightId::FromString("right.take");
    const auto domain = PropertyDomainId::FromString("property.physical");

    OwnershipRecord r;
    r.property = item;
    r.owner = npc;
    r.domain = domain;
    r.strength = OwnershipStrength::Owned;
    auto rid = o.AssignOwnership(r);
    if (!rid)
        return 1;

    // H01: claims/stolen/public records must never compete with the canonical Owned record.
    OwnershipRecord claimed;
    claimed.property = item;
    claimed.owner = player;
    claimed.domain = domain;
    claimed.strength = OwnershipStrength::Claimed;
    if (!o.AssignOwnership(claimed))
        return 2;
    OwnershipRecord stolen;
    stolen.property = item;
    stolen.owner = other;
    stolen.domain = domain;
    stolen.strength = OwnershipStrength::Stolen;
    if (!o.AssignOwnership(stolen))
        return 3;
    if (!o.GetOwner(item, domain) || o.GetOwner(item, domain)->owner != npc)
        return 4;

    OwnershipRecord second_owner;
    second_owner.property = item;
    second_owner.owner = player;
    second_owner.domain = domain;
    second_owner.strength = OwnershipStrength::Owned;
    if (o.AssignOwnership(second_owner))
        return 5;

    // Default fallback is a crime candidate for someone else's owned property.
    OwnershipPermissionQuery query;
    query.actor = player;
    query.property = item;
    query.right = take;
    query.domain = domain;
    query.context.time = GameplayTimeSeconds(10);
    auto q = o.EvaluatePermission(query);
    if (q.decision != PermissionDecision::CrimeCandidate || q.effective_owner != npc)
        return 6;

    // H03: permission expiration and strength threshold are authoritative.
    PermissionGrant weak;
    weak.subject = player;
    weak.property = item;
    weak.right = take;
    weak.strength = PermissionStrength::Weak;
    weak.granted_at = GameplayTimeSeconds(5);
    weak.expires_at = GameplayTimeSeconds(20);
    auto weak_id = o.GrantPermission(weak);
    if (!weak_id)
        return 7;

    AccessRule require_strong;
    require_strong.property = item;
    require_strong.right = take;
    require_strong.domain = domain;
    require_strong.decision = AccessDecision::RequirePermission;
    require_strong.minimum_permission_strength = PermissionStrength::Strong;
    require_strong.priority = 50;
    auto rule_id = o.AddAccessRule(require_strong);
    if (!rule_id)
        return 8;
    q = o.EvaluatePermission(query);
    if (q.decision != PermissionDecision::Denied)
        return 9;

    PermissionGrant strong = weak;
    strong.id = {};
    strong.strength = PermissionStrength::Strong;
    auto strong_id = o.GrantPermission(strong);
    if (!strong_id)
        return 10;
    q = o.EvaluatePermission(query);
    if (q.decision != PermissionDecision::Allowed || q.matched_grant != strong_id.Value())
        return 11;

    query.context.time = GameplayTimeSeconds(20);
    q = o.EvaluatePermission(query);
    if (q.decision != PermissionDecision::Denied)
        return 12;
    if (o.SweepExpiredPermissions(GameplayTimeSeconds(20)) != 2 || !o.FindPermissions(player).empty())
        return 13;

    // Equal-priority deny wins deterministically over allow, independent of insertion IDs/order.
    if (!o.RemoveAccessRule(rule_id.Value()))
        return 14;
    AccessRule allow;
    allow.property = item;
    allow.right = take;
    allow.domain = domain;
    allow.decision = AccessDecision::Allow;
    allow.priority = 100;
    if (!o.AddAccessRule(allow))
        return 15;
    AccessRule deny = allow;
    deny.id = {};
    deny.decision = AccessDecision::Deny;
    if (!o.AddAccessRule(deny))
        return 16;
    query.context.time = GameplayTimeSeconds(30);
    q = o.EvaluatePermission(query);
    if (q.decision != PermissionDecision::Denied)
        return 17;

    // RequireRole is resolved through a neutral provider, not a Society dependency.
    for (const auto &rule : o.FindAccessRules(item))
        if (!o.RemoveAccessRule(rule.id))
            return 18;
    TestRoles roles;
    roles.member = player;
    roles.role = TypeId::FromString("role.guard");
    o.SetRoleProvider(&roles);
    AccessRule role_rule;
    role_rule.property = item;
    role_rule.right = take;
    role_rule.domain = domain;
    role_rule.decision = AccessDecision::RequireRole;
    role_rule.required_role = roles.role;
    if (!o.AddAccessRule(role_rule))
        return 19;
    q = o.EvaluatePermission(query);
    if (q.decision != PermissionDecision::Allowed)
        return 20;

    // H02: stale transfer must not become assignment when the expected owner is gone.
    const auto original_record = rid.Value();
    if (!o.RemoveOwnership(original_record))
        return 21;
    TransferOwnershipRequest stale;
    stale.property = item;
    stale.from_owner = npc;
    stale.to_owner = player;
    stale.domain = domain;
    if (o.TransferOwnership(stale))
        return 22;
    TransferOwnershipRequest assign_unowned;
    assign_unowned.property = item;
    assign_unowned.to_owner = player;
    assign_unowned.domain = domain;
    assign_unowned.context.time = GameplayTimeSeconds(40);
    if (!o.TransferOwnership(assign_unowned))
        return 23;
    if (!o.GetOwner(item, domain) || o.GetOwner(item, domain)->owner != player)
        return 24;

    const auto current_revision = o.GetOwner(item, domain)->revision;
    TransferOwnershipRequest stale_revision;
    stale_revision.property = item;
    stale_revision.from_owner = player;
    stale_revision.to_owner = npc;
    stale_revision.domain = domain;
    stale_revision.expected_revision = Revision{current_revision.value + 1};
    if (o.TransferOwnership(stale_revision))
        return 25;
    stale_revision.expected_revision = current_revision;
    if (!o.TransferOwnership(stale_revision))
        return 26;

    PropertyClaim claim;
    claim.property = item;
    claim.claimant = player;
    claim.strength_micro = 100;
    auto cid = o.CreateClaim(claim);
    if (!cid || o.FindClaims(item).size() != 1)
        return 27;
    if (!o.ResolveClaim(cid.Value()) || !o.FindClaims(item).empty())
        return 28;

    // H04: restore is transactional and rejects semantic duplicate owners/generator rollback.
    auto valid = o.CaptureSnapshot();
    OwnershipService restored;
    if (!restored.RestoreSnapshot(valid))
        return 29;
    if (!restored.GetOwner(item, domain) || restored.GetOwner(item, domain)->owner != npc)
        return 30;

    auto invalid = valid;
    OwnershipRecord duplicate = *restored.GetOwner(item, domain);
    duplicate.id = OwnershipRecordId::FromRaw(0x2300, invalid.ownership_ids.next);
    duplicate.owner = other;
    invalid.records.push_back(duplicate);
    if (restored.RestoreSnapshot(invalid))
        return 31;
    if (!restored.GetOwner(item, domain) || restored.GetOwner(item, domain)->owner != npc)
        return 32; // failed restore must leave previous state intact

    invalid = valid;
    const auto owner_id = restored.GetOwner(item, domain)->id;
    invalid.ownership_ids.next = owner_id.value.Low();
    if (restored.RestoreSnapshot(invalid))
        return 33;
    if (!restored.GetOwner(item, domain) || restored.GetOwner(item, domain)->owner != npc)
        return 34;

    // Bounded/prunable journal must report a gap instead of silently returning an incomplete stream.
    const auto before = restored.ReadChangesSince(ChangeCursor{});
    if (!before.changes.empty() || before.snapshot_required)
        return 35;
    AccessRule journal_rule;
    journal_rule.property = item;
    journal_rule.right = take;
    journal_rule.domain = domain;
    journal_rule.decision = AccessDecision::Allow;
    auto journal_rule_id = restored.AddAccessRule(journal_rule);
    if (!journal_rule_id)
        return 36;
    auto changes = restored.ReadChangesSince(ChangeCursor{});
    if (changes.snapshot_required || changes.changes.empty())
        return 37;
    restored.PruneChangesThrough(changes.latest_sequence);
    if (!restored.ReadChangesSince(ChangeCursor{}).snapshot_required)
        return 38;

    return 0;
}
