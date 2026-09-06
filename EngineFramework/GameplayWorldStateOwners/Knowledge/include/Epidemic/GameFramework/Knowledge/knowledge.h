#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::knowledge
{
struct KnowledgeRecordId { GameplayObjectId value{}; static constexpr KnowledgeRecordId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} static constexpr KnowledgeRecordId FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};} [[nodiscard]] constexpr bool IsValid()const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const KnowledgeRecordId&)const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const KnowledgeRecordId&)const noexcept=default;};
struct MemoryRecordId { GameplayObjectId value{}; static constexpr MemoryRecordId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} static constexpr MemoryRecordId FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};} [[nodiscard]] constexpr bool IsValid()const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const MemoryRecordId&)const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const MemoryRecordId&)const noexcept=default;};
struct BeliefTypeId { TypeId value{}; static constexpr BeliefTypeId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid()const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const BeliefTypeId&)const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const BeliefTypeId&)const noexcept=default;};
struct KnowledgeSourceId { TypeId value{}; static constexpr KnowledgeSourceId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid()const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const KnowledgeSourceId&)const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const KnowledgeSourceId&)const noexcept=default;};
struct KnowledgeTopicId { TypeId value{}; static constexpr KnowledgeTopicId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid()const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const KnowledgeTopicId&)const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const KnowledgeTopicId&)const noexcept=default;};
struct MemoryTypeId { TypeId value{}; static constexpr MemoryTypeId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid()const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const MemoryTypeId&)const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const MemoryTypeId&)const noexcept=default;};
struct MemoryDecayRuleId { TypeId value{}; static constexpr MemoryDecayRuleId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid()const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const MemoryDecayRuleId&)const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const MemoryDecayRuleId&)const noexcept=default;};
struct IdHash { template<class T> [[nodiscard]] std::size_t operator()(const T& id)const noexcept{return std::hash<decltype(id.value)>{}(id.value);} };
struct RefHash { [[nodiscard]] std::size_t operator()(GameplayObjectRef r)const noexcept{return std::hash<GameplayObjectRef>{}(r);} };

enum class KnowledgeAssertionValue { Unknown, Affirmed, Denied };
enum class KnowledgeEpistemicState { Known, Suspected, Rumor, Contradicted, Outdated };
enum class KnowledgeConfidence { None, Low, Medium, High, Certain };
enum class MemoryImportance { Low, Normal, High, Critical };
enum class MemoryPersistencePolicy { Transient, Session, Persistent, Timed };
enum class KnowledgeChangeKind { Learned, Updated, Forgotten, Shared, Contradicted, MemoryCreated, KnowledgeDecayed, MemoryDecayed, MemoryCompacted, MemoryForgotten, ProfileRemoved };
enum class KnowledgeShareMode { Tell, Report, Rumor, Broadcast, GroupSync };

