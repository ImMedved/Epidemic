#include "Epidemic/GameFramework/Knowledge/knowledge.h"
#include "Epidemic/Foundation/error.h"

#include <limits>
#include <unordered_set>

namespace epidemic::gameplay::knowledge
{
namespace
{
foundation::Error Error(std::string_view c,std::string_view m){return foundation::Error::Create(c,m);}

template<class T> bool EnumInRange(T value,T last) noexcept
{
    const auto v=static_cast<int>(value); return v>=0&&v<=static_cast<int>(last);
}
int EpistemicRank(KnowledgeEpistemicState s) noexcept
{
    switch(s){case KnowledgeEpistemicState::Known:return 4;case KnowledgeEpistemicState::Suspected:return 3;case KnowledgeEpistemicState::Rumor:return 2;case KnowledgeEpistemicState::Outdated:return 1;case KnowledgeEpistemicState::Contradicted:return 0;} return 0;
}
KnowledgeEpistemicState Stronger(KnowledgeEpistemicState a,KnowledgeEpistemicState b) noexcept{return EpistemicRank(b)>EpistemicRank(a)?b:a;}

bool ValidateGenerator(MonotonicIdGenerator<GameplayObjectId>::Snapshot snapshot,std::uint64_t expected_scope,std::uint64_t max_low) noexcept
{
    if(!MonotonicIdGenerator<GameplayObjectId>::IsValidSnapshot(snapshot)||snapshot.scope!=expected_scope)return false;
    if(snapshot.next==0)return true;
    return snapshot.next>max_low;
}

template<class T> void InsertSorted(std::vector<T>& values,T value)
{
    const auto it=std::lower_bound(values.begin(),values.end(),value); if(it==values.end()||*it!=value)values.insert(it,value);
}
template<class T> void EraseSorted(std::vector<T>& values,T value)
{
    const auto it=std::lower_bound(values.begin(),values.end(),value); if(it!=values.end()&&*it==value)values.erase(it);
}

KnowledgeSourceId ShareSource(KnowledgeShareMode mode) noexcept
{
    switch(mode)
    {
    case KnowledgeShareMode::Tell:return KnowledgeSourceId::FromString("framework.knowledge.tell");
    case KnowledgeShareMode::Report:return KnowledgeSourceId::FromString("framework.knowledge.report");
    case KnowledgeShareMode::Rumor:return KnowledgeSourceId::FromString("framework.knowledge.rumor");
    case KnowledgeShareMode::Broadcast:return KnowledgeSourceId::FromString("framework.knowledge.broadcast");
    case KnowledgeShareMode::GroupSync:return KnowledgeSourceId::FromString("framework.knowledge.group_sync");
    }
    return KnowledgeSourceId::FromString("framework.knowledge.report");
}
} // namespace

KnowledgeService::KnowledgeService(std::size_t change_capacity):change_capacity_(std::max<std::size_t>(1,change_capacity)){}

foundation::Result<void> KnowledgeService::RegisterDecayRule(MemoryDecayRule rule)
{
    if(definitions_frozen_)return foundation::Result<void>::Failure(Error("gameplay.knowledge.registry_frozen","knowledge definitions are frozen"));
    if(!rule.id.IsValid()||rule.canonical_name.empty()||rule.interval.ticks<=0||rule.confidence_steps==0||decay_rules_.contains(rule.id))
        return foundation::Result<void>::Failure(Error("gameplay.knowledge.invalid_decay_rule","invalid or duplicate memory decay rule"));
    if(rule.outdated_at_or_below&&!EnumInRange(*rule.outdated_at_or_below,KnowledgeConfidence::Certain))
        return foundation::Result<void>::Failure(Error("gameplay.knowledge.invalid_decay_rule","invalid outdated confidence threshold"));
    if(rule.forget_at_or_below&&!EnumInRange(*rule.forget_at_or_below,KnowledgeConfidence::Certain))
        return foundation::Result<void>::Failure(Error("gameplay.knowledge.invalid_decay_rule","invalid forget confidence threshold"));
    decay_rules_.emplace(rule.id,std::move(rule)); return foundation::Result<void>::Success();
}
foundation::Result<void> KnowledgeService::FreezeDefinitions(){definitions_frozen_=true;return foundation::Result<void>::Success();}

foundation::Result<void> KnowledgeService::CreateProfile(KnowledgeProfile p)
{
    if(!p.subject.IsValid()||profiles_.contains(p.subject)||!EnumInRange(p.retention,MemoryPersistencePolicy::Timed))
        return foundation::Result<void>::Failure(Error("gameplay.knowledge.invalid_profile","invalid or duplicate knowledge profile"));
    if(p.default_decay_rule.IsValid()&&!decay_rules_.contains(p.default_decay_rule))
        return foundation::Result<void>::Failure(Error("gameplay.knowledge.invalid_profile","knowledge profile references unknown decay rule"));
    definitions_frozen_=true;
    Bump(); p.revision=revision_; profiles_.emplace(p.subject,std::move(p)); return foundation::Result<void>::Success();
}

foundation::Result<void> KnowledgeService::RemoveProfile(GameplayObjectRef subject,GameplayContext context)
{
    auto pit=profiles_.find(subject); if(pit==profiles_.end())return foundation::Result<void>::Failure(Error("gameplay.knowledge.missing_profile","knowledge profile missing"));
    auto knowledge_ids=knowledge_by_owner_[subject]; auto memory_ids=memories_by_owner_[subject];
    for(auto id:knowledge_ids){auto it=knowledge_.find(id);if(it!=knowledge_.end()){UnindexKnowledge(it->second);knowledge_.erase(it);}}
    for(auto id:memory_ids){auto it=memories_.find(id);if(it!=memories_.end()){UnindexMemory(it->second);memories_.erase(it);}}
    profiles_.erase(pit); diagnostics_.forgotten_records+=knowledge_ids.size();
    Bump(); Record({0,KnowledgeChangeKind::ProfileRemoved,subject,{}, {},{}, {},context,revision_}); return foundation::Result<void>::Success();
}

KnowledgeConfidence KnowledgeService::Degrade(KnowledgeConfidence c,KnowledgeShareMode mode)const noexcept
{
    if(mode==KnowledgeShareMode::Rumor)return c>=KnowledgeConfidence::High?KnowledgeConfidence::Medium:(c==KnowledgeConfidence::None?c:KnowledgeConfidence::Low);
    if(mode==KnowledgeShareMode::Broadcast)return c==KnowledgeConfidence::Certain?KnowledgeConfidence::High:c;
    if(mode==KnowledgeShareMode::Tell)return c;
    return c==KnowledgeConfidence::None?c:static_cast<KnowledgeConfidence>(static_cast<int>(c)-1);
}

foundation::Result<KnowledgeRecordId> KnowledgeService::Learn(LearnKnowledgeRequest r)
{
    return LearnInternal(std::move(r),{},{},{},0);
}
foundation::Result<KnowledgeRecordId> KnowledgeService::LearnInternal(LearnKnowledgeRequest r,KnowledgeRecordId derived_from,KnowledgeSourceId original_source_kind,GameplayObjectRef original_source,std::uint32_t transmission_depth)
{
    auto pit=profiles_.find(r.learner);
    if(!r.learner.IsValid()||pit==profiles_.end()||!r.type.IsValid()||!r.topic.id.IsValid()||!r.source_kind.IsValid()||
       !EnumInRange(r.assertion,KnowledgeAssertionValue::Denied)||!EnumInRange(r.epistemic_state,KnowledgeEpistemicState::Outdated)||!EnumInRange(r.confidence,KnowledgeConfidence::Certain))
        return foundation::Result<KnowledgeRecordId>::Failure(Error("gameplay.knowledge.invalid_learn","invalid knowledge learn request"));
    const auto persistence=r.persistence.value_or(pit->second.retention);
    if(!EnumInRange(persistence,MemoryPersistencePolicy::Timed))return foundation::Result<KnowledgeRecordId>::Failure(Error("gameplay.knowledge.invalid_learn","invalid persistence policy"));
    const auto decay_rule=r.decay_rule.value_or(pit->second.default_decay_rule);
    if(decay_rule.IsValid()&&!decay_rules_.contains(decay_rule))return foundation::Result<KnowledgeRecordId>::Failure(Error("gameplay.knowledge.invalid_learn","unknown decay rule"));
    if(transmission_depth>0&&(!derived_from.IsValid()||!original_source_kind.IsValid()))return foundation::Result<KnowledgeRecordId>::Failure(Error("gameplay.knowledge.invalid_provenance","invalid knowledge provenance"));

    KnowledgeRecord* existing=nullptr;
    auto owner_it=knowledge_by_owner_.find(r.learner);
    if(owner_it!=knowledge_by_owner_.end())for(auto id:owner_it->second){auto* k=FindMutableKnowledge(id);if(k&&k->topic==r.topic&&k->type==r.type&&k->assertion==r.assertion){existing=k;break;}}
    if(existing)
    {
        Bump(); existing->confidence=std::max(existing->confidence,r.confidence); existing->epistemic_state=Stronger(existing->epistemic_state,r.epistemic_state);
        existing->topic = r.topic;
        existing->source_kind=r.source_kind; existing->source=r.source_object; existing->derived_from=derived_from;
        existing->original_source_kind=original_source_kind.IsValid()?original_source_kind:r.source_kind; existing->original_source=original_source.IsValid()?original_source:r.source_object;
        existing->transmission_depth=transmission_depth; existing->last_confirmed_at=r.context.time; existing->last_decay_at=r.context.time; existing->persistence=persistence;
        existing->decay_rule=decay_rule; existing->payload=std::move(r.payload); existing->revision=revision_; ++diagnostics_.learn_ops;
        Record({0,KnowledgeChangeKind::Updated,r.learner,r.source_object,existing->id,{},r.topic.id,r.context,revision_});
        return foundation::Result<KnowledgeRecordId>::Success(existing->id);
    }
    auto id=KnowledgeRecordId{knowledge_ids_.Next()}; if(!id.IsValid())return foundation::Result<KnowledgeRecordId>::Failure(Error("gameplay.knowledge.id_exhausted","knowledge id generator exhausted"));
    Bump(); KnowledgeRecord k; k.id=id;k.owner=r.learner;k.type=r.type;k.topic=std::move(r.topic);k.assertion=r.assertion;k.epistemic_state=r.epistemic_state;k.confidence=r.confidence;
    k.subject=k.topic.primary_subject;k.source_kind=r.source_kind;k.source=r.source_object;k.derived_from=derived_from;k.original_source_kind=original_source_kind.IsValid()?original_source_kind:r.source_kind;
    k.original_source=original_source.IsValid()?original_source:r.source_object;k.transmission_depth=transmission_depth;k.learned_at=r.context.time;k.last_confirmed_at=r.context.time;k.last_decay_at=r.context.time;
    k.decay_rule=decay_rule;k.persistence=persistence;k.payload=std::move(r.payload);k.revision=revision_;
    knowledge_.emplace(id,k);IndexKnowledge(k);++diagnostics_.learn_ops;Record({0,KnowledgeChangeKind::Learned,r.learner,r.source_object,id,{},k.topic.id,r.context,revision_});
    return foundation::Result<KnowledgeRecordId>::Success(id);
}

foundation::Result<MemoryRecordId> KnowledgeService::CreateMemory(CreateMemoryRequest r)
{
    auto pit=profiles_.find(r.owner);
    if(!r.owner.IsValid()||pit==profiles_.end()||!r.type.IsValid()||!EnumInRange(r.importance,MemoryImportance::Critical)||r.decay_after.ticks<0)
        return foundation::Result<MemoryRecordId>::Failure(Error("gameplay.knowledge.invalid_memory","invalid memory request"));
    const auto persistence=r.persistence.value_or(pit->second.retention); if(!EnumInRange(persistence,MemoryPersistencePolicy::Timed))return foundation::Result<MemoryRecordId>::Failure(Error("gameplay.knowledge.invalid_memory","invalid memory persistence"));
    auto id=MemoryRecordId{memory_ids_.Next()};if(!id.IsValid())return foundation::Result<MemoryRecordId>::Failure(Error("gameplay.knowledge.id_exhausted","memory id generator exhausted"));
    Bump();MemoryRecord m; m.id=id;m.owner=r.owner;m.type=r.type;m.time=r.context.time;m.subject=r.subject;m.area=r.area;m.importance=r.importance;m.persistence=persistence;m.decay_after=r.decay_after;m.payload=std::move(r.payload);m.revision=revision_;
    memories_.emplace(id,m);IndexMemory(m);Record({0,KnowledgeChangeKind::MemoryCreated,r.owner,r.subject,{},id,{},r.context,revision_});return foundation::Result<MemoryRecordId>::Success(id);
}

foundation::Result<MemoryRecordId> KnowledgeService::CompactMemories(GameplayObjectRef owner,std::span<const MemoryRecordId> source_ids,CreateMemoryRequest summary,GameplayContext context)
{
    if(!owner.IsValid()||source_ids.empty()||summary.owner!=owner||!summary.type.IsValid())return foundation::Result<MemoryRecordId>::Failure(Error("gameplay.knowledge.invalid_compaction","invalid memory compaction request"));
    std::vector<MemoryRecordId> ids(source_ids.begin(),source_ids.end());std::sort(ids.begin(),ids.end());if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())return foundation::Result<MemoryRecordId>::Failure(Error("gameplay.knowledge.invalid_compaction","duplicate source memory"));
    for(auto id:ids){auto it=memories_.find(id);if(it==memories_.end()||it->second.owner!=owner)return foundation::Result<MemoryRecordId>::Failure(Error("gameplay.knowledge.invalid_compaction","source memory missing or owned by another subject"));}
    auto pit=profiles_.find(owner);if(pit==profiles_.end())return foundation::Result<MemoryRecordId>::Failure(Error("gameplay.knowledge.missing_profile","knowledge profile missing"));
    const auto persistence=summary.persistence.value_or(pit->second.retention);if(summary.decay_after.ticks<0||!EnumInRange(persistence,MemoryPersistencePolicy::Timed))return foundation::Result<MemoryRecordId>::Failure(Error("gameplay.knowledge.invalid_compaction","invalid summary memory"));
    auto new_id=MemoryRecordId{memory_ids_.Next()};if(!new_id.IsValid())return foundation::Result<MemoryRecordId>::Failure(Error("gameplay.knowledge.id_exhausted","memory id generator exhausted"));
    if(context.time.ticks!=0||summary.context.time.ticks==0)summary.context=context;
    Bump();MemoryRecord m; m.id=new_id;m.owner=owner;m.type=summary.type;m.time=summary.context.time;m.subject=summary.subject;m.area=summary.area;m.importance=summary.importance;m.persistence=persistence;m.decay_after=summary.decay_after;m.payload=std::move(summary.payload);m.revision=revision_;
    for(auto id:ids){auto it=memories_.find(id);UnindexMemory(it->second);memories_.erase(it);}memories_.emplace(new_id,m);IndexMemory(m);++diagnostics_.compacted_memories;
    Record({0,KnowledgeChangeKind::MemoryCompacted,owner,summary.subject,{},new_id,{},context,revision_});return foundation::Result<MemoryRecordId>::Success(new_id);
}

