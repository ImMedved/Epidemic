#pragma once

#include "Epidemic/Foundation/result.h"
#include "Epidemic/GameFramework/Foundation/gameplay_foundation.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace epidemic::gameplay::processes
{
using Fixed = std::int64_t;

struct ProcessDefinitionId
{
    TypeId value{};
    static constexpr ProcessDefinitionId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const ProcessDefinitionId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ProcessDefinitionId &) const noexcept = default;
};
struct ProcessRecipeId
{
    TypeId value{};
    static constexpr ProcessRecipeId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const ProcessRecipeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ProcessRecipeId &) const noexcept = default;
};
struct ProcessKindId
{
    TypeId value{};
    static constexpr ProcessKindId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const ProcessKindId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ProcessKindId &) const noexcept = default;
};
struct ProcessStepId
{
    TypeId value{};
    static constexpr ProcessStepId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const ProcessStepId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ProcessStepId &) const noexcept = default;
};
struct ProcessInputId
{
    TypeId value{};
    static constexpr ProcessInputId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const ProcessInputId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ProcessInputId &) const noexcept = default;
};
struct ProcessOutputId
{
    TypeId value{};
    static constexpr ProcessOutputId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const ProcessOutputId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ProcessOutputId &) const noexcept = default;
};
struct ProcessInputTypeId
{
    TypeId value{};
    static constexpr ProcessInputTypeId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const ProcessInputTypeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ProcessInputTypeId &) const noexcept = default;
};
struct ProcessOutputTypeId
{
    TypeId value{};
    static constexpr ProcessOutputTypeId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const ProcessOutputTypeId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ProcessOutputTypeId &) const noexcept = default;
};
struct ProcessStationId
{
    GameplayObjectId value{};
    static constexpr ProcessStationId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const ProcessStationId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ProcessStationId &) const noexcept = default;
};
struct ProcessInstanceId
{
    GameplayObjectId value{};
    static constexpr ProcessInstanceId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const ProcessInstanceId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ProcessInstanceId &) const noexcept = default;
};
struct ProcessReservationId
{
    GameplayObjectId value{};
    static constexpr ProcessReservationId FromRaw(std::uint64_t h, std::uint64_t l) noexcept
    {
        return {GameplayObjectId::FromRaw(h, l)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const ProcessReservationId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ProcessReservationId &) const noexcept = default;
};
struct ProcessQualityId
{
    TypeId value{};
    static constexpr ProcessQualityId FromString(std::string_view s) noexcept
    {
        return {TypeId::FromString(s)};
    }
    [[nodiscard]] constexpr bool IsValid() const noexcept
    {
        return value.IsValid();
    }
    [[nodiscard]] constexpr bool operator==(const ProcessQualityId &) const noexcept = default;
    [[nodiscard]] constexpr auto operator<=>(const ProcessQualityId &) const noexcept = default;
};

struct IdHash
{
    template <class T> [[nodiscard]] std::size_t operator()(const T &id) const noexcept
    {
        return std::hash<decltype(id.value)>{}(id.value);
    }
};

struct RegisteredPayload
{
    TypeId type{};
    std::uint32_t schema_version = 0;
    bool portable = false;
    std::vector<std::byte> bytes;
    template <class T> [[nodiscard]] static RegisteredPayload FromTrivial(TypeId type_id, const T &value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        RegisteredPayload payload;
        payload.type = type_id;
        payload.schema_version = 0;
        payload.portable = false;
        payload.bytes.resize(sizeof(T));
        std::memcpy(payload.bytes.data(), &value, sizeof(T));
        return payload;
    }
    [[nodiscard]] static RegisteredPayload FromVersioned(TypeId type_id, std::uint32_t version, std::vector<std::byte> encoded)
    {
        RegisteredPayload payload;
        payload.type = type_id;
        payload.schema_version = version;
        payload.portable = true;
        payload.bytes = std::move(encoded);
        return payload;
    }
    [[nodiscard]] bool IsPortable() const noexcept { return !type.IsValid() || portable; }
    template <class T> [[nodiscard]] std::optional<T> AsTrivial(TypeId expected) const
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (type != expected || bytes.size() != sizeof(T))
            return std::nullopt;
        T value{};
        std::memcpy(&value, bytes.data(), sizeof(T));
        return value;
    }
};

