#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::knowledge
{
struct KnowledgeRecordId{GameplayObjectId value{}; static constexpr KnowledgeRecordId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} static constexpr KnowledgeRecordId FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const KnowledgeRecordId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const KnowledgeRecordId&) const noexcept=default;};
struct MemoryRecordId{GameplayObjectId value{}; static constexpr MemoryRecordId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} static constexpr MemoryRecordId FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const MemoryRecordId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const MemoryRecordId&) const noexcept=default;};
struct BeliefTypeId{TypeId value{}; static constexpr BeliefTypeId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const BeliefTypeId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const BeliefTypeId&) const noexcept=default;};
struct KnowledgeSourceId{TypeId value{}; static constexpr KnowledgeSourceId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const KnowledgeSourceId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const KnowledgeSourceId&) const noexcept=default;};
struct KnowledgeTopicId{TypeId value{}; static constexpr KnowledgeTopicId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const KnowledgeTopicId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const KnowledgeTopicId&) const noexcept=default;};
struct MemoryTypeId{TypeId value{}; static constexpr MemoryTypeId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const MemoryTypeId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const MemoryTypeId&) const noexcept=default;};
struct MemoryDecayRuleId{TypeId value{}; static constexpr MemoryDecayRuleId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const MemoryDecayRuleId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const MemoryDecayRuleId&) const noexcept=default;};
struct IdHash{template<class T> [[nodiscard]] std::size_t operator()(const T& id) const noexcept{return std::hash<decltype(id.value)>{}(id.value);}};
struct RefHash{[[nodiscard]] std::size_t operator()(GameplayObjectRef r) const noexcept{return std::hash<GameplayObjectRef>{}(r);}};

enum class KnowledgeTruthState{KnownTrue,KnownFalse,Suspected,Rumor,Unknown,Contradicted,Outdated};
enum class KnowledgeConfidence{None,Low,Medium,High,Certain};
enum class MemoryImportance{Low,Normal,High,Critical};
enum class MemoryPersistencePolicy{Transient,Session,Persistent,Timed};
enum class KnowledgeChangeKind{Learned,Updated,Forgotten,Shared,Contradicted,MemoryCreated,MemoryDecayed,MemoryCompacted,MemoryForgotten};
enum class KnowledgeShareMode{Tell,Report,Rumor,Broadcast,GroupSync};

struct KnowledgeTopic{KnowledgeTopicId id{};GameplayTagSet tags;GameplayObjectRef primary_subject{};GameplayObjectRef scope{};std::vector<std::byte> payload; [[nodiscard]] bool operator==(const KnowledgeTopic& o) const noexcept{return id==o.id&&primary_subject==o.primary_subject&&scope==o.scope;}};
struct KnowledgeProfile{GameplayObjectRef subject{};GameplayTagSet tags;MemoryPersistencePolicy retention=MemoryPersistencePolicy::Persistent;GameplayTagSet sharing_tags;Revision revision{};};
struct KnowledgeRecord{KnowledgeRecordId id{};GameplayObjectRef owner{};BeliefTypeId type{};KnowledgeTopic topic{};KnowledgeTruthState truth_state=KnowledgeTruthState::Unknown;KnowledgeConfidence confidence=KnowledgeConfidence::None;GameplayObjectRef subject{};GameplayObjectRef source{};GameplayTimePoint learned_at{};GameplayTimePoint last_confirmed_at{};MemoryPersistencePolicy persistence=MemoryPersistencePolicy::Persistent;std::vector<std::byte> payload;Revision revision{};};
struct MemoryRecord{MemoryRecordId id{};GameplayObjectRef owner{};MemoryTypeId type{};GameplayTimePoint time{};GameplayObjectRef subject{};GameplayObjectRef area{};MemoryImportance importance=MemoryImportance::Normal;TagId emotion{};MemoryPersistencePolicy persistence=MemoryPersistencePolicy::Session;GameplayDuration decay_after{};std::vector<std::byte> payload;Revision revision{};};
struct LearnKnowledgeRequest{GameplayObjectRef learner{};BeliefTypeId type{};KnowledgeTopic topic{};KnowledgeSourceId source_kind{};GameplayObjectRef source_object{};KnowledgeConfidence confidence=KnowledgeConfidence::Low;std::vector<std::byte> payload;GameplayContext context{};};
struct CreateMemoryRequest{GameplayObjectRef owner{};MemoryTypeId type{};GameplayObjectRef subject{};GameplayObjectRef area{};MemoryImportance importance=MemoryImportance::Normal;MemoryPersistencePolicy persistence=MemoryPersistencePolicy::Session;GameplayDuration decay_after{};std::vector<std::byte> payload;GameplayContext context{};};
struct ShareKnowledgeRequest{GameplayObjectRef speaker{};GameplayObjectRef listener{};KnowledgeRecordId record{};KnowledgeShareMode mode=KnowledgeShareMode::Tell;GameplayContext context{};};
struct KnowledgeChange{std::uint64_t sequence=0;KnowledgeChangeKind kind=KnowledgeChangeKind::Learned;GameplayObjectRef owner{};GameplayObjectRef other{};KnowledgeRecordId knowledge{};MemoryRecordId memory{};KnowledgeTopicId topic{};GameplayContext context{};Revision revision{};};
struct KnowledgeSnapshot{std::vector<KnowledgeProfile> profiles;std::vector<KnowledgeRecord> knowledge;std::vector<MemoryRecord> memories;MonotonicIdGenerator<GameplayObjectId>::Snapshot knowledge_ids{};MonotonicIdGenerator<GameplayObjectId>::Snapshot memory_ids{};Revision revision{};};
struct KnowledgeDiagnostics{std::uint64_t profiles=0,knowledge_records=0,memory_records=0,learn_ops=0,shared_records=0,forgotten_records=0,compacted_memories=0,contradictions=0;};

