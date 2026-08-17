#include "Epidemic/GameFramework/Knowledge/knowledge.h"
#include "Epidemic/Foundation/error.h"
#include <iterator>

namespace epidemic::gameplay::knowledge
{
namespace
{
foundation::Error Error(std::string_view c, std::string_view m)
{
    return foundation::Error::Create(c, m);
}
} // namespace
KnowledgeService::KnowledgeService() = default;
foundation::Result<void> KnowledgeService::CreateProfile(KnowledgeProfile p)
{
    if (!p.subject.IsValid() || profiles_.contains(p.subject))
        return foundation::Result<void>::Failure(
            Error("gameplay.knowledge.invalid_profile", "invalid or duplicate knowledge profile"));
    Bump();
    p.revision = revision_;
    profiles_.emplace(p.subject, std::move(p));
    return foundation::Result<void>::Success();
}
KnowledgeConfidence KnowledgeService::Degrade(KnowledgeConfidence c, KnowledgeShareMode mode) const noexcept
{
    if (mode == KnowledgeShareMode::Rumor)
    {
        return c >= KnowledgeConfidence::High ? KnowledgeConfidence::Medium : KnowledgeConfidence::Low;
    }
    if (mode == KnowledgeShareMode::Broadcast)
    {
        return c == KnowledgeConfidence::Certain ? KnowledgeConfidence::High : c;
    }
    return c == KnowledgeConfidence::None
               ? KnowledgeConfidence::None
               : static_cast<KnowledgeConfidence>(static_cast<int>(c) - ((mode == KnowledgeShareMode::Tell) ? 0 : 1));
}
foundation::Result<KnowledgeRecordId> KnowledgeService::Learn(LearnKnowledgeRequest r)
{
    if (!r.learner.IsValid() || !profiles_.contains(r.learner) || !r.topic.id.IsValid())
        return foundation::Result<KnowledgeRecordId>::Failure(
            Error("gameplay.knowledge.invalid_learn", "invalid knowledge learn request"));
    KnowledgeRecord *existing = nullptr;
    for (auto &[id, k] : knowledge_)
    {
        (void)id;
        if (k.owner == r.learner && k.topic == r.topic && k.type == r.type)
        {
            existing = &k;
            break;
        }
    }
    Bump();
    if (existing)
    {
        existing->confidence = std::max(existing->confidence, r.confidence);
        existing->truth_state =
            r.confidence >= KnowledgeConfidence::High ? KnowledgeTruthState::KnownTrue : KnowledgeTruthState::Suspected;
        existing->source = r.source_object;
        existing->last_confirmed_at = r.context.tick.IsValid()
                                          ? GameplayTimePoint{static_cast<std::int64_t>(r.context.tick.Raw())}
                                          : existing->last_confirmed_at;
        existing->payload = r.payload;
        existing->revision = revision_;
        ++diagnostics_.learn_ops;
        Record({0,
                KnowledgeChangeKind::Updated,
                r.learner,
                r.source_object,
                existing->id,
                {},
                r.topic.id,
                r.context,
                revision_});
        return foundation::Result<KnowledgeRecordId>::Success(existing->id);
    }
    auto id = KnowledgeRecordId{knowledge_ids_.Next()};
    KnowledgeRecord k;
    k.id = id;
    k.owner = r.learner;
    k.type = r.type;
    k.topic = r.topic;
    k.truth_state =
        r.confidence >= KnowledgeConfidence::High ? KnowledgeTruthState::KnownTrue : KnowledgeTruthState::Suspected;
    k.confidence = r.confidence;
    k.subject = r.topic.primary_subject;
    k.source = r.source_object;
    k.learned_at = GameplayTimePoint{static_cast<std::int64_t>(r.context.tick.Raw())};
    k.last_confirmed_at = k.learned_at;
    k.payload = r.payload;
    k.revision = revision_;
    knowledge_.emplace(id, k);
    ++diagnostics_.learn_ops;
    Record({0, KnowledgeChangeKind::Learned, r.learner, r.source_object, id, {}, r.topic.id, r.context, revision_});
    return foundation::Result<KnowledgeRecordId>::Success(id);
}
foundation::Result<MemoryRecordId> KnowledgeService::CreateMemory(CreateMemoryRequest r)
{
    if (!r.owner.IsValid() || !profiles_.contains(r.owner) || !r.type.IsValid())
        return foundation::Result<MemoryRecordId>::Failure(
            Error("gameplay.knowledge.invalid_memory", "invalid memory request"));
    Bump();
    auto id = MemoryRecordId{memory_ids_.Next()};
    MemoryRecord m;
    m.id = id;
    m.owner = r.owner;
    m.type = r.type;
    m.time = GameplayTimePoint{static_cast<std::int64_t>(r.context.tick.Raw())};
    m.subject = r.subject;
    m.area = r.area;
    m.importance = r.importance;
    m.persistence = r.persistence;
    m.decay_after = r.decay_after;
    m.payload = r.payload;
    m.revision = revision_;
    memories_.emplace(id, m);
    Record({0, KnowledgeChangeKind::MemoryCreated, r.owner, r.subject, {}, id, {}, r.context, revision_});
    return foundation::Result<MemoryRecordId>::Success(id);
}
foundation::Result<KnowledgeRecordId> KnowledgeService::Share(ShareKnowledgeRequest r)
{
    auto *src = FindKnowledge(r.record);
    if (!src || !profiles_.contains(r.listener) || !r.speaker.IsValid())
        return foundation::Result<KnowledgeRecordId>::Failure(
            Error("gameplay.knowledge.invalid_share", "invalid share request"));
    auto req = LearnKnowledgeRequest{r.listener,   src->type,
                                     src->topic,   KnowledgeSourceId::FromString("framework.knowledge.report"),
                                     r.speaker,    Degrade(src->confidence, r.mode),
                                     src->payload, r.context};
    auto res = Learn(req);
    if (res)
    {
        ++diagnostics_.shared_records;
        Record({0,
                KnowledgeChangeKind::Shared,
                r.listener,
                r.speaker,
                res.Value(),
                {},
                src->topic.id,
                r.context,
                revision_});
    }
    return res;
}
foundation::Result<void> KnowledgeService::Forget(KnowledgeRecordId id, GameplayContext context)
{
    auto it = knowledge_.find(id);
    if (it == knowledge_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.knowledge.missing_record", "knowledge record missing"));
    auto owner = it->second.owner;
    auto topic = it->second.topic.id;
    knowledge_.erase(it);
    Bump();
    ++diagnostics_.forgotten_records;
    Record({0, KnowledgeChangeKind::Forgotten, owner, {}, id, {}, topic, context, revision_});
    return foundation::Result<void>::Success();
}
KnowledgeRecord *KnowledgeService::FindMutableKnowledge(KnowledgeRecordId id) noexcept
{
    auto it = knowledge_.find(id);
    return it == knowledge_.end() ? nullptr : &it->second;
}
MemoryRecord *KnowledgeService::FindMutableMemory(MemoryRecordId id) noexcept
{
    auto it = memories_.find(id);
    return it == memories_.end() ? nullptr : &it->second;
}
foundation::Result<void> KnowledgeService::Contradict(KnowledgeRecordId old, KnowledgeRecord replacement,
                                                      GameplayContext context)
{
    auto *rec = FindMutableKnowledge(old);
    if (!rec || !replacement.owner.IsValid() || !replacement.topic.id.IsValid())
        return foundation::Result<void>::Failure(
            Error("gameplay.knowledge.invalid_contradiction", "invalid contradiction"));
    Bump();
    rec->truth_state = KnowledgeTruthState::Contradicted;
    rec->revision = revision_;
    replacement.id = KnowledgeRecordId{knowledge_ids_.Next()};
    replacement.revision = revision_;
    knowledge_.emplace(replacement.id, replacement);
    ++diagnostics_.contradictions;
    Record({0,
            KnowledgeChangeKind::Contradicted,
            rec->owner,
            replacement.owner,
            old,
            {},
            rec->topic.id,
            context,
            revision_});
    Record({0,
            KnowledgeChangeKind::Learned,
            replacement.owner,
            replacement.source,
            replacement.id,
            {},
            replacement.topic.id,
            context,
            revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> KnowledgeService::Decay(GameplayTimePoint now)
{
    std::vector<MemoryRecordId> forget_mem;
    for (const auto &[id, m] : memories_)
    {
        if (m.persistence == MemoryPersistencePolicy::Timed && m.decay_after.ticks > 0 &&
            now.ticks >= m.time.ticks + m.decay_after.ticks && m.importance != MemoryImportance::Critical)
            forget_mem.push_back(id);
    }
    std::sort(forget_mem.begin(), forget_mem.end());
    for (auto id : forget_mem)
    {
        auto it = memories_.find(id);
        if (it != memories_.end())
        {
            auto owner = it->second.owner;
            auto subject = it->second.subject;
            memories_.erase(it);
            Bump();
            Record({0, KnowledgeChangeKind::MemoryForgotten, owner, subject, {}, id, {}, {}, revision_});
        }
    }
    for (auto &[id, k] : knowledge_)
    {
        (void)id;
        if (k.confidence > KnowledgeConfidence::Low && k.persistence == MemoryPersistencePolicy::Timed &&
            now.ticks > k.learned_at.ticks + 86400)
        {
            Bump();
            k.confidence = static_cast<KnowledgeConfidence>(static_cast<int>(k.confidence) - 1);
            k.truth_state = k.confidence >= KnowledgeConfidence::High ? KnowledgeTruthState::KnownTrue
                                                                      : KnowledgeTruthState::Suspected;
            k.revision = revision_;
            Record({0, KnowledgeChangeKind::MemoryDecayed, k.owner, k.subject, k.id, {}, k.topic.id, {}, revision_});
        }
    }
    return foundation::Result<void>::Success();
}
const KnowledgeProfile *KnowledgeService::FindProfile(GameplayObjectRef s) const noexcept
{
    auto it = profiles_.find(s);
    return it == profiles_.end() ? nullptr : &it->second;
}
const KnowledgeRecord *KnowledgeService::FindKnowledge(KnowledgeRecordId id) const noexcept
{
    auto it = knowledge_.find(id);
    return it == knowledge_.end() ? nullptr : &it->second;
}
const MemoryRecord *KnowledgeService::FindMemory(MemoryRecordId id) const noexcept
{
    auto it = memories_.find(id);
    return it == memories_.end() ? nullptr : &it->second;
}
std::vector<KnowledgeRecord> KnowledgeService::FindKnowledgeByOwner(GameplayObjectRef o) const
{
    std::vector<KnowledgeRecord> out;
    for (const auto &[id, k] : knowledge_)
    {
        (void)id;
        if (k.owner == o)
            out.push_back(k);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return out;
}
std::vector<KnowledgeRecord> KnowledgeService::FindKnowledgeByTopic(GameplayObjectRef o, KnowledgeTopicId t) const
{
    std::vector<KnowledgeRecord> out;
    for (const auto &[id, k] : knowledge_)
    {
        (void)id;
        if (k.owner == o && k.topic.id == t)
            out.push_back(k);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return out;
}
std::vector<KnowledgeRecord> KnowledgeService::FindKnowledgeAboutSubject(GameplayObjectRef o, GameplayObjectRef s) const
{
    std::vector<KnowledgeRecord> out;
    for (const auto &[id, k] : knowledge_)
    {
        (void)id;
        if (k.owner == o && k.subject == s)
            out.push_back(k);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return out;
}
std::optional<KnowledgeRecord> KnowledgeService::GetLastKnownPosition(GameplayObjectRef o, GameplayObjectRef s) const
{
    std::optional<KnowledgeRecord> best;
    auto topic = KnowledgeTopicId::FromString("framework.knowledge.last_known_position");
    for (const auto &[id, k] : knowledge_)
    {
        (void)id;
        if (k.owner == o && k.subject == s && k.topic.id == topic &&
            (!best || k.last_confirmed_at > best->last_confirmed_at))
            best = k;
    }
    return best;
}
std::vector<MemoryRecord> KnowledgeService::FindMemoriesByOwner(GameplayObjectRef o) const
{
    std::vector<MemoryRecord> out;
    for (const auto &[id, m] : memories_)
    {
        (void)id;
        if (m.owner == o)
            out.push_back(m);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return out;
}
std::vector<MemoryRecord> KnowledgeService::FindImportantMemories(GameplayObjectRef o, MemoryImportance min) const
{
    std::vector<MemoryRecord> out;
    for (const auto &[id, m] : memories_)
    {
        (void)id;
        if (m.owner == o && static_cast<int>(m.importance) >= static_cast<int>(min))
            out.push_back(m);
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.id < b.id; });
    return out;
}
std::vector<KnowledgeChange> KnowledgeService::ChangesSince(std::uint64_t seq) const
{
    std::vector<KnowledgeChange> out;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(out),
                 [seq](const auto &c) { return c.sequence > seq; });
    return out;
}
KnowledgeSnapshot KnowledgeService::CaptureSnapshot() const
{
    KnowledgeSnapshot s;
    for (const auto &[o, p] : profiles_)
    {
        (void)o;
        s.profiles.push_back(p);
    }
    for (const auto &[id, k] : knowledge_)
    {
        (void)id;
        if (k.persistence != MemoryPersistencePolicy::Transient)
            s.knowledge.push_back(k);
    }
    for (const auto &[id, m] : memories_)
    {
        (void)id;
        if (m.persistence == MemoryPersistencePolicy::Persistent || m.persistence == MemoryPersistencePolicy::Timed)
            s.memories.push_back(m);
    }
    std::sort(s.profiles.begin(), s.profiles.end(), [](auto &a, auto &b) { return a.subject < b.subject; });
    std::sort(s.knowledge.begin(), s.knowledge.end(), [](auto &a, auto &b) { return a.id < b.id; });
    std::sort(s.memories.begin(), s.memories.end(), [](auto &a, auto &b) { return a.id < b.id; });
    s.knowledge_ids = knowledge_ids_.GetSnapshot();
    s.memory_ids = memory_ids_.GetSnapshot();
    s.revision = revision_;
    return s;
}
foundation::Result<void> KnowledgeService::RestoreSnapshot(KnowledgeSnapshot s)
{
    profiles_.clear();
    knowledge_.clear();
    memories_.clear();
    for (auto &p : s.profiles)
    {
        if (!p.subject.IsValid())
            return foundation::Result<void>::Failure(Error("gameplay.knowledge.restore_invalid", "invalid profile"));
        profiles_[p.subject] = p;
    }
    for (auto &k : s.knowledge)
    {
        if (!k.id.IsValid() || !profiles_.contains(k.owner))
            return foundation::Result<void>::Failure(Error("gameplay.knowledge.restore_invalid", "invalid knowledge"));
        knowledge_[k.id] = k;
    }
    for (auto &m : s.memories)
    {
        if (!m.id.IsValid() || !profiles_.contains(m.owner))
            return foundation::Result<void>::Failure(Error("gameplay.knowledge.restore_invalid", "invalid memory"));
        memories_[m.id] = m;
    }
    knowledge_ids_.Restore(s.knowledge_ids);
    memory_ids_.Restore(s.memory_ids);
    revision_ = s.revision;
    changes_.clear();
    next_change_sequence_ = 1;
    return foundation::Result<void>::Success();
}
KnowledgeDiagnostics KnowledgeService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_;
    d.profiles = profiles_.size();
    d.knowledge_records = knowledge_.size();
    d.memory_records = memories_.size();
    return d;
}
void KnowledgeService::Record(KnowledgeChange c)
{
    c.sequence = next_change_sequence_++;
    changes_.push_back(c);
}
} // namespace epidemic::gameplay::knowledge