bool KnowledgeService::CanShareTopic(const KnowledgeProfile& profile,const KnowledgeTopic& topic)const noexcept
{
    const auto& allowed=profile.sharing_tags.Values();if(allowed.empty())return true;const auto& actual=topic.tags.Values();
    std::size_t i=0,j=0;while(i<allowed.size()&&j<actual.size()){if(allowed[i]==actual[j])return true;if(allowed[i]<actual[j])++i;else ++j;}return false;
}
foundation::Result<KnowledgeRecordId> KnowledgeService::Share(ShareKnowledgeRequest r)
{
    const auto* src=FindKnowledge(r.record);auto speaker=profiles_.find(r.speaker);auto listener=profiles_.find(r.listener);
    if(!src||src->owner!=r.speaker||speaker==profiles_.end()||listener==profiles_.end()||!r.speaker.IsValid()||!r.listener.IsValid()||!CanShareTopic(speaker->second,src->topic))
        return foundation::Result<KnowledgeRecordId>::Failure(Error("gameplay.knowledge.invalid_share","share is not authorized by source ownership or profile policy"));
    if(src->transmission_depth==std::numeric_limits<std::uint32_t>::max())return foundation::Result<KnowledgeRecordId>::Failure(Error("gameplay.knowledge.share_depth_exhausted","knowledge transmission depth exhausted"));
    LearnKnowledgeRequest req;req.learner=r.listener;req.type=src->type;req.topic=src->topic;req.source_kind=ShareSource(r.mode);req.source_object=r.speaker;req.assertion=src->assertion;
    req.epistemic_state=r.mode==KnowledgeShareMode::Rumor?KnowledgeEpistemicState::Rumor:src->epistemic_state;req.confidence=Degrade(src->confidence,r.mode);req.payload=src->payload;req.context=r.context;
    auto res=LearnInternal(std::move(req),src->id,src->original_source_kind.IsValid()?src->original_source_kind:src->source_kind,src->original_source.IsValid()?src->original_source:src->source,src->transmission_depth+1);
    if(res){++diagnostics_.shared_records;Record({0,KnowledgeChangeKind::Shared,r.listener,r.speaker,res.Value(),{},src->topic.id,r.context,revision_});}return res;
}

