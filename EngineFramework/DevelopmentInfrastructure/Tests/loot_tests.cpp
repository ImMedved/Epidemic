#include "Epidemic/GameFramework/Loot/loot.h"

#include <stdexcept>

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::loot;

namespace
{
class Handler final : public IRewardHandler
{
  public:
    explicit Handler(std::string_view name = "test.reward", bool fail_prepare = false, bool throw_prepare = false)
        : type(RewardTypeId::FromString(name)), fail_prepare(fail_prepare), throw_prepare(throw_prepare)
    {
    }

    RewardTypeId type{};
    bool fail_prepare = false;
    bool throw_prepare = false;
    std::int64_t delivered = 0;
    std::int64_t prepared = 0;
    std::int64_t cancelled = 0;

    [[nodiscard]] RewardTypeId Type() const noexcept override { return type; }

    [[nodiscard]] foundation::Result<RewardDeliveryStage> Prepare(const RewardOperation& operation) override
    {
        if (throw_prepare)
            throw std::runtime_error("prepare threw");
        if (fail_prepare)
            return foundation::Result<RewardDeliveryStage>::Failure(foundation::Error::Create("prepare", "prepare failed"));
        prepared += operation.quantity_micro;
        return foundation::Result<RewardDeliveryStage>::Success({operation, RewardDeliveryDisposition::Delivered});
    }
    void Commit(RewardDeliveryStage& stage) noexcept override { delivered += stage.operation.quantity_micro; }
    void Cancel(RewardDeliveryStage& stage) noexcept override { cancelled += stage.operation.quantity_micro; }
};

class ThrowingConditionProvider final : public ILootConditionProvider
{
  public:
    [[nodiscard]] bool Evaluate(TypeId, const LootContext&) const override
    {
        throw std::runtime_error("condition threw");
    }
};

struct ConfiguredLoot
{
    Handler handler;
    LootService service;
    RewardDefinition reward;
    RewardDefinitionId reward_id{};
    LootTableId table_id{};
};

bool Configure(ConfiguredLoot& cfg, LootRollPolicy policy = LootRollPolicy::GuaranteedAll, std::uint64_t weight = 1,
               TypeId condition = {})
{
    if (!cfg.service.RegisterRewardHandler("test.reward", &cfg.handler)) return false;
    cfg.reward.canonical_name = "test.reward.gold";
    cfg.reward.type = cfg.handler.Type();
    cfg.reward.min_quantity_micro = 10;
    cfg.reward.max_quantity_micro = 20;
    auto rid = cfg.service.RegisterRewardDefinition(cfg.reward);
    if (!rid) return false;
    cfg.reward_id = rid.Value();
    LootTableDefinition table;
    table.canonical_name = "test.table";
    table.policy = policy;
    table.entries.push_back({LootEntryId::FromString("test.entry"), weight, 1'000'000, cfg.reward_id, {}, condition});
    auto tid = cfg.service.RegisterLootTable(table);
    if (!tid) return false;
    cfg.table_id = tid.Value();
    return true;
}
} // namespace