struct KnowledgeTopic
{
    KnowledgeTopicId id{}; GameplayTagSet tags; GameplayObjectRef primary_subject{}; GameplayObjectRef scope{}; std::vector<std::byte> payload;
    [[nodiscard]] bool operator==(const KnowledgeTopic& o)const noexcept{return id==o.id&&primary_subject==o.primary_subject&&scope==o.scope;}
};
struct MemoryDecayRule
{
    MemoryDecayRuleId id{}; std::string canonical_name; GameplayDuration interval{}; std::uint8_t confidence_steps=1;
    std::optional<KnowledgeConfidence> outdated_at_or_below; std::optional<KnowledgeConfidence> forget_at_or_below;
};
struct KnowledgeProfile
{
    GameplayObjectRef subject{}; GameplayTagSet tags; MemoryPersistencePolicy retention=MemoryPersistencePolicy::Persistent;
    GameplayTagSet sharing_tags; MemoryDecayRuleId default_decay_rule{}; Revision revision{};
};
struct KnowledgeRecord
{
    KnowledgeRecordId id{}; GameplayObjectRef owner{}; BeliefTypeId type{}; KnowledgeTopic topic{};
    KnowledgeAssertionValue assertion=KnowledgeAssertionValue::Unknown; KnowledgeEpistemicState epistemic_state=KnowledgeEpistemicState::Suspected;
    KnowledgeConfidence confidence=KnowledgeConfidence::None; GameplayObjectRef subject{}; KnowledgeSourceId source_kind{}; GameplayObjectRef source{};
    KnowledgeRecordId derived_from{}; KnowledgeSourceId original_source_kind{}; GameplayObjectRef original_source{}; std::uint32_t transmission_depth=0;
    GameplayTimePoint learned_at{}; GameplayTimePoint last_confirmed_at{}; GameplayTimePoint last_decay_at{}; MemoryDecayRuleId decay_rule{};
    MemoryPersistencePolicy persistence=MemoryPersistencePolicy::Persistent; std::vector<std::byte> payload; Revision revision{};
};
struct MemoryRecord
{
    MemoryRecordId id{}; GameplayObjectRef owner{}; MemoryTypeId type{}; GameplayTimePoint time{}; GameplayObjectRef subject{}; GameplayObjectRef area{};
    MemoryImportance importance=MemoryImportance::Normal; TagId emotion{}; MemoryPersistencePolicy persistence=MemoryPersistencePolicy::Session;
    GameplayDuration decay_after{}; std::vector<std::byte> payload; Revision revision{};
};
struct LearnKnowledgeRequest
{
    GameplayObjectRef learner{}; BeliefTypeId type{}; KnowledgeTopic topic{}; KnowledgeSourceId source_kind{}; GameplayObjectRef source_object{};
    KnowledgeAssertionValue assertion=KnowledgeAssertionValue::Affirmed; KnowledgeEpistemicState epistemic_state=KnowledgeEpistemicState::Suspected;
    KnowledgeConfidence confidence=KnowledgeConfidence::Low; std::optional<MemoryPersistencePolicy> persistence; std::optional<MemoryDecayRuleId> decay_rule;
    std::vector<std::byte> payload; GameplayContext context{};
};
struct CreateMemoryRequest
{
    GameplayObjectRef owner{}; MemoryTypeId type{}; GameplayObjectRef subject{}; GameplayObjectRef area{}; MemoryImportance importance=MemoryImportance::Normal;
    std::optional<MemoryPersistencePolicy> persistence; GameplayDuration decay_after{}; std::vector<std::byte> payload; GameplayContext context{};
};
struct ShareKnowledgeRequest { GameplayObjectRef speaker{}; GameplayObjectRef listener{}; KnowledgeRecordId record{}; KnowledgeShareMode mode=KnowledgeShareMode::Tell; GameplayContext context{}; };
struct ContradictKnowledgeRequest
{
    KnowledgeRecordId old_record{}; KnowledgeAssertionValue new_assertion=KnowledgeAssertionValue::Denied;
    KnowledgeEpistemicState epistemic_state=KnowledgeEpistemicState::Known; KnowledgeConfidence confidence=KnowledgeConfidence::Medium;
    KnowledgeSourceId source_kind{}; GameplayObjectRef source_object{}; std::vector<std::byte> payload; GameplayContext context{};
};
struct KnowledgeChange
{
    std::uint64_t sequence=0; KnowledgeChangeKind kind=KnowledgeChangeKind::Learned; GameplayObjectRef owner{}; GameplayObjectRef other{};
    KnowledgeRecordId knowledge{}; MemoryRecordId memory{}; KnowledgeTopicId topic{}; GameplayContext context{}; Revision revision{};
};
struct KnowledgeChangeBatch
{
    std::vector<KnowledgeChange> changes; bool snapshot_required=false; std::uint64_t oldest_available_sequence=0; std::uint64_t latest_sequence=0;
};
struct KnowledgeSnapshot
{
    std::vector<KnowledgeProfile> profiles; std::vector<KnowledgeRecord> knowledge; std::vector<MemoryRecord> memories;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot knowledge_ids{}; MonotonicIdGenerator<GameplayObjectId>::Snapshot memory_ids{}; Revision revision{};
};
struct KnowledgeDiagnostics
{
    std::uint64_t profiles=0,knowledge_records=0,memory_records=0,learn_ops=0,shared_records=0,forgotten_records=0,compacted_memories=0,contradictions=0;
};