foundation::Result<void> KnowledgeService::Forget(KnowledgeRecordId id,GameplayContext context)
{
    auto it=knowledge_.find(id);if(it==knowledge_.end())return foundation::Result<void>::Failure(Error("gameplay.knowledge.missing_record","knowledge record missing"));
    auto copy=it->second;UnindexKnowledge(copy);knowledge_.erase(it);Bump();++diagnostics_.forgotten_records;Record({0,KnowledgeChangeKind::Forgotten,copy.owner,{},id,{},copy.topic.id,context,revision_});return foundation::Result<void>::Success();
}
KnowledgeRecord* KnowledgeService::FindMutableKnowledge(KnowledgeRecordId id)noexcept{auto it=knowledge_.find(id);return it==knowledge_.end()?nullptr:&it->second;}

foundation::Result<KnowledgeRecordId> KnowledgeService::Contradict(ContradictKnowledgeRequest r)
{
    auto* old=FindMutableKnowledge(r.old_record);if(!old||r.new_assertion==KnowledgeAssertionValue::Unknown||r.new_assertion==old->assertion||!r.source_kind.IsValid()||!EnumInRange(r.epistemic_state,KnowledgeEpistemicState::Outdated)||!EnumInRange(r.confidence,KnowledgeConfidence::Certain))
        return foundation::Result<KnowledgeRecordId>::Failure(Error("gameplay.knowledge.invalid_contradiction","invalid contradiction request"));
    KnowledgeRecord* existing=nullptr;auto oit=knowledge_by_owner_.find(old->owner);if(oit!=knowledge_by_owner_.end())for(auto id:oit->second){auto* k=FindMutableKnowledge(id);if(k&&k->id!=old->id&&k->topic==old->topic&&k->type==old->type&&k->assertion==r.new_assertion){existing=k;break;}}
    KnowledgeRecordId new_id{};if(!existing){new_id=KnowledgeRecordId{knowledge_ids_.Next()};if(!new_id.IsValid())return foundation::Result<KnowledgeRecordId>::Failure(Error("gameplay.knowledge.id_exhausted","knowledge id generator exhausted"));}
    if(old->transmission_depth==std::numeric_limits<std::uint32_t>::max())return foundation::Result<KnowledgeRecordId>::Failure(Error("gameplay.knowledge.provenance_depth_exhausted","knowledge provenance depth exhausted"));
    const auto old_copy=*old;Bump();old->epistemic_state=KnowledgeEpistemicState::Contradicted;old->revision=revision_;
    if(existing)
    {
        existing->confidence=std::max(existing->confidence,r.confidence);existing->epistemic_state=Stronger(existing->epistemic_state,r.epistemic_state);existing->source_kind=r.source_kind;existing->source=r.source_object;
        existing->derived_from=old_copy.id;existing->original_source_kind=r.source_kind;existing->original_source=r.source_object;existing->transmission_depth=old_copy.transmission_depth+1;existing->last_confirmed_at=r.context.time;existing->last_decay_at=r.context.time;existing->payload=std::move(r.payload);existing->revision=revision_;new_id=existing->id;
    }
    else
    {
        KnowledgeRecord n;n.id=new_id;n.owner=old_copy.owner;n.type=old_copy.type;n.topic=old_copy.topic;n.assertion=r.new_assertion;n.epistemic_state=r.epistemic_state;n.confidence=r.confidence;n.subject=old_copy.subject;
        n.source_kind=r.source_kind;n.source=r.source_object;n.derived_from=old_copy.id;n.original_source_kind=r.source_kind;n.original_source=r.source_object;n.transmission_depth=old_copy.transmission_depth+1;
        n.learned_at=r.context.time;n.last_confirmed_at=r.context.time;n.last_decay_at=r.context.time;n.decay_rule=old_copy.decay_rule;n.persistence=old_copy.persistence;n.payload=std::move(r.payload);n.revision=revision_;knowledge_.emplace(new_id,n);IndexKnowledge(n);
    }
    ++diagnostics_.contradictions;Record({0,KnowledgeChangeKind::Contradicted,old_copy.owner,r.source_object,old_copy.id,{},old_copy.topic.id,r.context,revision_});
    Record({0,existing?KnowledgeChangeKind::Updated:KnowledgeChangeKind::Learned,old_copy.owner,r.source_object,new_id,{},old_copy.topic.id,r.context,revision_});return foundation::Result<KnowledgeRecordId>::Success(new_id);
}

