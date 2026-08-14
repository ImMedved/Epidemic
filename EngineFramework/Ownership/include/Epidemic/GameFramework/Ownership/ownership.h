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

namespace epidemic::gameplay::ownership
{
struct OwnershipRecordId{GameplayObjectId value{}; static constexpr OwnershipRecordId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} static constexpr OwnershipRecordId FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const OwnershipRecordId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const OwnershipRecordId&) const noexcept=default;};
struct PropertyClaimId{GameplayObjectId value{}; static constexpr PropertyClaimId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} static constexpr PropertyClaimId FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const PropertyClaimId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const PropertyClaimId&) const noexcept=default;};
struct AccessRuleId{GameplayObjectId value{}; static constexpr AccessRuleId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} static constexpr AccessRuleId FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const AccessRuleId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const AccessRuleId&) const noexcept=default;};
struct PermissionGrantId{GameplayObjectId value{}; static constexpr PermissionGrantId FromString(std::string_view s) noexcept{return {GameplayObjectId::FromString(s)};} static constexpr PermissionGrantId FromRaw(std::uint64_t h,std::uint64_t l) noexcept{return {GameplayObjectId::FromRaw(h,l)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const PermissionGrantId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const PermissionGrantId&) const noexcept=default;};
struct PropertyRightId{TypeId value{}; static constexpr PropertyRightId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const PropertyRightId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const PropertyRightId&) const noexcept=default;};
struct PropertyDomainId{TypeId value{}; static constexpr PropertyDomainId FromString(std::string_view s) noexcept{return {TypeId::FromString(s)};} [[nodiscard]] constexpr bool IsValid() const noexcept{return value.IsValid();} [[nodiscard]] constexpr bool operator==(const PropertyDomainId&) const noexcept=default; [[nodiscard]] constexpr auto operator<=>(const PropertyDomainId&) const noexcept=default;};
struct IdHash{template<class T> [[nodiscard]] std::size_t operator()(const T& id) const noexcept{return std::hash<decltype(id.value)>{}(id.value);}};
struct RefHash{[[nodiscard]] std::size_t operator()(GameplayObjectRef r) const noexcept{return std::hash<GameplayObjectRef>{}(r);}};

enum class OwnershipStrength{Owned,Claimed,Borrowed,Leased,Reserved,Stolen,Contested,Abandoned,Public,Unowned};
enum class PermissionStrength{Weak,Normal,Strong,Absolute};
enum class AccessDecision{Allow,Deny,RequirePermission,RequireRole,RequireOwner,Public,Private,TrespassIfViolated,CrimeIfViolated};
enum class PermissionDecision{Allowed,Denied,Trespass,CrimeCandidate,Unknown,NotApplicable};
enum class OwnershipInheritancePolicy{InheritOwner,OverrideOwner,NoInheritance,PublicInside};
enum class TransferReason{Trade,Gift,Reward,Loot,Theft,Confiscation,Inheritance,Abandonment,ConstructionCreated,GameSpecific};
enum class OwnershipChangeKind{OwnershipAssigned,OwnershipTransferred,OwnershipRemoved,PermissionGranted,PermissionRevoked,AccessRuleAdded,AccessRuleChanged,AccessRuleRemoved,PropertyClaimCreated,PropertyClaimResolved};

struct OwnershipRecord{OwnershipRecordId id{};GameplayObjectRef property{};GameplayObjectRef owner{};PropertyDomainId domain{};OwnershipStrength strength=OwnershipStrength::Owned;GameplayTimePoint acquired_at{};std::vector<std::byte> payload;Revision revision{};};
struct PermissionGrant{PermissionGrantId id{};GameplayObjectRef subject{};GameplayObjectRef property{};PropertyRightId right{};TypeId source{};GameplayTimePoint granted_at{};std::optional<GameplayTimePoint> expires_at;PermissionStrength strength=PermissionStrength::Normal;Revision revision{};};
struct AccessRule{AccessRuleId id{};GameplayObjectRef property{};PropertyRightId right{};AccessDecision decision=AccessDecision::RequirePermission;int priority=0;GameplayObjectRef required_subject{};TypeId required_role{};Revision revision{};};
struct PropertyClaim{PropertyClaimId id{};GameplayObjectRef claimant{};GameplayObjectRef property{};std::int64_t strength_micro=0;GameplayTimePoint created_at{};std::vector<std::byte> payload;Revision revision{};};
struct OwnershipPermissionQuery{GameplayObjectRef actor{};GameplayObjectRef property{};PropertyRightId right{};GameplayContext context{};};
struct OwnershipPermissionResult{PermissionDecision decision=PermissionDecision::Unknown;GameplayObjectRef effective_owner{};std::vector<TypeId> reasons;Revision revision{};};
struct TransferOwnershipRequest{GameplayObjectRef property{};GameplayObjectRef from_owner{};GameplayObjectRef to_owner{};TransferReason reason=TransferReason::Trade;GameplayContext context{};};
struct OwnershipChange{std::uint64_t sequence=0;OwnershipChangeKind kind=OwnershipChangeKind::OwnershipAssigned;GameplayObjectRef property{};GameplayObjectRef actor{};GameplayObjectRef owner{};PropertyRightId right{};GameplayContext context{};Revision revision{};};
struct OwnershipSnapshot{std::vector<OwnershipRecord> records;std::vector<PermissionGrant> grants;std::vector<AccessRule> rules;std::vector<PropertyClaim> claims;MonotonicIdGenerator<GameplayObjectId>::Snapshot ownership_ids{};MonotonicIdGenerator<GameplayObjectId>::Snapshot grant_ids{};MonotonicIdGenerator<GameplayObjectId>::Snapshot rule_ids{};MonotonicIdGenerator<GameplayObjectId>::Snapshot claim_ids{};Revision revision{};};
struct OwnershipDiagnostics{std::uint64_t ownership_records=0,permission_grants=0,access_rules=0,property_claims=0,permission_queries=0,denied_permissions=0,crime_candidate_permissions=0,ownership_transfers=0,claim_conflicts=0;};