class KnowledgeService
{
public:
    KnowledgeService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept{return GameplayDomainId::FromString("framework.knowledge");}
    [[nodiscard]] foundation::Result<void> CreateProfile(KnowledgeProfile profile);
    [[nodiscard]] foundation::Result<KnowledgeRecordId> Learn(LearnKnowledgeRequest request);
    [[nodiscard]] foundation::Result<MemoryRecordId> CreateMemory(CreateMemoryRequest request);
    [[nodiscard]] foundation::Result<KnowledgeRecordId> Share(ShareKnowledgeRequest request);
    [[nodiscard]] foundation::Result<void> Forget(KnowledgeRecordId id,GameplayContext context={});
    [[nodiscard]] foundation::Result<void> Contradict(KnowledgeRecordId old_record,KnowledgeRecord replacement,GameplayContext context={});
    [[nodiscard]] foundation::Result<void> Decay(GameplayTimePoint now);
    [[nodiscard]] const KnowledgeProfile* FindProfile(GameplayObjectRef subject) const noexcept;
    [[nodiscard]] const KnowledgeRecord* FindKnowledge(KnowledgeRecordId id) const noexcept;
    [[nodiscard]] const MemoryRecord* FindMemory(MemoryRecordId id) const noexcept;
    [[nodiscard]] std::vector<KnowledgeRecord> FindKnowledgeByOwner(GameplayObjectRef owner) const;
    [[nodiscard]] std::vector<KnowledgeRecord> FindKnowledgeByTopic(GameplayObjectRef owner,KnowledgeTopicId topic) const;
    [[nodiscard]] std::vector<KnowledgeRecord> FindKnowledgeAboutSubject(GameplayObjectRef owner,GameplayObjectRef subject) const;
    [[nodiscard]] std::optional<KnowledgeRecord> GetLastKnownPosition(GameplayObjectRef owner,GameplayObjectRef subject) const;
    [[nodiscard]] std::vector<MemoryRecord> FindMemoriesByOwner(GameplayObjectRef owner) const;
    [[nodiscard]] std::vector<MemoryRecord> FindImportantMemories(GameplayObjectRef owner,MemoryImportance min_importance) const;
    [[nodiscard]] std::vector<KnowledgeChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] KnowledgeSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(KnowledgeSnapshot snapshot);
    [[nodiscard]] KnowledgeDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept{return revision_;}
private:
    void Bump() noexcept{revision_.value++;}
    void Record(KnowledgeChange change);
    [[nodiscard]] KnowledgeConfidence Degrade(KnowledgeConfidence c,KnowledgeShareMode mode) const noexcept;
    [[nodiscard]] KnowledgeRecord* FindMutableKnowledge(KnowledgeRecordId id) noexcept;
    [[nodiscard]] MemoryRecord* FindMutableMemory(MemoryRecordId id) noexcept;
    Revision revision_{};
    MonotonicIdGenerator<GameplayObjectId> knowledge_ids_{0x2100};
    MonotonicIdGenerator<GameplayObjectId> memory_ids_{0x2101};
    std::unordered_map<GameplayObjectRef,KnowledgeProfile,RefHash> profiles_;
    std::unordered_map<KnowledgeRecordId,KnowledgeRecord,IdHash> knowledge_;
    std::unordered_map<MemoryRecordId,MemoryRecord,IdHash> memories_;
    std::vector<KnowledgeChange> changes_;
    std::uint64_t next_change_sequence_=1;
    KnowledgeDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::knowledge