foundation::Result<void> KnowledgeService::Decay(GameplayTimePoint now)
{
    std::vector<MemoryRecordId> forget_mem;for(const auto& [id,m]:memories_)if(m.persistence==MemoryPersistencePolicy::Timed&&m.decay_after.ticks>0&&m.importance!=MemoryImportance::Critical&&now>=SaturatingAdd(m.time,m.decay_after))forget_mem.push_back(id);
    std::sort(forget_mem.begin(),forget_mem.end());for(auto id:forget_mem){auto it=memories_.find(id);if(it==memories_.end())continue;auto copy=it->second;UnindexMemory(copy);memories_.erase(it);Bump();Record({0,KnowledgeChangeKind::MemoryForgotten,copy.owner,copy.subject,{},id,{},GameplayContext{.time=now},revision_});}

    std::vector<KnowledgeRecordId> ids;ids.reserve(knowledge_.size());for(const auto& [id,k]:knowledge_){(void)k;ids.push_back(id);}std::sort(ids.begin(),ids.end());std::vector<KnowledgeRecordId> forget_knowledge;
    for(auto id:ids)
    {
        auto* k=FindMutableKnowledge(id);if(!k||!k->decay_rule.IsValid())continue;auto rit=decay_rules_.find(k->decay_rule);if(rit==decay_rules_.end()||rit->second.interval.ticks<=0)continue;
        const auto elapsed=CheckedDifference(now,k->last_decay_at);if(!elapsed||elapsed->ticks<rit->second.interval.ticks)continue;const auto steps=elapsed->ticks/rit->second.interval.ticks;if(steps<=0)continue;
        const auto current=static_cast<int>(k->confidence);const auto max_dec=static_cast<std::uint64_t>(current);const auto raw_dec=static_cast<std::uint64_t>(steps)>max_dec/rit->second.confidence_steps?max_dec:static_cast<std::uint64_t>(steps)*rit->second.confidence_steps;
        const auto next_conf=static_cast<KnowledgeConfidence>(std::max(0,current-static_cast<int>(raw_dec)));const auto advanced=GameplayDuration{rit->second.interval.ticks*steps};
        k->last_decay_at=SaturatingAdd(k->last_decay_at,advanced);k->confidence=next_conf;
        if(rit->second.outdated_at_or_below&&static_cast<int>(next_conf)<=static_cast<int>(*rit->second.outdated_at_or_below))k->epistemic_state=KnowledgeEpistemicState::Outdated;
        if(rit->second.forget_at_or_below&&static_cast<int>(next_conf)<=static_cast<int>(*rit->second.forget_at_or_below)){forget_knowledge.push_back(id);continue;}
        Bump();k->revision=revision_;Record({0,KnowledgeChangeKind::KnowledgeDecayed,k->owner,k->subject,k->id,{},k->topic.id,GameplayContext{.time=now},revision_});
    }
    for(auto id:forget_knowledge){auto it=knowledge_.find(id);if(it==knowledge_.end())continue;auto copy=it->second;UnindexKnowledge(copy);knowledge_.erase(it);Bump();++diagnostics_.forgotten_records;Record({0,KnowledgeChangeKind::Forgotten,copy.owner,copy.subject,id,{},copy.topic.id,GameplayContext{.time=now},revision_});}
    return foundation::Result<void>::Success();
}

