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

namespace epidemic::gameplay::society
{
struct MembershipId{GameplayObjectId value{}; static constexpr MembershipId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} static constexpr MembershipId FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const MembershipId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const MembershipId&) const noexcept=default;};
struct RelationshipId{GameplayObjectId value{}; static constexpr RelationshipId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} static constexpr RelationshipId FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const RelationshipId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const RelationshipId&) const noexcept=default;};
struct SocialRoleId{TypeId value{}; static constexpr SocialRoleId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const SocialRoleId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const SocialRoleId&) const noexcept=default;};
struct SocialGroupTypeId{TypeId value{}; static constexpr SocialGroupTypeId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const SocialGroupTypeId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const SocialGroupTypeId&) const noexcept=default;};
struct RelationshipTypeId{TypeId value{}; static constexpr RelationshipTypeId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const RelationshipTypeId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const RelationshipTypeId&) const noexcept=default;};
struct ReputationTrackId{TypeId value{}; static constexpr ReputationTrackId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const ReputationTrackId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const ReputationTrackId&) const noexcept=default;};
struct IdHash{template<class T> [[nodiscard]] std::size_t operator()(const T& id) const noexcept{return std::hash<decltype(id.value)>{}(id.value);}};
struct RefHash{[[nodiscard]] std::size_t operator()(GameplayObjectRef r) const noexcept{return std::hash<GameplayObjectRef>{}(r);}};
struct PairHash{[[nodiscard]] std::size_t operator()(const std::pair<GameplayObjectRef,GameplayObjectRef>& p) const noexcept{return std::hash<GameplayObjectRef>{}(p.first)^(std::hash<GameplayObjectRef>{}(p.second)+0x9E3779B97F4A7C15ull);}};

enum class MembershipState{Active,Suspended,Former,Banned,Applicant,Honorary};
enum class RelationshipState{Neutral,Friendly,Hostile,Allied,Feared,Trusted,Unknown};
enum class SocietyChangeKind{GroupCreated,MembershipAdded,MembershipRemoved,MembershipChanged,RelationshipChanged,ReputationChanged,SocialRoleAssigned,SocialRoleRemoved,FactionRelationChanged};

struct SocialGroupDefinition{GameplayObjectRef group{};GameplayTagSet tags;SocialGroupTypeId type{};std::vector<std::byte> payload;Revision revision{};};
struct MembershipRecord{MembershipId id{};GameplayObjectRef member{};GameplayObjectRef group{};SocialRoleId role{};std::int64_t rank=0;MembershipState state=MembershipState::Active;GameplayTimePoint joined_at{};Revision revision{};};
struct RelationshipRecord{RelationshipId id{};GameplayObjectRef subject{};GameplayObjectRef target{};RelationshipTypeId type{};std::int64_t value_micro=0;RelationshipState state=RelationshipState::Neutral;GameplayTimePoint updated_at{};Revision revision{};};
struct ReputationRecord{GameplayObjectRef subject{};GameplayObjectRef scope{};ReputationTrackId track{};std::int64_t value_micro=0;Revision revision{};};
struct SocialStandingSnapshot{GameplayObjectRef subject{};GameplayObjectRef scope{};GameplayTagSet standing_tags;std::vector<ReputationRecord> reputations;Revision revision{};};
struct SocialChangeRequest{GameplayObjectRef subject{};GameplayObjectRef target{};RelationshipTypeId type{};std::int64_t delta_micro=0;TypeId reason{};GameplayContext context{};};
struct AttitudeQuery{GameplayObjectRef observer{};GameplayObjectRef target{};GameplayContext context{};};
struct SocietyChange{std::uint64_t sequence=0;SocietyChangeKind kind=SocietyChangeKind::GroupCreated;GameplayObjectRef subject{};GameplayObjectRef target{};RelationshipTypeId relationship_type{};ReputationTrackId reputation_track{};MembershipId membership{};GameplayContext context{};Revision revision{};};
struct SocietySnapshot{std::vector<SocialGroupDefinition> groups;std::vector<MembershipRecord> memberships;std::vector<RelationshipRecord> relationships;std::vector<ReputationRecord> reputations;MonotonicIdGenerator<GameplayObjectId>::Snapshot membership_ids{};MonotonicIdGenerator<GameplayObjectId>::Snapshot relationship_ids{};Revision revision{};};
struct SocietyDiagnostics{std::uint64_t groups=0,memberships=0,relationships=0,reputation_records=0,relationship_changes=0,reputation_changes=0,attitude_queries=0,social_transactions=0;};