int main()
{
    Handler h;
    LootService s;
    if (!s.RegisterRewardHandler("test.reward", &h)) return 1;
    RewardDefinition r;
    r.canonical_name = "test.reward.gold";
    r.type = h.Type();
    r.min_quantity_micro = 10;
    r.max_quantity_micro = 20;
    auto rid = s.RegisterRewardDefinition(r);
    if (!rid) return 2;
    LootTableDefinition t;
    t.canonical_name = "test.table";
    t.policy = LootRollPolicy::GuaranteedAll;
    t.entries.push_back({LootEntryId::FromString("test.entry"), 1, 1'000'000, rid.Value(), {}, {}});
    auto tid = s.RegisterLootTable(t);
    if (!tid || !s.Freeze()) return 3;
    GameplayObjectRef recipient{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("player")};
    LootContext c;
    c.recipient = recipient;
    c.seed = {42};

    // H34: preview is deterministic, untracked and cannot become pending.
    auto preview = s.Preview(tid.Value(), c);
    if (!preview || preview.Value().id.IsValid() || preview.Value().rewards.size() != 1) return 4;
    auto preview_pending = s.MakePending(preview.Value());
    if (preview_pending) return 5;

    auto a = s.GenerateTracked(tid.Value(), c);
    if (!a || a.Value().rewards.size() != 1 || !a.Value().id.IsValid()) return 6;
    const auto quantity = a.Value().rewards[0].quantity_micro;

    ConfiguredLoot s2;
    if (!Configure(s2) || !s2.service.Freeze()) return 7;
    auto b = s2.service.GenerateTracked(s2.table_id, c);
    if (!b || b.Value().rewards[0].quantity_micro != quantity) return 8;

    // Tracked generation has an explicit discard path and releases tracked capacity/state.
    const auto discarded_id = b.Value().id;
    if (!s2.service.DiscardGenerated(discarded_id)) return 9;
    auto discarded_pending = s2.service.MakePending(std::move(b).Value());
    if (discarded_pending) return 10;

    auto pending = s.MakePending(std::move(a).Value());
    if (!pending || !s.Claim(pending.Value()) || !s.WasClaimed(pending.Value())) return 11;
    if (s.ClaimHistoryStatus(pending.Value()) != RewardClaimHistoryStatus::Claimed) return 12;
    const auto delivered = h.delivered;
    if (!s.Claim(pending.Value()) || h.delivered != delivered) return 13;

    Handler good("test.reward.good");
    Handler bad("test.reward.bad", true);
    LootService tx;
    if (!tx.RegisterRewardHandler("test.reward.good", &good) || !tx.RegisterRewardHandler("test.reward.bad", &bad)) return 14;
    RewardDefinition good_reward;
    good_reward.canonical_name = "test.reward.good.gold";
    good_reward.type = good.Type();
    good_reward.min_quantity_micro = good_reward.max_quantity_micro = 100;
    RewardDefinition bad_reward;
    bad_reward.canonical_name = "test.reward.bad.perk";
    bad_reward.type = bad.Type();
    bad_reward.min_quantity_micro = bad_reward.max_quantity_micro = 1;
    auto good_id = tx.RegisterRewardDefinition(good_reward);
    auto bad_id = tx.RegisterRewardDefinition(bad_reward);
    if (!good_id || !bad_id) return 15;
    LootTableDefinition tx_table;
    tx_table.canonical_name = "test.tx.table";
    tx_table.policy = LootRollPolicy::GuaranteedAll;
    tx_table.entries.push_back({LootEntryId::FromString("good"), 1, 1'000'000, good_id.Value(), {}, {}});
    tx_table.entries.push_back({LootEntryId::FromString("bad"), 1, 1'000'000, bad_id.Value(), {}, {}});
    auto tx_tid = tx.RegisterLootTable(tx_table);
    if (!tx_tid || !tx.Freeze()) return 16;
    auto bundle = tx.GenerateTracked(tx_tid.Value(), c);
    auto tx_pending = tx.MakePending(std::move(bundle).Value());
    if (!tx_pending) return 17;
    auto claim = tx.Claim(tx_pending.Value());
    if (claim || good.delivered != 0 || good.cancelled != 100 || !tx.FindPending(tx_pending.Value())) return 18;

    // M38: exceptions at provider/Prepare boundaries are converted to controlled failures.
    ThrowingConditionProvider throwing_condition;
    ConfiguredLoot conditioned;
    conditioned.service.SetConditionProvider(&throwing_condition);
    if (!Configure(conditioned, LootRollPolicy::GuaranteedAll, 1, TypeId::FromString("test.condition")) ||
        !conditioned.service.Freeze()) return 19;
    if (conditioned.service.Preview(conditioned.table_id, c)) return 20;

    Handler throwing("test.reward.throw", false, true);
    LootService throwing_service;
    if (!throwing_service.RegisterRewardHandler("test.reward.throw", &throwing)) return 21;
    RewardDefinition throwing_reward;
    throwing_reward.canonical_name = "test.reward.throw.value";
    throwing_reward.type = throwing.Type();
    throwing_reward.min_quantity_micro = throwing_reward.max_quantity_micro = 1;
    auto throwing_rid = throwing_service.RegisterRewardDefinition(throwing_reward);
    if (!throwing_rid) return 22;
    LootTableDefinition throwing_table;
    throwing_table.canonical_name = "test.throw.table";
    throwing_table.policy = LootRollPolicy::GuaranteedAll;
    throwing_table.entries.push_back({LootEntryId::FromString("throw"), 1, 1'000'000, throwing_rid.Value(), {}, {}});
    auto throwing_tid = throwing_service.RegisterLootTable(throwing_table);
    if (!throwing_tid || !throwing_service.Freeze()) return 23;
    auto throwing_bundle = throwing_service.GenerateTracked(throwing_tid.Value(), c);
    if (!throwing_bundle) return 24;
    auto throwing_pending = throwing_service.MakePending(std::move(throwing_bundle).Value());
    if (!throwing_pending || throwing_service.Claim(throwing_pending.Value())) return 25;
    if (!throwing_service.FindPending(throwing_pending.Value())) return 26;

    // Weighted configs with zero total weight are malformed rather than silently empty.
    ConfiguredLoot zero_weight;
    if (!Configure(zero_weight, LootRollPolicy::WeightedOne, 0)) return 27;
    if (zero_weight.service.Freeze()) return 28;

    // Recovery enumeration is global and deterministic, without requiring recipient knowledge.
    ConfiguredLoot recovery;
    if (!Configure(recovery) || !recovery.service.Freeze()) return 29;
    LootContext c2 = c;
    c2.seed = {43};
    auto g1 = recovery.service.GenerateTracked(recovery.table_id, c);
    auto g2 = recovery.service.GenerateTracked(recovery.table_id, c2);
    if (!g1 || !g2) return 30;
    auto p1 = recovery.service.MakePending(std::move(g1).Value(), GameplayTimePoint{100});
    auto p2 = recovery.service.MakePending(std::move(g2).Value(), GameplayTimePoint{200});
    if (!p1 || !p2) return 31;
    auto all = recovery.service.AllPending();
    if (all.size() != 2 || !(all[0].bundle.id < all[1].bundle.id)) return 32;

    // Generator restore rejects a foreign scope and a next value behind restored IDs.
    auto snapshot = recovery.service.CaptureSnapshot();
    auto bad_scope = snapshot;
    ++bad_scope.execution_ids.scope;
    if (recovery.service.RestoreSnapshot(std::move(bad_scope))) return 33;
    auto bad_next = snapshot;
    bad_next.execution_ids.next = all.back().bundle.id.value.Low();
    if (recovery.service.RestoreSnapshot(std::move(bad_next))) return 34;

    // M37: once retained claim history was truncated, old service-issued IDs report an expired horizon.
    auto history_snapshot = snapshot;
    history_snapshot.generated.clear();
    history_snapshot.pending.clear();
    history_snapshot.claimed.clear();
    history_snapshot.journal.clear();
    history_snapshot.next_change_sequence = 1;
    history_snapshot.claimed_history_floor_low = 10;
    history_snapshot.execution_ids.next = 11;
    if (!recovery.service.RestoreSnapshot(std::move(history_snapshot))) return 35;
    const auto scope = GameplayObjectId::FromString("framework.loot.executions").High();
    RewardExecutionId old_id{GameplayObjectId::FromRaw(scope, 5)};
    RewardExecutionId new_id{GameplayObjectId::FromRaw(scope, 12)};
    if (recovery.service.ClaimHistoryStatus(old_id) != RewardClaimHistoryStatus::HistoryExpired) return 36;
    if (recovery.service.ClaimHistoryStatus(new_id) != RewardClaimHistoryStatus::Unknown) return 37;

    return 0;
}