const KnowledgeProfile* KnowledgeService::FindProfile(GameplayObjectRef s)const noexcept{auto it=profiles_.find(s);return it==profiles_.end()?nullptr:&it->second;}
const KnowledgeRecord* KnowledgeService::FindKnowledge(KnowledgeRecordId id)const noexcept{auto it=knowledge_.find(id);return it==knowledge_.end()?nullptr:&it->second;}
const MemoryRecord* KnowledgeService::FindMemory(MemoryRecordId id)const noexcept{auto it=memories_.find(id);return it==memories_.end()?nullptr:&it->second;}
const MemoryDecayRule* KnowledgeService::FindDecayRule(MemoryDecayRuleId id)const noexcept{auto it=decay_rules_.find(id);return it==decay_rules_.end()?nullptr:&it->second;}

std::vector<KnowledgeRecord> KnowledgeService::FindKnowledgeByOwner(GameplayObjectRef owner)const
{
    std::vector<KnowledgeRecord> out;auto it=knowledge_by_owner_.find(owner);if(it==knowledge_by_owner_.end())return out;out.reserve(it->second.size());for(auto id:it->second){auto k=knowledge_.find(id);if(k!=knowledge_.end())out.push_back(k->second);}return out;
}
std::vector<KnowledgeRecord> KnowledgeService::FindKnowledgeByTopic(GameplayObjectRef owner,KnowledgeTopicId topic)const
{
    std::vector<KnowledgeRecord> out;auto oit=knowledge_by_topic_.find(owner);if(oit==knowledge_by_topic_.end())return out;auto tit=oit->second.find(topic);if(tit==oit->second.end())return out;out.reserve(tit->second.size());for(auto id:tit->second){auto k=knowledge_.find(id);if(k!=knowledge_.end())out.push_back(k->second);}return out;
}
std::vector<KnowledgeRecord> KnowledgeService::FindKnowledgeAboutSubject(GameplayObjectRef owner,GameplayObjectRef subject)const
{
    std::vector<KnowledgeRecord> out;auto oit=knowledge_by_subject_.find(owner);if(oit==knowledge_by_subject_.end())return out;auto sit=oit->second.find(subject);if(sit==oit->second.end())return out;out.reserve(sit->second.size());for(auto id:sit->second){auto k=knowledge_.find(id);if(k!=knowledge_.end())out.push_back(k->second);}return out;
}
std::optional<KnowledgeRecord> KnowledgeService::GetLastKnownPosition(GameplayObjectRef owner,GameplayObjectRef subject)const
{
    const auto topic=KnowledgeTopicId::FromString("framework.knowledge.last_known_position");std::optional<KnowledgeRecord> best;auto records=FindKnowledgeAboutSubject(owner,subject);for(const auto& k:records)if(k.topic.id==topic&&(!best||k.last_confirmed_at>best->last_confirmed_at||(k.last_confirmed_at==best->last_confirmed_at&&k.id<best->id)))best=k;return best;
}
std::vector<MemoryRecord> KnowledgeService::FindMemoriesByOwner(GameplayObjectRef owner)const
{
    std::vector<MemoryRecord> out;auto it=memories_by_owner_.find(owner);if(it==memories_by_owner_.end())return out;out.reserve(it->second.size());for(auto id:it->second){auto m=memories_.find(id);if(m!=memories_.end())out.push_back(m->second);}return out;
}
std::vector<MemoryRecord> KnowledgeService::FindImportantMemories(GameplayObjectRef owner,MemoryImportance min)const
{
    auto all=FindMemoriesByOwner(owner);all.erase(std::remove_if(all.begin(),all.end(),[min](const auto& m){return static_cast<int>(m.importance)<static_cast<int>(min);}),all.end());return all;
}