class KnowledgeService
{
public:
    explicit KnowledgeService(std::size_t change_capacity=4096);
    [[nodiscard]] static constexpr GameplayDomainId Domain()noexcept{return GameplayDomainId::FromString("framework.knowledge");}
    [[nodiscard]] foundation::Result<void> RegisterDecayRule(MemoryDecayRule rule);
    [[nodiscard]] foundation::Result<void> FreezeDefinitions();
    [[nodiscard]] bool DefinitionsFrozen()const noexcept{return definitions_frozen_;}
    [[nodiscard]] foundation::Result<void> CreateProfile(KnowledgeProfile profile);
    [[nodiscard]] foundation::Result<void> RemoveProfile(GameplayObjectRef subject,GameplayContext context={});
    [[nodiscard]] foundation::Result<KnowledgeRecordId> Learn(LearnKnowledgeRequest request);
    [[nodiscard]] foundation::Result<MemoryRecordId> CreateMemory(CreateMemoryRequest request);
    [[nodiscard]] foundation::Result<MemoryRecordId> CompactMemories(GameplayObjectRef owner,std::span<const MemoryRecordId> source_ids,CreateMemoryRequest summary,GameplayContext context={});
    [[nodiscard]] foundation::Result<KnowledgeRecordId> Share(ShareKnowledgeRequest request);
    [[nodiscard]] foundation::Result<void> Forget(KnowledgeRecordId id,GameplayContext context={});
    [[nodiscard]] foundation::Result<KnowledgeRecordId> Contradict(ContradictKnowledgeRequest request);
    [[nodiscard]] foundation::Result<void> Decay(GameplayTimePoint now);
    [[nodiscard]] const KnowledgeProfile* FindProfile(GameplayObjectRef subject)const noexcept;
    [[nodiscard]] const KnowledgeRecord* FindKnowledge(KnowledgeRecordId id)const noexcept;
    [[nodiscard]] const MemoryRecord* FindMemory(MemoryRecordId id)const noexcept;
    [[nodiscard]] const MemoryDecayRule* FindDecayRule(MemoryDecayRuleId id)const noexcept;
    [[nodiscard]] std::vector<KnowledgeRecord> FindKnowledgeByOwner(GameplayObjectRef owner)const;
    [[nodiscard]] std::vector<KnowledgeRecord> FindKnowledgeByTopic(GameplayObjectRef owner,KnowledgeTopicId topic)const;
    [[nodiscard]] std::vector<KnowledgeRecord> FindKnowledgeAboutSubject(GameplayObjectRef owner,GameplayObjectRef subject)const;
    [[nodiscard]] std::optional<KnowledgeRecord> GetLastKnownPosition(GameplayObjectRef owner,GameplayObjectRef subject)const;
    [[nodiscard]] std::vector<MemoryRecord> FindMemoriesByOwner(GameplayObjectRef owner)const;
    [[nodiscard]] std::vector<MemoryRecord> FindImportantMemories(GameplayObjectRef owner,MemoryImportance min_importance)const;
    [[nodiscard]] KnowledgeChangeBatch ReadChangesSince(std::uint64_t sequence)const;
    [[nodiscard]] std::vector<KnowledgeChange> ChangesSince(std::uint64_t sequence)const{return ReadChangesSince(sequence).changes;}
    [[nodiscard]] KnowledgeSnapshot CaptureSnapshot()const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(KnowledgeSnapshot snapshot);
    [[nodiscard]] KnowledgeDiagnostics GetDiagnostics()const noexcept;
    [[nodiscard]] Revision CurrentRevision()const noexcept{return revision_;}
private:
    void Bump()noexcept{revision_.value++;}
    void Record(KnowledgeChange change);
    [[nodiscard]] KnowledgeConfidence Degrade(KnowledgeConfidence c,KnowledgeShareMode mode)const noexcept;
    [[nodiscard]] foundation::Result<KnowledgeRecordId> LearnInternal(LearnKnowledgeRequest request,KnowledgeRecordId derived_from,KnowledgeSourceId original_source_kind,GameplayObjectRef original_source,std::uint32_t transmission_depth);
    [[nodiscard]] KnowledgeRecord* FindMutableKnowledge(KnowledgeRecordId id)noexcept;
    void IndexKnowledge(const KnowledgeRecord& record); void UnindexKnowledge(const KnowledgeRecord& record);
    void IndexMemory(const MemoryRecord& record); void UnindexMemory(const MemoryRecord& record);
    void RebuildIndexes();
    [[nodiscard]] bool CanShareTopic(const KnowledgeProfile& profile,const KnowledgeTopic& topic)const noexcept;

    Revision revision_{};
    MonotonicIdGenerator<GameplayObjectId> knowledge_ids_{0x2100}; MonotonicIdGenerator<GameplayObjectId> memory_ids_{0x2101};
    std::unordered_map<MemoryDecayRuleId,MemoryDecayRule,IdHash> decay_rules_; bool definitions_frozen_=false;
    std::unordered_map<GameplayObjectRef,KnowledgeProfile,RefHash> profiles_;
    std::unordered_map<KnowledgeRecordId,KnowledgeRecord,IdHash> knowledge_; std::unordered_map<MemoryRecordId,MemoryRecord,IdHash> memories_;
    std::unordered_map<GameplayObjectRef,std::vector<KnowledgeRecordId>,RefHash> knowledge_by_owner_;
    std::unordered_map<GameplayObjectRef,std::unordered_map<KnowledgeTopicId,std::vector<KnowledgeRecordId>,IdHash>,RefHash> knowledge_by_topic_;
    std::unordered_map<GameplayObjectRef,std::unordered_map<GameplayObjectRef,std::vector<KnowledgeRecordId>,RefHash>,RefHash> knowledge_by_subject_;
    std::unordered_map<GameplayObjectRef,std::vector<MemoryRecordId>,RefHash> memories_by_owner_;
    std::deque<KnowledgeChange> changes_; std::size_t change_capacity_=4096; std::uint64_t next_change_sequence_=1; KnowledgeDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::knowledge