class SocietyService
{
public:
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept{return GameplayDomainId::FromString("framework.society");}
    [[nodiscard]] foundation::Result<void> RegisterGroup(SocialGroupDefinition group);
    [[nodiscard]] foundation::Result<MembershipId> AddMembership(MembershipRecord record);
    [[nodiscard]] foundation::Result<void> RemoveMembership(MembershipId id,GameplayContext context={});
    [[nodiscard]] foundation::Result<RelationshipId> SetRelationship(RelationshipRecord record);
    [[nodiscard]] foundation::Result<void> ApplySocialChange(SocialChangeRequest request);
    [[nodiscard]] foundation::Result<void> SetReputation(ReputationRecord record);
    [[nodiscard]] const SocialGroupDefinition* GetGroup(GameplayObjectRef group) const noexcept;
    [[nodiscard]] const MembershipRecord* GetMembership(MembershipId id) const noexcept;
    [[nodiscard]] std::vector<MembershipRecord> FindGroupsOf(GameplayObjectRef member) const;
    [[nodiscard]] std::vector<MembershipRecord> FindMembersOf(GameplayObjectRef group) const;
    [[nodiscard]] std::optional<RelationshipRecord> GetRelationship(GameplayObjectRef subject,GameplayObjectRef target,RelationshipTypeId type) const;
    [[nodiscard]] std::int64_t GetEffectiveAttitude(AttitudeQuery query) const;
    [[nodiscard]] std::optional<ReputationRecord> GetReputation(GameplayObjectRef subject,GameplayObjectRef scope,ReputationTrackId track) const;
    [[nodiscard]] SocialStandingSnapshot GetSocialStanding(GameplayObjectRef subject,GameplayObjectRef scope) const;
    [[nodiscard]] bool HasRole(GameplayObjectRef subject,SocialRoleId role,GameplayObjectRef scope={}) const;
    [[nodiscard]] std::vector<SocietyChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] SocietySnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(SocietySnapshot snapshot);
    [[nodiscard]] SocietyDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept{return revision_;}
private:
    void Bump() noexcept{revision_.value++;}
    void Record(SocietyChange change);
    [[nodiscard]] RelationshipRecord* FindMutableRelationship(GameplayObjectRef subject,GameplayObjectRef target,RelationshipTypeId type) noexcept;
    Revision revision_{};
    MonotonicIdGenerator<GameplayObjectId> membership_ids_{0x2400};
    MonotonicIdGenerator<GameplayObjectId> relationship_ids_{0x2401};
    std::unordered_map<GameplayObjectRef,SocialGroupDefinition,RefHash> groups_;
    std::unordered_map<MembershipId,MembershipRecord,IdHash> memberships_;
    std::unordered_map<RelationshipId,RelationshipRecord,IdHash> relationships_;
    std::vector<ReputationRecord> reputations_;
    std::vector<SocietyChange> changes_;
    std::uint64_t next_change_sequence_=1;
    mutable SocietyDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::society

namespace std{template<> struct hash<epidemic::gameplay::society::MembershipId>{size_t operator()(const epidemic::gameplay::society::MembershipId& v) const noexcept{return hash<epidemic::gameplay::GameplayObjectId>{}(v.value);}};template<> struct hash<epidemic::gameplay::society::RelationshipId>{size_t operator()(const epidemic::gameplay::society::RelationshipId& v) const noexcept{return hash<epidemic::gameplay::GameplayObjectId>{}(v.value);}};}