KnowledgeChangeBatch KnowledgeService::ReadChangesSince(std::uint64_t seq)const
{
    KnowledgeChangeBatch b;b.latest_sequence=next_change_sequence_==0?std::numeric_limits<std::uint64_t>::max():next_change_sequence_-1;b.oldest_available_sequence=changes_.empty()?b.latest_sequence+static_cast<std::uint64_t>(b.latest_sequence!=std::numeric_limits<std::uint64_t>::max()):changes_.front().sequence;
    if(!changes_.empty()&&seq+static_cast<std::uint64_t>(seq!=std::numeric_limits<std::uint64_t>::max())<changes_.front().sequence){b.snapshot_required=true;return b;}
    for(const auto& c:changes_)
    {
        if(c.sequence>seq)
            b.changes.push_back(c);
    }
    return b;
}

KnowledgeSnapshot KnowledgeService::CaptureSnapshot()const
{
    KnowledgeSnapshot s;for(const auto& [o,p]:profiles_){(void)o;s.profiles.push_back(p);}for(const auto& [id,k]:knowledge_){(void)id;if(k.persistence==MemoryPersistencePolicy::Persistent||k.persistence==MemoryPersistencePolicy::Timed)s.knowledge.push_back(k);}for(const auto& [id,m]:memories_){(void)id;if(m.persistence==MemoryPersistencePolicy::Persistent||m.persistence==MemoryPersistencePolicy::Timed)s.memories.push_back(m);}
    std::sort(s.profiles.begin(),s.profiles.end(),[](const auto&a,const auto&b){return a.subject<b.subject;});std::sort(s.knowledge.begin(),s.knowledge.end(),[](const auto&a,const auto&b){return a.id<b.id;});std::sort(s.memories.begin(),s.memories.end(),[](const auto&a,const auto&b){return a.id<b.id;});s.knowledge_ids=knowledge_ids_.GetSnapshot();s.memory_ids=memory_ids_.GetSnapshot();s.revision=revision_;return s;
}