enum class ProcessTimingPolicy
{
    Instant,
    Timed,
    ExternalCompletion
};
enum class ProcessPersistencePolicy
{
    Transient,
    Session,
    Persistent
};
enum class InputConsumptionPolicy
{
    ConsumeOnStart,
    ConsumeOnCompletion,
    ReserveThenConsume,
    ToolNotConsumed,
    Catalyst,
    ConditionRequired
};
enum class OutputDeliveryPolicy
{
    Immediate,
    OnCompletion
};
enum class ProcessInstanceState
{
    Prepared,
    Reserved,
    Running,
    Paused,
    Completed,
    Failed,
    Cancelled,
    Expired,
    ReconciliationRequired
};
enum class StationState
{
    Active,
    Disabled,
    Destroyed,
    Occupied,
    Unavailable
};
enum class ProcessChangeKind
{
    Prepared,
    Started,
    Paused,
    Resumed,
    Completed,
    Failed,
    Cancelled,
    InputReserved,
    InputConsumed,
    OutputProduced,
    QualityResolved,
    ReconciliationRequired
};

struct ProcessStepDefinition
{
    ProcessStepId id{};
    TypeId step_type{};
    GameplayDuration duration{};
    std::vector<TypeId> requirement_ids;
    RegisteredPayload payload;
};
struct ProcessInputDefinition
{
    ProcessInputId id{};
    ProcessInputTypeId type{};
    Fixed amount = 0;
    InputConsumptionPolicy consumption = InputConsumptionPolicy::ReserveThenConsume;
    GameplayTagSet required_tags;
    RegisteredPayload payload;
};
struct ProcessOutputDefinition
{
    ProcessOutputId id{};
    ProcessOutputTypeId type{};
    Fixed amount = 0;
    OutputDeliveryPolicy delivery = OutputDeliveryPolicy::OnCompletion;
    RegisteredPayload payload;
};
struct ProcessDefinition
{
    ProcessDefinitionId id{};
    std::string canonical_name;
    GameplayTagSet tags;
    ProcessKindId kind{};
    std::vector<ProcessStepDefinition> steps;
    ProcessTimingPolicy timing = ProcessTimingPolicy::Instant;
    ProcessPersistencePolicy persistence = ProcessPersistencePolicy::Transient;
    RegisteredPayload payload;
    Revision revision{};
};
struct ProcessRecipe
{
    ProcessRecipeId id{};
    std::string canonical_name;
    ProcessDefinitionId process{};
    std::vector<ProcessInputDefinition> inputs;
    std::vector<ProcessOutputDefinition> outputs;
    std::vector<TypeId> requirements;
    GameplayTagSet station_capabilities;
    RegisteredPayload payload;
    Revision revision{};
};
struct ProcessStation
{
    ProcessStationId id{};
    GameplayObjectRef station_object{};
    GameplayTagSet capabilities;
    StationState state = StationState::Active;
    Fixed efficiency_micro = 1'000'000;
    Revision revision{};
};
struct ProcessQualityResult
{
    ProcessQualityId quality{};
    Fixed quality_score_micro = 1'000'000;
    std::vector<TypeId> reasons;
};
enum class ProcessInputCommitState
{
    Reserved,
    Consumed,
    Released
};
enum class ProcessOutputCommitState
{
    Prepared,
    Committed,
    Cancelled
};
struct ReservedProcessInput
{
    ProcessReservationId id{};
    ProcessInputId input{};
    ProcessInputTypeId type{};
    Fixed amount = 0;
    InputConsumptionPolicy consumption = InputConsumptionPolicy::ReserveThenConsume;
    ProcessInputCommitState state = ProcessInputCommitState::Reserved;
    RegisteredPayload provider_token;
};
struct PreparedProcessOutput
{
    ProcessOutputId output{};
    ProcessOutputTypeId type{};
    OutputDeliveryPolicy delivery = OutputDeliveryPolicy::OnCompletion;
    ProcessOutputCommitState state = ProcessOutputCommitState::Prepared;
    RegisteredPayload provider_token;
};
struct ProcessInstance
{
    ProcessInstanceId id{};
    ProcessRecipeId recipe{};
    GameplayObjectRef actor{};
    GameplayObjectRef station{};
    GameplayObjectRef target{};
    GameplayObjectRef simulation_area{};
    ProcessInstanceState state = ProcessInstanceState::Prepared;
    GameplayTimePoint started_at{};
    GameplayTimePoint due_at{};
    GameplayTimePoint last_updated_at{};
    std::vector<ReservedProcessInput> reserved_inputs;
    std::vector<PreparedProcessOutput> prepared_outputs;
    GameplayDuration total_duration{};
    GameplayDuration paused_remaining{};
    ProcessQualityResult quality{};
    RegisteredPayload runtime_payload;
    Revision revision{};
};
struct StartProcessRequest
{
    ProcessRecipeId recipe{};
    GameplayObjectRef actor{};
    GameplayObjectRef station{};
    GameplayObjectRef target{};
    GameplayTimePoint now{};
    std::uint64_t seed = 0;
    GameplayContext context{};
    GameplayObjectRef simulation_area{};
};
struct ProcessChange
{
    std::uint64_t sequence = 0;
    ProcessChangeKind kind = ProcessChangeKind::Prepared;
    ProcessInstanceId process{};
    ProcessRecipeId recipe{};
    GameplayObjectRef actor{};
    GameplayTimePoint time{};
    GameplayContext context{};
    Revision revision{};
};
struct ProcessChangeBatch
{
    bool snapshot_required = false;
    std::uint64_t oldest_available_sequence = 0;
    std::uint64_t latest_sequence = 0;
    std::vector<ProcessChange> changes;
};