class OwnershipService
{
public:
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept{return GameplayDomainId::FromString("framework.ownership");}
    [[nodiscard]] foundation::Result<OwnershipRecordId> AssignOwnership(OwnershipRecord record);
    [[nodiscard]] foundation::Result<void> RemoveOwnership(OwnershipRecordId id,GameplayContext context={});
    [[nodiscard]] foundation::Result<void> TransferOwnership(TransferOwnershipRequest request);
    [[nodiscard]] foundation::Result<PermissionGrantId> GrantPermission(PermissionGrant grant);
    [[nodiscard]] foundation::Result<void> RevokePermission(PermissionGrantId id,GameplayContext context={});
    [[nodiscard]] foundation::Result<AccessRuleId> AddAccessRule(AccessRule rule);
    [[nodiscard]] foundation::Result<PropertyClaimId> CreateClaim(PropertyClaim claim);
    [[nodiscard]] foundation::Result<void> ResolveClaim(PropertyClaimId id,GameplayContext context={});
    [[nodiscard]] OwnershipPermissionResult EvaluatePermission(OwnershipPermissionQuery query) const;
    [[nodiscard]] const OwnershipRecord* GetOwner(GameplayObjectRef property) const noexcept;
    [[nodiscard]] std::vector<OwnershipRecord> GetOwnershipRecords(GameplayObjectRef property) const;
    [[nodiscard]] std::vector<OwnershipRecord> FindPropertiesOwnedBy(GameplayObjectRef owner) const;
    [[nodiscard]] std::vector<PermissionGrant> FindPermissions(GameplayObjectRef subject) const;
    [[nodiscard]] std::vector<PropertyClaim> FindClaims(GameplayObjectRef property) const;
    [[nodiscard]] std::vector<AccessRule> FindAccessRules(GameplayObjectRef property) const;
    [[nodiscard]] std::vector<OwnershipChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] OwnershipSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(OwnershipSnapshot snapshot);
    [[nodiscard]] OwnershipDiagnostics GetDiagnostics() const noexcept;
    [[nodiscard]] Revision CurrentRevision() const noexcept{return revision_;}
private:
    void Bump() noexcept{revision_.value++;}
    void Record(OwnershipChange change);
    [[nodiscard]] OwnershipRecord* FindMutableOwnership(OwnershipRecordId id) noexcept;
    Revision revision_{};
    MonotonicIdGenerator<GameplayObjectId> ownership_ids_{0x2300};
    MonotonicIdGenerator<GameplayObjectId> grant_ids_{0x2301};
    MonotonicIdGenerator<GameplayObjectId> rule_ids_{0x2302};
    MonotonicIdGenerator<GameplayObjectId> claim_ids_{0x2303};
    std::unordered_map<OwnershipRecordId,OwnershipRecord,IdHash> records_;
    std::unordered_map<PermissionGrantId,PermissionGrant,IdHash> grants_;
    std::unordered_map<AccessRuleId,AccessRule,IdHash> rules_;
    std::unordered_map<PropertyClaimId,PropertyClaim,IdHash> claims_;
    std::vector<OwnershipChange> changes_;
    std::uint64_t next_change_sequence_=1;
    mutable OwnershipDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::ownership

namespace std{template<> struct hash<epidemic::gameplay::ownership::OwnershipRecordId>{size_t operator()(const epidemic::gameplay::ownership::OwnershipRecordId& v) const noexcept{return hash<epidemic::gameplay::GameplayObjectId>{}(v.value);}};template<> struct hash<epidemic::gameplay::ownership::PermissionGrantId>{size_t operator()(const epidemic::gameplay::ownership::PermissionGrantId& v) const noexcept{return hash<epidemic::gameplay::GameplayObjectId>{}(v.value);}};template<> struct hash<epidemic::gameplay::ownership::AccessRuleId>{size_t operator()(const epidemic::gameplay::ownership::AccessRuleId& v) const noexcept{return hash<epidemic::gameplay::GameplayObjectId>{}(v.value);}};template<> struct hash<epidemic::gameplay::ownership::PropertyClaimId>{size_t operator()(const epidemic::gameplay::ownership::PropertyClaimId& v) const noexcept{return hash<epidemic::gameplay::GameplayObjectId>{}(v.value);}};}