foundation::Result<void> KnowledgeService::RestoreSnapshot(KnowledgeSnapshot s)
{
    std::unordered_map<GameplayObjectRef,KnowledgeProfile,RefHash> new_profiles;std::unordered_map<KnowledgeRecordId,KnowledgeRecord,IdHash> new_knowledge;std::unordered_map<MemoryRecordId,MemoryRecord,IdHash> new_memories;
    new_profiles.reserve(s.profiles.size());new_knowledge.reserve(s.knowledge.size());new_memories.reserve(s.memories.size());std::uint64_t max_k=0,max_m=0;
    for(const auto& p:s.profiles)
    {
        if(!p.subject.IsValid()||!EnumInRange(p.retention,MemoryPersistencePolicy::Timed)||p.revision.value>s.revision.value||(p.default_decay_rule.IsValid()&&!decay_rules_.contains(p.default_decay_rule))||!new_profiles.emplace(p.subject,p).second)
            return foundation::Result<void>::Failure(Error("gameplay.knowledge.restore_invalid","invalid or duplicate profile"));
    }
    for(const auto& k:s.knowledge)
    {
        if(!k.id.IsValid()||!new_profiles.contains(k.owner)||!k.type.IsValid()||!k.topic.id.IsValid()||!k.source_kind.IsValid()||!EnumInRange(k.assertion,KnowledgeAssertionValue::Denied)||!EnumInRange(k.epistemic_state,KnowledgeEpistemicState::Outdated)||!EnumInRange(k.confidence,KnowledgeConfidence::Certain)||!EnumInRange(k.persistence,MemoryPersistencePolicy::Timed)||k.revision.value>s.revision.value||k.last_confirmed_at<k.learned_at||k.last_decay_at<k.learned_at||(k.decay_rule.IsValid()&&!decay_rules_.contains(k.decay_rule))||
           (k.transmission_depth==0&&k.derived_from.IsValid())||(k.transmission_depth>0&&(!k.derived_from.IsValid()||!k.original_source_kind.IsValid()))||!new_knowledge.emplace(k.id,k).second)
            return foundation::Result<void>::Failure(Error("gameplay.knowledge.restore_invalid","invalid or duplicate knowledge record"));
        max_k=std::max(max_k,k.id.value.Low());
    }
    for(const auto& m:s.memories)
    {
        if(!m.id.IsValid()||!new_profiles.contains(m.owner)||!m.type.IsValid()||!EnumInRange(m.importance,MemoryImportance::Critical)||!EnumInRange(m.persistence,MemoryPersistencePolicy::Timed)||m.decay_after.ticks<0||m.revision.value>s.revision.value||!new_memories.emplace(m.id,m).second)
            return foundation::Result<void>::Failure(Error("gameplay.knowledge.restore_invalid","invalid or duplicate memory record"));
        max_m=std::max(max_m,m.id.value.Low());
    }
    if(!ValidateGenerator(s.knowledge_ids,knowledge_ids_.GetSnapshot().scope,max_k)||!ValidateGenerator(s.memory_ids,memory_ids_.GetSnapshot().scope,max_m))
        return foundation::Result<void>::Failure(Error("gameplay.knowledge.restore_invalid","invalid id generator snapshot"));
    profiles_=std::move(new_profiles);knowledge_=std::move(new_knowledge);memories_=std::move(new_memories);knowledge_ids_.Restore(s.knowledge_ids);memory_ids_.Restore(s.memory_ids);revision_=s.revision;changes_.clear();next_change_sequence_=1;definitions_frozen_=true;RebuildIndexes();return foundation::Result<void>::Success();
}