struct ProcessesSnapshot
{
    std::vector<ProcessStation> stations;
    std::vector<ProcessInstance> instances;
    MonotonicIdGenerator<GameplayObjectId>::Snapshot station_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot instance_ids{};
    MonotonicIdGenerator<GameplayObjectId>::Snapshot reservation_ids{};
    Revision revision{};
};
struct ProcessesDiagnostics
{
    std::uint64_t stations = 0;
    std::uint64_t active_processes = 0;
    std::uint64_t completed_processes = 0;
    std::uint64_t failed_processes = 0;
    std::uint64_t reservations = 0;
    std::uint64_t outputs = 0;
};

class IProcessInputProvider
{
  public:
    virtual ~IProcessInputProvider() = default;
    [[nodiscard]] virtual foundation::Result<void> Validate(const ProcessInputDefinition &input,
                                                            const StartProcessRequest &request,
                                                            ProcessInstanceId instance) = 0;
    [[nodiscard]] virtual foundation::Result<ReservedProcessInput> Reserve(const ProcessInputDefinition &input,
                                                                           const StartProcessRequest &request,
                                                                           ProcessInstanceId instance) = 0;
    // Consume and Release must be idempotent for the stable provider token. A successful call may be
    // repeated after restore/reconciliation without applying the mutation twice.
    [[nodiscard]] virtual foundation::Result<void> Consume(const ReservedProcessInput &reservation,
                                                           GameplayContext context) = 0;
    [[nodiscard]] virtual foundation::Result<void> Release(const ReservedProcessInput &reservation,
                                                           GameplayContext context) = 0;
};
class IProcessOutputHandler
{
  public:
    virtual ~IProcessOutputHandler() = default;
    [[nodiscard]] virtual bool Supports(ProcessOutputTypeId type) const noexcept = 0;
    // Prepare is the only stage allowed to fail before delivery. The returned token must be stable
    // and Commit must be idempotent for it so a partially completed process can resume safely.
    [[nodiscard]] virtual foundation::Result<PreparedProcessOutput> Prepare(const ProcessOutputDefinition &output,
                                                                           const ProcessInstance &instance,
                                                                           GameplayContext context) = 0;
    [[nodiscard]] virtual foundation::Result<void> Commit(const PreparedProcessOutput &output,
                                                          const ProcessInstance &instance,
                                                          GameplayContext context) = 0;
    [[nodiscard]] virtual foundation::Result<void> Cancel(const PreparedProcessOutput &output,
                                                          const ProcessInstance &instance,
                                                          GameplayContext context) = 0;
};
class IProcessQualityProvider
{
  public:
    virtual ~IProcessQualityProvider() = default;
    [[nodiscard]] virtual ProcessQualityResult Resolve(const ProcessRecipe &recipe,
                                                       const StartProcessRequest &request) const = 0;
};

class ProcessesService
{
  public:
    ProcessesService();
    [[nodiscard]] static constexpr GameplayDomainId Domain() noexcept
    {
        return GameplayDomainId::FromString("framework.processes");
    }
    [[nodiscard]] foundation::Result<ProcessDefinitionId> RegisterDefinition(ProcessDefinition definition);
    [[nodiscard]] foundation::Result<ProcessRecipeId> RegisterRecipe(ProcessRecipe recipe);
    [[nodiscard]] foundation::Result<ProcessStationId> RegisterStation(ProcessStation station);
    void Freeze() noexcept
    {
        frozen_ = true;
    }
    void SetInputProvider(IProcessInputProvider *provider) noexcept
    {
        input_provider_ = provider;
    }
    void AddOutputHandler(IProcessOutputHandler *handler)
    {
        output_handlers_.push_back(handler);
    }
    void SetQualityProvider(const IProcessQualityProvider *provider) noexcept
    {
        quality_provider_ = provider;
    }

    [[nodiscard]] const ProcessDefinition *FindDefinition(ProcessDefinitionId id) const noexcept;
    [[nodiscard]] const ProcessRecipe *FindRecipe(ProcessRecipeId id) const noexcept;
    [[nodiscard]] const ProcessStation *FindStationByObject(GameplayObjectRef station) const noexcept;
    [[nodiscard]] const ProcessInstance *FindInstance(ProcessInstanceId id) const noexcept;
    [[nodiscard]] Fixed EvaluateProgress(ProcessInstanceId id, GameplayTimePoint now) const noexcept;

