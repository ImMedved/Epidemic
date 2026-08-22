#include "Epidemic/GameFramework/Loot/loot.h"

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::loot;

namespace
{
class Handler final : public IRewardHandler
{
  public:
    explicit Handler(std::string_view name = "test.reward", bool fail_prepare = false)
        : type(RewardTypeId::FromString(name)), fail_prepare(fail_prepare)
    {
    }

    RewardTypeId type{};
    bool fail_prepare = false;
    std::int64_t delivered = 0;
    std::int64_t prepared = 0;
    std::int64_t cancelled = 0;

    [[nodiscard]] RewardTypeId Type() const noexcept override { return type; }
    [[nodiscard]] foundation::Result<RewardDeliveryDisposition> Validate(const RewardOperation&) const override { return foundation::Result<RewardDeliveryDisposition>::Success(RewardDeliveryDisposition::Delivered); }

    [[nodiscard]] foundation::Result<RewardDeliveryStage> Prepare(const RewardOperation& operation) override
    {
        if (fail_prepare)
        {
            return foundation::Result<RewardDeliveryStage>::Failure(foundation::Error::Create("prepare", "prepare failed"));
        }
        prepared += operation.quantity_micro;
        return foundation::Result<RewardDeliveryStage>::Success({operation, RewardDeliveryDisposition::Delivered});
    }
    void Commit(RewardDeliveryStage& stage) noexcept override { delivered += stage.operation.quantity_micro; }
    void Cancel(RewardDeliveryStage& stage) noexcept override { cancelled += stage.operation.quantity_micro; }
};
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
    auto a = s.Generate(tid.Value(), c);
    if (!a || a.Value().rewards.size() != 1) return 4;
    const auto quantity = a.Value().rewards[0].quantity_micro;

    LootService s2;
    Handler h2;
    auto h2r = s2.RegisterRewardHandler("test.reward", &h2);
    auto r2 = s2.RegisterRewardDefinition(r);
    auto t2 = s2.RegisterLootTable(t);
    auto f2 = s2.Freeze();
    if (!h2r || !r2 || !t2 || !f2) return 5;
    auto b = s2.Generate(tid.Value(), c);
    if (!b || b.Value().rewards[0].quantity_micro != quantity) return 5;
    auto pending = s.MakePending(std::move(a).Value());
    if (!pending || !s.Claim(pending.Value()) || !s.WasClaimed(pending.Value())) return 6;
    const auto delivered = h.delivered;
    if (!s.Claim(pending.Value()) || h.delivered != delivered) return 7;

    Handler good("test.reward.good");
    Handler bad("test.reward.bad", true);
    LootService tx;
    if (!tx.RegisterRewardHandler("test.reward.good", &good) || !tx.RegisterRewardHandler("test.reward.bad", &bad)) return 8;
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
    if (!good_id || !bad_id) return 9;
    LootTableDefinition tx_table;
    tx_table.canonical_name = "test.tx.table";
    tx_table.policy = LootRollPolicy::GuaranteedAll;
    tx_table.entries.push_back({LootEntryId::FromString("good"), 1, 1'000'000, good_id.Value(), {}, {}});
    tx_table.entries.push_back({LootEntryId::FromString("bad"), 1, 1'000'000, bad_id.Value(), {}, {}});
    auto tx_tid = tx.RegisterLootTable(tx_table);
    if (!tx_tid || !tx.Freeze()) return 10;
    auto bundle = tx.Generate(tx_tid.Value(), c);
    auto tx_pending = tx.MakePending(std::move(bundle).Value());
    if (!tx_pending) return 11;
    auto claim = tx.Claim(tx_pending.Value());
    if (claim || good.delivered != 0 || good.cancelled != 100 || !tx.FindPending(tx_pending.Value())) return 12;
    return 0;
}