KnowledgeDiagnostics KnowledgeService::GetDiagnostics()const noexcept{auto d=diagnostics_;d.profiles=profiles_.size();d.knowledge_records=knowledge_.size();d.memory_records=memories_.size();return d;}
void KnowledgeService::Record(KnowledgeChange c)
{
    if(next_change_sequence_==0)
        return;
    c.sequence=next_change_sequence_;
    if(next_change_sequence_==std::numeric_limits<std::uint64_t>::max())
        next_change_sequence_=0;
    else
        ++next_change_sequence_;
    changes_.push_back(std::move(c));
    while(changes_.size()>change_capacity_)
        changes_.pop_front();
}
void KnowledgeService::IndexKnowledge(const KnowledgeRecord& k){InsertSorted(knowledge_by_owner_[k.owner],k.id);InsertSorted(knowledge_by_topic_[k.owner][k.topic.id],k.id);if(k.subject.IsValid())InsertSorted(knowledge_by_subject_[k.owner][k.subject],k.id);}
void KnowledgeService::UnindexKnowledge(const KnowledgeRecord& k)
{
    auto oi=knowledge_by_owner_.find(k.owner);if(oi!=knowledge_by_owner_.end()){EraseSorted(oi->second,k.id);if(oi->second.empty())knowledge_by_owner_.erase(oi);}auto ti=knowledge_by_topic_.find(k.owner);if(ti!=knowledge_by_topic_.end()){auto q=ti->second.find(k.topic.id);if(q!=ti->second.end()){EraseSorted(q->second,k.id);if(q->second.empty())ti->second.erase(q);}if(ti->second.empty())knowledge_by_topic_.erase(ti);}if(k.subject.IsValid()){auto si=knowledge_by_subject_.find(k.owner);if(si!=knowledge_by_subject_.end()){auto q=si->second.find(k.subject);if(q!=si->second.end()){EraseSorted(q->second,k.id);if(q->second.empty())si->second.erase(q);}if(si->second.empty())knowledge_by_subject_.erase(si);}}
}
void KnowledgeService::IndexMemory(const MemoryRecord& m){InsertSorted(memories_by_owner_[m.owner],m.id);}
void KnowledgeService::UnindexMemory(const MemoryRecord& m){auto it=memories_by_owner_.find(m.owner);if(it!=memories_by_owner_.end()){EraseSorted(it->second,m.id);if(it->second.empty())memories_by_owner_.erase(it);}}
void KnowledgeService::RebuildIndexes(){knowledge_by_owner_.clear();knowledge_by_topic_.clear();knowledge_by_subject_.clear();memories_by_owner_.clear();std::vector<KnowledgeRecordId> k;for(const auto&[id,r]:knowledge_){(void)r;k.push_back(id);}std::sort(k.begin(),k.end());for(auto id:k)IndexKnowledge(knowledge_.at(id));std::vector<MemoryRecordId> m;for(const auto&[id,r]:memories_){(void)r;m.push_back(id);}std::sort(m.begin(),m.end());for(auto id:m)IndexMemory(memories_.at(id));}
} // namespace epidemic::gameplay::knowledge
