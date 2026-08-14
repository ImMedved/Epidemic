#include "Epidemic/GameFramework/Abilities/abilities.h"
#include "Epidemic/Foundation/error.h"

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::abilities;

namespace
{
class Resource final : public IAbilityResourceProvider
{
  public:
    std::int64_t balance = 100;
    AbilityResourceTypeId fail_reserve{};

    [[nodiscard]] bool CanAfford(GameplayObjectRef, AbilityResourceTypeId, std::int64_t amount) const override { return balance >= amount; }

    [[nodiscard]] foundation::Result<AbilityResourceReservation> Reserve(GameplayObjectRef, AbilityResourceTypeId type, std::int64_t amount, GameplayContext) override
    {
        if (type == fail_reserve)
        {
            return foundation::Result<AbilityResourceReservation>::Failure(foundation::Error::Create("reserve", "reserve failed"));
        }
        if (balance < amount)
        {
            return foundation::Result<AbilityResourceReservation>::Failure(foundation::Error::Create("no", "no"));
        }
        balance -= amount;
        struct Payload { std::int64_t amount; };
        return foundation::Result<AbilityResourceReservation>::Success({{GameplayObjectId::FromRaw(1, static_cast<std::uint64_t>(amount))}, type, RegisteredAbilityPayload::FromTrivial(TypeId::FromString("p"), Payload{amount})});
    }

    [[nodiscard]] foundation::Result<void> Commit(const AbilityResourceReservation&, GameplayContext) override { return foundation::Result<void>::Success(); }

    [[nodiscard]] foundation::Result<void> Release(const AbilityResourceReservation& reservation, GameplayContext) override
    {
        struct Payload { std::int64_t amount; };
        auto payload = reservation.provider_token.AsTrivial<Payload>(TypeId::FromString("p"));
        if (!payload)
        {
            return foundation::Result<void>::Failure(foundation::Error::Create("bad", "bad"));
        }
        balance += payload->amount;
        return foundation::Result<void>::Success();
    }
};
} // namespace

int main()
{
    AbilityService s;
    Resource res;
    s.SetResourceProvider(&res);

    AbilityDefinition d;
    d.canonical_name = "game.fireball";
    d.targeting = AbilityTargetPolicy::SingleTarget;
    d.timing.kind = AbilityTimingKind::CastTime;
    d.timing.cast_duration = {5};
    d.cooldown.group = CooldownGroupId::FromString("game.gcd");
    d.cooldown.duration = {10};
    AbilityCostDefinition c;
    c.resource = AbilityResourceTypeId::FromString("game.mana");
    c.amount_micro = 20;
    c.policy = AbilityCostPolicy::ReserveThenCommit;
    d.costs.push_back(c);
    d.outputs.push_back({ActionTypeId::FromString("game.damage"), 1'000'000, {}});
    auto did = s.RegisterDefinition(d);
    if (!did) return 1;
    s.Freeze();

    GameplayObjectRef owner{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("mage")};
    GameplayObjectRef target{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("enemy")};
    auto iid = s.Grant(owner, did.Value());
    if (!iid) return 2;
    AbilityTargetSet targets;
    targets.primary = target;
    auto exec = s.BeginActivation({iid.Value(), targets, {0}, {}});
    if (!exec || res.balance != 80) return 3;
    if (s.CompleteExecution(exec.Value(), {4})) return 4;
    auto out = s.CompleteExecution(exec.Value(), {5});
    if (!out || out.Value().size() != 1 || res.balance != 80) return 5;
    if (s.CanActivate(iid.Value(), targets, {6}).availability != AbilityAvailability::Unavailable) return 6;
    if (s.CanActivate(iid.Value(), targets, {15}).availability != AbilityAvailability::Available) return 7;
    auto exec2 = s.BeginActivation({iid.Value(), targets, {15}, {}});
    if (!exec2 || res.balance != 60) return 8;
    if (!s.Interrupt(exec2.Value(), TypeId::FromString("test.interrupt"), {16}) || res.balance != 80) return 9;

    AbilityService tx;
    Resource tx_res;
    tx.SetResourceProvider(&tx_res);
    AbilityDefinition tx_def;
    tx_def.canonical_name = "game.combo";
    tx_def.timing.kind = AbilityTimingKind::Instant;
    AbilityCostDefinition a{AbilityResourceTypeId::FromString("game.mana"), 10, AbilityCostPolicy::PayOnStart};
    AbilityCostDefinition b{AbilityResourceTypeId::FromString("game.stamina"), 20, AbilityCostPolicy::PayOnStart};
    AbilityCostDefinition fail{AbilityResourceTypeId::FromString("game.charge"), 1, AbilityCostPolicy::PayOnStart};
    tx_res.fail_reserve = fail.resource;
    tx_def.costs = {a, b, fail};
    tx_def.outputs.push_back({ActionTypeId::FromString("game.combo.out"), 1, {}});
    auto tx_did = tx.RegisterDefinition(tx_def);
    if (!tx_did) return 10;
    tx.Freeze();
    auto tx_iid = tx.Grant(owner, tx_did.Value());
    if (!tx_iid) return 11;
    auto failed = tx.BeginActivation({tx_iid.Value(), {}, {20}, {}});
    if (failed || tx_res.balance != 100) return 12;
    return 0;
}
