#include "Epidemic/GameFramework/Loot/loot.h"
#include "Epidemic/Foundation/error.h"
#include <algorithm>
#include <iterator>

namespace epidemic::gameplay::loot
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string c, std::string m)
{
    return foundation::Error::Create(std::move(c), std::move(m));
}
} // namespace
LootService::LootService() : execution_ids_(GameplayObjectId::FromString("framework.loot.executions").High())
{
}
foundation::Result<RewardTypeId> LootService::RegisterRewardHandler(std::string name, IRewardHandler *handler)
{
    if (frozen_)
        return foundation::Result<RewardTypeId>::Failure(Error("gameplay.registry_frozen", "loot registry frozen"));
    const auto id = RewardTypeId::FromString(name);
    if (name.empty() || !handler || handler->Type() != id || handlers_.contains(id))
        return foundation::Result<RewardTypeId>::Failure(
            Error("gameplay.loot.reward_handler_invalid", "invalid or duplicate reward handler"));
    handlers_[id] = handler;
    return foundation::Result<RewardTypeId>::Success(id);
}
foundation::Result<RewardDefinitionId> LootService::RegisterRewardDefinition(RewardDefinition d)
{
    if (frozen_)
        return foundation::Result<RewardDefinitionId>::Failure(
            Error("gameplay.registry_frozen", "loot registry frozen"));
    if (d.canonical_name.empty())
        return foundation::Result<RewardDefinitionId>::Failure(
            Error("gameplay.loot.reward_invalid", "reward name required"));
    const auto e = RewardDefinitionId::FromString(d.canonical_name);
    if (!d.id.IsValid())
        d.id = e;
    if (d.id != e || !d.type.IsValid() || d.min_quantity_micro < 0 || d.max_quantity_micro < d.min_quantity_micro ||
        rewards_.contains(d.id))
        return foundation::Result<RewardDefinitionId>::Failure(
            Error("gameplay.loot.reward_invalid", "invalid or duplicate reward definition"));
    const auto id = d.id;
    rewards_.emplace(id, std::move(d));
    return foundation::Result<RewardDefinitionId>::Success(id);
}
foundation::Result<LootTableId> LootService::RegisterLootTable(LootTableDefinition d)
{
    if (frozen_)
        return foundation::Result<LootTableId>::Failure(Error("gameplay.registry_frozen", "loot registry frozen"));
    if (d.canonical_name.empty() || d.entries.empty())
        return foundation::Result<LootTableId>::Failure(
            Error("gameplay.loot.table_invalid", "loot table name and entries required"));
    const auto e = LootTableId::FromString(d.canonical_name);
    if (!d.id.IsValid())
        d.id = e;
    if (d.id != e || tables_.contains(d.id) || d.rolls == 0)
        return foundation::Result<LootTableId>::Failure(
            Error("gameplay.loot.table_invalid", "invalid or duplicate loot table"));
    std::unordered_set<LootEntryId, IdHash> ids;
    for (const auto &entry : d.entries)
    {
        if (!entry.id.IsValid() || (!entry.reward.IsValid() && !entry.nested_table.IsValid()) ||
            (entry.reward.IsValid() && entry.nested_table.IsValid()) || entry.chance_micro > 1'000'000 ||
            !ids.insert(entry.id).second)
            return foundation::Result<LootTableId>::Failure(Error("gameplay.loot.entry_invalid", "invalid loot entry"));
    }
    const auto id = d.id;
    tables_.emplace(id, std::move(d));
    return foundation::Result<LootTableId>::Success(id);
}
foundation::Result<void> LootService::ValidateNestedTables() const
{
    std::unordered_map<LootTableId, std::uint8_t, IdHash> marks;
    const auto visit = [&](auto &&self, LootTableId id) -> bool {
        auto it = tables_.find(id);
        if (it == tables_.end())
            return false;
        auto &m = marks[id];
        if (m == 1)
            return false;
        if (m == 2)
            return true;
        m = 1;
        for (const auto &e : it->second.entries)
        {
            if (e.reward.IsValid() && !rewards_.contains(e.reward))
                return false;
            if (e.nested_table.IsValid() && !self(self, e.nested_table))
                return false;
        }
        m = 2;
        return true;
    };
    for (const auto &[id, t] : tables_)
    {
        (void)t;
        if (!visit(visit, id))
            return foundation::Result<void>::Failure(
                Error("gameplay.loot.table_cycle", "loot table has cycle or unknown reference"));
    }
    return foundation::Result<void>::Success();
}
foundation::Result<void> LootService::Freeze()
{
    auto valid = ValidateNestedTables();
    if (!valid)
        return valid;
    for (const auto &[id, r] : rewards_)
    {
        (void)id;
        if (!handlers_.contains(r.type))
            return foundation::Result<void>::Failure(
                Error("gameplay.loot.reward_handler_missing", "reward definition has no handler"));
    }
    frozen_ = true;
    return foundation::Result<void>::Success();
}
bool LootService::EntryAllowed(const LootEntry &e, const LootContext &c) const
{
    return !e.condition.IsValid() || (conditions_ && conditions_->Evaluate(e.condition, c));
}
foundation::Result<void> LootService::EmitEntry(const LootEntry &e, const LootContext &c, random::RandomSequence &seq,
                                                std::vector<RewardOperation> &out, std::uint32_t depth)
{
    ++diagnostics_.entries_evaluated;
    if (!EntryAllowed(e, c))
        return foundation::Result<void>::Success();
    if (e.nested_table.IsValid())
        return GenerateTable(e.nested_table, c, seq, out, depth + 1);
    auto r = rewards_.find(e.reward);
    if (r == rewards_.end())
        return foundation::Result<void>::Failure(Error("gameplay.loot.reward_missing", "reward definition missing"));
    const auto range = static_cast<std::uint64_t>(r->second.max_quantity_micro - r->second.min_quantity_micro);
    const auto quantity = r->second.min_quantity_micro + static_cast<std::int64_t>(seq.Uniform(range + 1));
    out.push_back(
        {r->second.type, r->second.id, c.recipient, quantity, r->second.quality, r->second.payload, c.gameplay});
    return foundation::Result<void>::Success();
}
foundation::Result<void> LootService::GenerateTable(LootTableId id, const LootContext &c, random::RandomSequence &seq,
                                                    std::vector<RewardOperation> &out, std::uint32_t depth)
{
    diagnostics_.max_nested_depth = std::max<std::uint64_t>(diagnostics_.max_nested_depth, depth);
    if (depth > 64)
        return foundation::Result<void>::Failure(Error("gameplay.loot.depth_exceeded", "nested loot depth exceeded"));
    auto it = tables_.find(id);
    if (it == tables_.end())
        return foundation::Result<void>::Failure(Error("gameplay.loot.table_missing", "loot table missing"));
    const auto &table = it->second;
    ++diagnostics_.rolls;
    std::vector<const LootEntry *> allowed;
    for (const auto &e : table.entries)
        if (EntryAllowed(e, c))
            allowed.push_back(&e);
    if (allowed.empty())
        return foundation::Result<void>::Success();
    const auto pickWeighted = [&](const std::vector<const LootEntry *> &pool) -> const LootEntry * {
        std::uint64_t total = 0;
        for (auto *e : pool)
            total += e->weight;
        if (total == 0)
            return nullptr;
        auto roll = seq.Uniform(total);
        for (auto *e : pool)
        {
            if (roll < e->weight)
                return e;
            roll -= e->weight;
        }
        return pool.back();
    };
    switch (table.policy)
    {
    case LootRollPolicy::WeightedOne: {
        auto *e = pickWeighted(allowed);
        if (e)
        {
            auto r = EmitEntry(*e, c, seq, out, depth);
            if (!r)
                return r;
        }
        break;
    }
    case LootRollPolicy::WeightedMany:
        for (std::uint32_t n = 0; n < table.rolls; ++n)
        {
            auto *e = pickWeighted(allowed);
            if (e)
            {
                auto r = EmitEntry(*e, c, seq, out, depth);
                if (!r)
                    return r;
            }
        }
        break;
    case LootRollPolicy::IndependentChance:
        for (auto *e : allowed)
            if (seq.RollMicro(e->chance_micro))
            {
                auto r = EmitEntry(*e, c, seq, out, depth);
                if (!r)
                    return r;
            }
        break;
    case LootRollPolicy::GuaranteedAll:
        for (auto *e : allowed)
        {
            auto r = EmitEntry(*e, c, seq, out, depth);
            if (!r)
                return r;
        }
        break;
    case LootRollPolicy::PickNWithoutReplacement: {
        auto pool = allowed;
        const auto count = std::min<std::size_t>(table.rolls, pool.size());
        for (std::size_t n = 0; n < count; ++n)
        {
            auto *e = pickWeighted(pool);
            if (!e)
                break;
            auto r = EmitEntry(*e, c, seq, out, depth);
            if (!r)
                return r;
            pool.erase(std::find(pool.begin(), pool.end(), e));
        }
        break;
    }
    }
    return foundation::Result<void>::Success();
}
foundation::Result<RewardBundle> LootService::Generate(LootTableId table, LootContext context)
{
    if (!frozen_)
        return foundation::Result<RewardBundle>::Failure(
            Error("gameplay.loot.not_frozen", "loot registries must be frozen before generation"));
    RewardBundle bundle;
    bundle.id = {execution_ids_.Next()};
    bundle.seed = context.seed;
    bundle.context = context.gameplay;
    random::RandomSequence seq(context.seed, random::RandomStream::FromString("loot.table"));
    auto r = GenerateTable(table, context, seq, bundle.rewards, 0);
    if (!r)
        return foundation::Result<RewardBundle>::Failure(r.GetError());
    ++diagnostics_.bundles;
    Record({0, LootChangeKind::Generated, bundle.id, context.recipient, context.gameplay});
    return foundation::Result<RewardBundle>::Success(std::move(bundle));
}
foundation::Result<RewardExecutionId> LootService::MakePending(RewardBundle bundle,
                                                               std::optional<GameplayTimePoint> expires)
{
    if (!bundle.id.IsValid() || pending_.contains(bundle.id) || claimed_.contains(bundle.id))
        return foundation::Result<RewardExecutionId>::Failure(
            Error("gameplay.loot.pending_invalid", "invalid or duplicate pending reward"));
    PendingReward p{std::move(bundle), PendingRewardState::Available, expires, {}, {1}};
    const auto id = p.bundle.id;
    const auto recipient = p.bundle.rewards.empty() ? GameplayObjectRef{} : p.bundle.rewards.front().recipient;
    pending_.emplace(id, std::move(p));
    diagnostics_.pending = pending_.size();
    Record({0, LootChangeKind::Available, id, recipient, {}});
    return foundation::Result<RewardExecutionId>::Success(id);
}
foundation::Result<void> LootService::BindSchedule(RewardExecutionId id, ScheduleId s)
{
    auto it = pending_.find(id);
    if (it == pending_.end() || !s.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.loot.schedule_invalid", "pending reward or schedule invalid"));
    it->second.schedule = s;
    ++it->second.revision.value;
    return foundation::Result<void>::Success();
}
foundation::Result<void> LootService::Claim(RewardExecutionId id, GameplayContext context)
{
    if (claimed_.contains(id))
        return foundation::Result<void>::Success();
    auto it = pending_.find(id);
    if (it == pending_.end() || it->second.state != PendingRewardState::Available)
        return foundation::Result<void>::Failure(Error("gameplay.loot.reward_unavailable", "reward is not available"));
    std::vector<std::pair<IRewardHandler *, RewardDeliveryStage>> stages;
    stages.reserve(it->second.bundle.rewards.size());
    for (const auto &op : it->second.bundle.rewards)
    {
        auto h = handlers_.find(op.type);
        if (h == handlers_.end())
            return foundation::Result<void>::Failure(Error("gameplay.loot.handler_missing", "reward handler missing"));
        auto prepared = h->second->Prepare(op);
        if (!prepared || prepared.Value().disposition == RewardDeliveryDisposition::Rejected ||
            prepared.Value().disposition == RewardDeliveryDisposition::Unavailable)
        {
            for (auto &stage : stages)
                stage.first->Cancel(stage.second);
            ++diagnostics_.delivery_failures;
            Record({0, LootChangeKind::DeliveryFailed, id, op.recipient, context});
            return prepared ? foundation::Result<void>::Failure(
                                  Error("gameplay.loot.delivery_rejected", "reward delivery rejected"))
                            : foundation::Result<void>::Failure(prepared.GetError());
        }
        stages.push_back({h->second, std::move(prepared).Value()});
    }
    for (auto &stage : stages)
        stage.first->Commit(stage.second);
    it->second.state = PendingRewardState::Claimed;
    ++it->second.revision.value;
    claimed_.insert(id);
    ++diagnostics_.claims;
    const auto recipient =
        it->second.bundle.rewards.empty() ? GameplayObjectRef{} : it->second.bundle.rewards.front().recipient;
    Record({0, LootChangeKind::Claimed, id, recipient, context});
    pending_.erase(it);
    diagnostics_.pending = pending_.size();
    return foundation::Result<void>::Success();
}
foundation::Result<void> LootService::Expire(RewardExecutionId id, GameplayTimePoint now, GameplayContext context)
{
    auto it = pending_.find(id);
    if (it == pending_.end())
        return foundation::Result<void>::Failure(Error("gameplay.loot.pending_missing", "pending reward missing"));
    if (!it->second.expires_at || it->second.expires_at->ticks > now.ticks)
        return foundation::Result<void>::Failure(Error("gameplay.loot.not_due", "reward not due to expire"));
    it->second.state = PendingRewardState::Expired;
    ++it->second.revision.value;
    Record({0, LootChangeKind::Expired, id,
            it->second.bundle.rewards.empty() ? GameplayObjectRef{} : it->second.bundle.rewards.front().recipient,
            context});
    return foundation::Result<void>::Success();
}
foundation::Result<void> LootService::Cancel(RewardExecutionId id, GameplayContext context)
{
    auto it = pending_.find(id);
    if (it == pending_.end())
        return foundation::Result<void>::Failure(Error("gameplay.loot.pending_missing", "pending reward missing"));
    it->second.state = PendingRewardState::Cancelled;
    ++it->second.revision.value;
    Record({0, LootChangeKind::Cancelled, id,
            it->second.bundle.rewards.empty() ? GameplayObjectRef{} : it->second.bundle.rewards.front().recipient,
            context});
    return foundation::Result<void>::Success();
}
const PendingReward *LootService::FindPending(RewardExecutionId id) const noexcept
{
    auto it = pending_.find(id);
    return it == pending_.end() ? nullptr : &it->second;
}
std::vector<PendingReward> LootService::PendingFor(GameplayObjectRef recipient) const
{
    std::vector<PendingReward> o;
    for (const auto &[id, p] : pending_)
    {
        (void)id;
        if (!p.bundle.rewards.empty() && p.bundle.rewards.front().recipient == recipient)
            o.push_back(p);
    }
    std::sort(o.begin(), o.end(), [](auto &a, auto &b) { return a.bundle.id < b.bundle.id; });
    return o;
}
bool LootService::WasClaimed(RewardExecutionId id) const noexcept
{
    return claimed_.contains(id);
}
std::vector<LootChange> LootService::ChangesSince(std::uint64_t s) const
{
    std::vector<LootChange> o;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(o),
                 [&](const auto &c) { return c.sequence > s; });
    return o;
}
LootSnapshot LootService::CaptureSnapshot() const
{
    LootSnapshot s;
    s.execution_ids = execution_ids_.GetSnapshot();
    for (const auto &[id, p] : pending_)
    {
        (void)id;
        s.pending.push_back(p);
    }
    s.claimed.assign(claimed_.begin(), claimed_.end());
    std::sort(s.pending.begin(), s.pending.end(), [](auto &a, auto &b) { return a.bundle.id < b.bundle.id; });
    std::sort(s.claimed.begin(), s.claimed.end());
    return s;
}
foundation::Result<void> LootService::RestoreSnapshot(LootSnapshot s)
{
    pending_.clear();
    claimed_.clear();
    for (auto &p : s.pending)
    {
        if (!p.bundle.id.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.loot.restore_invalid", "invalid pending reward"));
        pending_.emplace(p.bundle.id, std::move(p));
    }
    claimed_.insert(s.claimed.begin(), s.claimed.end());
    execution_ids_.Restore(s.execution_ids);
    changes_.clear();
    next_change_sequence_ = 1;
    diagnostics_.pending = pending_.size();
    return foundation::Result<void>::Success();
}
LootDiagnostics LootService::GetDiagnostics() const noexcept
{
    return diagnostics_;
}
void LootService::Record(LootChange c)
{
    c.sequence = next_change_sequence_++;
    changes_.push_back(std::move(c));
}
} // namespace epidemic::gameplay::loot