    [[nodiscard]] foundation::Result<ProcessInstanceId> StartProcess(StartProcessRequest request);
    [[nodiscard]] foundation::Result<void> Pause(ProcessInstanceId id, GameplayTimePoint now,
                                                 GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> Resume(ProcessInstanceId id, GameplayTimePoint now,
                                                  GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> Cancel(ProcessInstanceId id, GameplayTimePoint now,
                                                  GameplayContext context = {});
    [[nodiscard]] foundation::Result<void> Complete(ProcessInstanceId id, GameplayTimePoint now,
                                                    GameplayContext context = {});
    [[nodiscard]] foundation::Result<std::vector<ProcessInstanceId>> CompleteDue(GameplayTimePoint now);
    [[nodiscard]] std::vector<ProcessInstance> FindDueProcessesForSimulation(GameplayObjectRef simulation_area,
                                                                              GameplayTimePoint from,
                                                                              GameplayTimePoint to) const;
    [[nodiscard]] foundation::Result<std::vector<ProcessInstanceId>> CompletePreparedDueForSimulation(
        GameplayObjectRef simulation_area, std::span<const ProcessInstanceId> process_ids, GameplayTimePoint now,
        GameplayContext context = {});

    [[nodiscard]] std::vector<ProcessInstance> FindProcessesByActor(GameplayObjectRef actor) const;
    [[nodiscard]] std::vector<ProcessInstance> FindProcessesByStation(GameplayObjectRef station) const;
    [[nodiscard]] std::vector<ProcessChange> ChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] ProcessChangeBatch ReadChangesSince(std::uint64_t sequence) const;
    [[nodiscard]] foundation::Result<void> PruneTerminalProcesses(std::size_t keep_recent = 0);
    [[nodiscard]] ProcessesSnapshot CaptureSnapshot() const;
    [[nodiscard]] foundation::Result<void> RestoreSnapshot(ProcessesSnapshot snapshot);
    [[nodiscard]] ProcessesDiagnostics GetDiagnostics() const noexcept;

  private:
    void Bump() noexcept
    {
        ++revision_.value;
    }
    void Record(ProcessChange change);
    [[nodiscard]] foundation::Result<void> ReserveInputs(ProcessInstance &instance, const ProcessRecipe &recipe,
                                                         const StartProcessRequest &request);
    [[nodiscard]] foundation::Result<void> ConsumeInputs(ProcessInstance &instance, InputConsumptionPolicy phase,
                                                         GameplayContext context);
    [[nodiscard]] foundation::Result<void> ReleaseInputs(ProcessInstance &instance, GameplayContext context);
    [[nodiscard]] foundation::Result<void> PrepareOutputs(ProcessInstance &instance, const ProcessRecipe &recipe,
                                                          OutputDeliveryPolicy phase, GameplayContext context);
    [[nodiscard]] foundation::Result<void> CommitOutputs(ProcessInstance &instance, OutputDeliveryPolicy phase,
                                                         GameplayContext context);
    [[nodiscard]] foundation::Result<void> CancelPreparedOutputs(ProcessInstance &instance, GameplayContext context);
    [[nodiscard]] foundation::Result<void> ValidateProviderInput(const ProcessInputDefinition &input,
                                                                 const StartProcessRequest &request,
                                                                 ProcessInstanceId instance) const;
    [[nodiscard]] GameplayDuration DurationFor(const ProcessRecipe &recipe, const ProcessDefinition &definition,
                                               const ProcessStation *station) const noexcept;

    bool frozen_ = false;
    Revision revision_{};
    std::unordered_map<ProcessDefinitionId, ProcessDefinition, IdHash> definitions_;
    std::unordered_map<ProcessRecipeId, ProcessRecipe, IdHash> recipes_;
    std::unordered_map<ProcessStationId, ProcessStation, IdHash> stations_;
    std::unordered_map<GameplayObjectRef, ProcessStationId> station_by_object_;
    std::unordered_map<ProcessInstanceId, ProcessInstance, IdHash> instances_;
    MonotonicIdGenerator<GameplayObjectId> station_ids_;
    MonotonicIdGenerator<GameplayObjectId> instance_ids_;
    MonotonicIdGenerator<GameplayObjectId> reservation_ids_;
    IProcessInputProvider *input_provider_ = nullptr;
    std::vector<IProcessOutputHandler *> output_handlers_;
    const IProcessQualityProvider *quality_provider_ = nullptr;
    std::deque<ProcessChange> changes_;
    std::size_t change_journal_capacity_ = 4096;
    std::uint64_t next_change_sequence_ = 1;
    ProcessesDiagnostics diagnostics_{};
};
} // namespace epidemic::gameplay::processes
