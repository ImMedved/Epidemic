#include "allocation_fault_injection.h"
#include "Epidemic/Foundation/error.h"
#include "Epidemic/GameFramework/Processes/processes.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::processes;

namespace
{
enum class CallbackMode
{
    Success,
    Failure,
    Throw
};

enum class ProviderIdMode
{
    Normal,
    Invalid,
    FixedConflict
};

[[nodiscard]] RegisteredPayload PortableToken(std::uint64_t value)
{
    std::vector<std::byte> bytes(8);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<std::byte>((value >> (i * 8)) & 0xffu);
    return RegisteredPayload::FromVersioned(TypeId::FromString("test.process.token"), 1, std::move(bytes));
}

[[nodiscard]] RegisteredPayload NonPortableToken(std::uint64_t value)
{
    return RegisteredPayload::FromTrivial(TypeId::FromString("test.process.nonportable_token"), value);
}

[[nodiscard]] bool SamePayload(const RegisteredPayload &a, const RegisteredPayload &b)
{
    return a.type == b.type && a.schema_version == b.schema_version && a.portable == b.portable && a.bytes == b.bytes;
}

class FaultInput final : public IProcessInputProvider
{
  public:
    bool portable_token = true;
    ProviderIdMode id_mode = ProviderIdMode::Normal;
    CallbackMode release_mode = CallbackMode::Success;
    CallbackMode consume_mode = CallbackMode::Success;
    int reserve_calls = 0;
    int release_calls = 0;
    int consume_calls = 0;
    RegisteredPayload last_reserved_token;
    RegisteredPayload last_released_token;

    [[nodiscard]] foundation::Result<void> Validate(const ProcessInputDefinition &, const StartProcessRequest &,
                                                     ProcessInstanceId) override
    {
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<ReservedProcessInput> Reserve(const ProcessInputDefinition &input,
                                                                   const StartProcessRequest &,
                                                                   ProcessInstanceId instance) override
    {
        ++reserve_calls;
        ReservedProcessInput reservation;
        if (id_mode == ProviderIdMode::Normal)
            reservation.id = ProcessReservationId::FromRaw(instance.value.High(), static_cast<std::uint64_t>(reserve_calls));
        else if (id_mode == ProviderIdMode::FixedConflict)
            reservation.id = ProcessReservationId::FromRaw(instance.value.High(), 77);
        reservation.input = input.id;
        reservation.type = input.type;
        reservation.amount = input.amount;
        reservation.provider_token = portable_token ? PortableToken(static_cast<std::uint64_t>(reserve_calls))
                                                    : NonPortableToken(static_cast<std::uint64_t>(reserve_calls));
        last_reserved_token = reservation.provider_token;
        return foundation::Result<ReservedProcessInput>::Success(std::move(reservation));
    }

    [[nodiscard]] foundation::Result<void> Consume(const ReservedProcessInput &, GameplayContext) override
    {
        ++consume_calls;
        if (consume_mode == CallbackMode::Throw)
            throw std::runtime_error("consume failure");
        if (consume_mode == CallbackMode::Failure)
            return foundation::Result<void>::Failure(foundation::Error::Create("test.consume_failed", "consume failed"));
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void> Release(const ReservedProcessInput &reservation, GameplayContext) override
    {
        ++release_calls;
        last_released_token = reservation.provider_token;
        if (release_mode == CallbackMode::Throw)
            throw std::runtime_error("release failure");
        if (release_mode == CallbackMode::Failure)
            return foundation::Result<void>::Failure(foundation::Error::Create("test.release_failed", "release failed"));
        return foundation::Result<void>::Success();
    }
};

class FaultOutput final : public IProcessOutputHandler
{
  public:
    bool portable_token = true;
    int fail_prepare_on_call = 0;
    CallbackMode cancel_mode = CallbackMode::Success;
    CallbackMode commit_mode = CallbackMode::Success;
    int prepare_calls = 0;
    int cancel_calls = 0;
    int commit_calls = 0;
    RegisteredPayload last_prepared_token;
    RegisteredPayload last_cancelled_token;

    [[nodiscard]] bool Supports(ProcessOutputTypeId type) const noexcept override
    {
        return type == ProcessOutputTypeId::FromString("test.output");
    }

    [[nodiscard]] foundation::Result<PreparedProcessOutput> Prepare(const ProcessOutputDefinition &output,
                                                                    const ProcessInstance &, GameplayContext) override
    {
        ++prepare_calls;
        if (fail_prepare_on_call == prepare_calls)
            return foundation::Result<PreparedProcessOutput>::Failure(
                foundation::Error::Create("test.prepare_failed", "prepare failed"));
        PreparedProcessOutput prepared;
        prepared.output = output.id;
        prepared.type = output.type;
        prepared.delivery = output.delivery;
        prepared.provider_token = portable_token ? PortableToken(1000u + static_cast<std::uint64_t>(prepare_calls))
                                                 : NonPortableToken(1000u + static_cast<std::uint64_t>(prepare_calls));
        last_prepared_token = prepared.provider_token;
        return foundation::Result<PreparedProcessOutput>::Success(std::move(prepared));
    }

    [[nodiscard]] foundation::Result<void> Commit(const PreparedProcessOutput &, const ProcessInstance &,
                                                  GameplayContext) override
    {
        ++commit_calls;
        if (commit_mode == CallbackMode::Throw)
            throw std::runtime_error("commit failure");
        if (commit_mode == CallbackMode::Failure)
            return foundation::Result<void>::Failure(foundation::Error::Create("test.commit_failed", "commit failed"));
        return foundation::Result<void>::Success();
    }

    [[nodiscard]] foundation::Result<void> Cancel(const PreparedProcessOutput &output, const ProcessInstance &,
                                                  GameplayContext) override
    {
        ++cancel_calls;
        last_cancelled_token = output.provider_token;
        if (cancel_mode == CallbackMode::Throw)
            throw std::runtime_error("cancel failure");
        if (cancel_mode == CallbackMode::Failure)
            return foundation::Result<void>::Failure(foundation::Error::Create("test.cancel_failed", "cancel failed"));
        return foundation::Result<void>::Success();
    }
};

struct ConfiguredProcess
{
    ProcessDefinition definition;
    ProcessRecipe recipe;
    ProcessRecipeId recipe_id{};
};

[[nodiscard]] ConfiguredProcess Configure(ProcessesService &service, ProcessTimingPolicy timing,
                                          ProcessPersistencePolicy persistence, int input_count,
                                          std::vector<OutputDeliveryPolicy> outputs)
{
    ConfiguredProcess configured;
    configured.definition.canonical_name = "test.process";
    configured.definition.kind = ProcessKindId::FromString("test.kind");
    configured.definition.timing = timing;
    configured.definition.persistence = persistence;
    if (timing == ProcessTimingPolicy::Timed)
        configured.definition.steps.push_back(
            {ProcessStepId::FromString("test.step"), TypeId::FromString("test.step.kind"), GameplayDuration{10}, {}, {}});
    auto definition_id = service.RegisterDefinition(configured.definition);
    if (!definition_id)
        return configured;
    configured.definition.id = definition_id.Value();

    configured.recipe.canonical_name = "test.recipe";
    configured.recipe.process = definition_id.Value();
    for (int i = 0; i < input_count; ++i)
    {
        const auto input_name = i == 0 ? "test.input.0" : "test.input.1";
        configured.recipe.inputs.push_back({ProcessInputId::FromString(input_name),
                                            ProcessInputTypeId::FromString("test.input"),
                                            1,
                                            InputConsumptionPolicy::ReserveThenConsume,
                                            {},
                                            {}});
    }
    for (std::size_t i = 0; i < outputs.size(); ++i)
    {
        const auto output_name = i == 0 ? "test.output.0" : "test.output.1";
        configured.recipe.outputs.push_back({ProcessOutputId::FromString(output_name),
                                             ProcessOutputTypeId::FromString("test.output"),
                                             1,
                                             outputs[i],
                                             {}});
    }
    auto recipe_id = service.RegisterRecipe(configured.recipe);
    if (!recipe_id)
        return configured;
    configured.recipe.id = recipe_id.Value();
    configured.recipe_id = recipe_id.Value();
    service.Freeze();
    return configured;
}

[[nodiscard]] StartProcessRequest Request(ProcessRecipeId recipe, GameplayObjectRef actor, GameplayTimePoint now = {})
{
    StartProcessRequest request;
    request.recipe = recipe;
    request.actor = actor;
    request.now = now;
    request.context.time = now;
    return request;
}

[[nodiscard]] bool TestReservationIdExhaustionBeforeExternalCall()
{
    ProcessesService service;
    const auto configured = Configure(service, ProcessTimingPolicy::Timed, ProcessPersistencePolicy::Persistent, 1, {});
    if (!configured.recipe_id.IsValid())
        return false;
    auto snapshot = service.CaptureSnapshot();
    snapshot.reservation_ids.next = 0;
    if (!service.RestoreSnapshot(std::move(snapshot)))
        return false;
    FaultInput input;
    service.SetInputProvider(&input);
    const GameplayObjectRef actor{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("actor.exhaustion")};
    auto started = service.StartProcess(Request(configured.recipe_id, actor));
    return !started && input.reserve_calls == 0 && service.FindProcessesByActor(actor).empty();
}

[[nodiscard]] bool TestProviderReservationIdsAreCanonicalized()
{
    ProcessesService service;
    const auto configured = Configure(service, ProcessTimingPolicy::Timed, ProcessPersistencePolicy::Persistent, 2, {});
    if (!configured.recipe_id.IsValid())
        return false;
    FaultInput input;
    input.id_mode = ProviderIdMode::FixedConflict;
    service.SetInputProvider(&input);
    const GameplayObjectRef actor{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("actor.conflict")};
    auto started = service.StartProcess(Request(configured.recipe_id, actor));
    if (!started)
        return false;
    const auto *instance = service.FindInstance(started.Value());
    if (!instance || instance->reserved_inputs.size() != 2 ||
        instance->reserved_inputs[0].id == instance->reserved_inputs[1].id)
        return false;

    ProcessesService invalid_id_service;
    const auto invalid_config = Configure(invalid_id_service, ProcessTimingPolicy::Timed,
                                          ProcessPersistencePolicy::Persistent, 1, {});
    FaultInput invalid_input;
    invalid_input.id_mode = ProviderIdMode::Invalid;
    invalid_id_service.SetInputProvider(&invalid_input);
    auto invalid_started = invalid_id_service.StartProcess(
        Request(invalid_config.recipe_id,
                GameplayObjectRef{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("actor.invalid_id")}));
    const auto *invalid_instance = invalid_started ? invalid_id_service.FindInstance(invalid_started.Value()) : nullptr;
    return invalid_started && invalid_instance && invalid_instance->reserved_inputs.size() == 1 &&
           invalid_instance->reserved_inputs.front().id.IsValid();
}

[[nodiscard]] bool TestNonPortableInputCompensation(CallbackMode mode)
{
    ProcessesService service;
    const auto configured = Configure(service, ProcessTimingPolicy::Timed, ProcessPersistencePolicy::Persistent, 1, {});
    FaultInput input;
    input.portable_token = false;
    input.release_mode = mode;
    service.SetInputProvider(&input);
    const GameplayObjectRef actor{GameplayDomainId::FromString("test"),
                                  GameplayObjectId::FromString(mode == CallbackMode::Success ? "actor.input.release_ok" :
                                                               mode == CallbackMode::Failure ? "actor.input.release_fail" :
                                                                                               "actor.input.release_throw")};
    auto started = service.StartProcess(Request(configured.recipe_id, actor));
    if (mode == CallbackMode::Success)
        return !started && input.release_calls == 1 && service.FindProcessesByActor(actor).empty() &&
               SamePayload(input.last_reserved_token, input.last_released_token);

    if (!started)
        return false;
    const auto id = started.Value();
    const auto *instance = service.FindInstance(id);
    if (!instance || instance->state != ProcessInstanceState::ReconciliationRequired ||
        instance->reserved_inputs.size() != 1 || instance->reserved_inputs.front().state != ProcessInputCommitState::Reserved ||
        !SamePayload(instance->reserved_inputs.front().provider_token, input.last_reserved_token))
        return false;
    input.release_mode = CallbackMode::Success;
    auto cancelled = service.Cancel(id, GameplayTimePoint{1});
    const auto *after = service.FindInstance(id);
    return cancelled && after && after->state == ProcessInstanceState::Cancelled &&
           SamePayload(input.last_reserved_token, input.last_released_token);
}

[[nodiscard]] bool TestNonPortableOutputCompensation(CallbackMode mode)
{
    ProcessesService service;
    const auto configured = Configure(service, ProcessTimingPolicy::Timed, ProcessPersistencePolicy::Persistent, 0,
                                      {OutputDeliveryPolicy::Immediate});
    FaultOutput output;
    output.portable_token = false;
    output.cancel_mode = mode;
    service.AddOutputHandler(&output);
    const GameplayObjectRef actor{GameplayDomainId::FromString("test"),
                                  GameplayObjectId::FromString(mode == CallbackMode::Success ? "actor.output.cancel_ok" :
                                                               mode == CallbackMode::Failure ? "actor.output.cancel_fail" :
                                                                                               "actor.output.cancel_throw")};
    auto started = service.StartProcess(Request(configured.recipe_id, actor));
    if (mode == CallbackMode::Success)
        return !started && output.cancel_calls == 1 && service.FindProcessesByActor(actor).empty() &&
               SamePayload(output.last_prepared_token, output.last_cancelled_token);

    if (!started)
        return false;
    const auto id = started.Value();
    const auto *instance = service.FindInstance(id);
    if (!instance || instance->state != ProcessInstanceState::ReconciliationRequired ||
        instance->prepared_outputs.size() != 1 ||
        instance->prepared_outputs.front().state != ProcessOutputCommitState::Prepared ||
        !SamePayload(instance->prepared_outputs.front().provider_token, output.last_prepared_token))
        return false;
    output.cancel_mode = CallbackMode::Success;
    auto cancelled = service.Cancel(id, GameplayTimePoint{1});
    const auto *after = service.FindInstance(id);
    return cancelled && after && after->state == ProcessInstanceState::Cancelled &&
           SamePayload(output.last_prepared_token, output.last_cancelled_token);
}

[[nodiscard]] bool TestImmediatePrepareFailureWithInputRollbackFailure()
{
    ProcessesService service;
    const auto configured = Configure(service, ProcessTimingPolicy::Timed, ProcessPersistencePolicy::Persistent, 1,
                                      {OutputDeliveryPolicy::Immediate});
    FaultInput input;
    input.release_mode = CallbackMode::Failure;
    FaultOutput output;
    output.fail_prepare_on_call = 1;
    service.SetInputProvider(&input);
    service.AddOutputHandler(&output);
    auto started = service.StartProcess(Request(
        configured.recipe_id,
        GameplayObjectRef{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("actor.prepare_input_rollback")}));
    if (!started)
        return false;
    const auto *instance = service.FindInstance(started.Value());
    return instance && instance->state == ProcessInstanceState::ReconciliationRequired &&
           instance->reserved_inputs.size() == 1 && instance->reserved_inputs.front().state == ProcessInputCommitState::Reserved;
}

[[nodiscard]] bool TestEarlierPreparedOutputCancelFailureIsDurable()
{
    ProcessesService service;
    const auto configured = Configure(service, ProcessTimingPolicy::Timed, ProcessPersistencePolicy::Persistent, 1,
                                      {OutputDeliveryPolicy::Immediate, OutputDeliveryPolicy::Immediate});
    FaultInput input;
    FaultOutput output;
    output.fail_prepare_on_call = 2;
    output.cancel_mode = CallbackMode::Failure;
    service.SetInputProvider(&input);
    service.AddOutputHandler(&output);
    auto started = service.StartProcess(Request(
        configured.recipe_id,
        GameplayObjectRef{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("actor.prepared_cancel_failure")}));
    if (!started)
        return false;
    const auto *instance = service.FindInstance(started.Value());
    return instance && instance->state == ProcessInstanceState::ReconciliationRequired &&
           instance->prepared_outputs.size() == 1 &&
           instance->prepared_outputs.front().state == ProcessOutputCommitState::Prepared &&
           instance->reserved_inputs.size() == 1 && instance->reserved_inputs.front().state == ProcessInputCommitState::Released;
}

[[nodiscard]] bool TestInstantCompletionFailureKeepsOperationIdentity()
{
    ProcessesService service;
    const auto configured = Configure(service, ProcessTimingPolicy::Instant, ProcessPersistencePolicy::Persistent, 0,
                                      {OutputDeliveryPolicy::OnCompletion});
    FaultOutput output;
    output.commit_mode = CallbackMode::Failure;
    service.AddOutputHandler(&output);
    const GameplayObjectRef actor{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("actor.instant")};
    auto started = service.StartProcess(Request(configured.recipe_id, actor));
    if (!started)
        return false;
    const auto id = started.Value();
    const auto *instance = service.FindInstance(id);
    if (!instance || instance->state != ProcessInstanceState::ReconciliationRequired ||
        service.FindProcessesByActor(actor).size() != 1 || output.commit_calls != 1)
        return false;
    output.commit_mode = CallbackMode::Success;
    auto completed = service.Complete(id, GameplayTimePoint{0});
    const auto *after = service.FindInstance(id);
    return completed && after && after->state == ProcessInstanceState::Completed &&
           service.FindProcessesByActor(actor).size() == 1 && output.commit_calls == 2;
}

[[nodiscard]] bool TestCheckedTimeBoundaries()
{
    ProcessesService service;
    const auto configured = Configure(service, ProcessTimingPolicy::Timed, ProcessPersistencePolicy::Transient, 0, {});
    const GameplayObjectRef actor{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("actor.time")};
    auto started = service.StartProcess(Request(configured.recipe_id, actor,
                                                GameplayTimePoint{std::numeric_limits<std::int64_t>::max() - 5}));
    if (!started)
        return false;
    if (service.EvaluateProgress(started.Value(), GameplayTimePoint{std::numeric_limits<std::int64_t>::min()}) != 0)
        return false;
    if (!service.Pause(started.Value(), GameplayTimePoint{std::numeric_limits<std::int64_t>::min()}))
        return false;
    const auto *paused = service.FindInstance(started.Value());
    if (!paused || paused->paused_remaining.ticks != std::numeric_limits<std::int64_t>::max() ||
        service.EvaluateProgress(started.Value(), GameplayTimePoint{std::numeric_limits<std::int64_t>::max()}) != 0)
        return false;

    ProcessesService completed_service;
    const auto completed_config = Configure(completed_service, ProcessTimingPolicy::Timed,
                                            ProcessPersistencePolicy::Transient, 0, {});
    auto early = completed_service.StartProcess(Request(
        completed_config.recipe_id,
        GameplayObjectRef{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("actor.time.complete")},
        GameplayTimePoint{std::numeric_limits<std::int64_t>::min()}));
    return early && completed_service.EvaluateProgress(
                        early.Value(), GameplayTimePoint{std::numeric_limits<std::int64_t>::max()}) == 1'000'000;
}

[[nodiscard]] bool TestJournalSequenceSurvivesSnapshot()
{
    ProcessesService service;
    const auto configured = Configure(service, ProcessTimingPolicy::Timed, ProcessPersistencePolicy::Transient, 0, {});
    auto started = service.StartProcess(Request(
        configured.recipe_id,
        GameplayObjectRef{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("actor.journal")}));
    if (!started)
        return false;
    auto snapshot = service.CaptureSnapshot();
    if (snapshot.next_change_sequence <= 1)
        return false;
    const auto expected_latest_sequence = snapshot.next_change_sequence - 1;

    ProcessesService restored;
    const auto restored_config = Configure(restored, ProcessTimingPolicy::Timed, ProcessPersistencePolicy::Transient, 0, {});
    (void)restored_config;
    if (!restored.RestoreSnapshot(std::move(snapshot)))
        return false;
    const auto stale = restored.ReadChangesSince(ChangeCursor{});
    const auto current = restored.ReadChangesSince(restored.LatestChangeCursor());
    return stale.snapshot_required && stale.latest_cursor.sequence == expected_latest_sequence && !current.snapshot_required &&
           current.changes.empty();
}
} // namespace

int main()
{
    if (!TestReservationIdExhaustionBeforeExternalCall())
        return 1;
    if (!TestProviderReservationIdsAreCanonicalized())
        return 2;
    if (!TestNonPortableInputCompensation(CallbackMode::Success))
        return 3;
    if (!TestNonPortableInputCompensation(CallbackMode::Failure))
        return 4;
    if (!TestNonPortableInputCompensation(CallbackMode::Throw))
        return 5;
    if (!TestNonPortableOutputCompensation(CallbackMode::Success))
        return 6;
    if (!TestNonPortableOutputCompensation(CallbackMode::Failure))
        return 7;
    if (!TestNonPortableOutputCompensation(CallbackMode::Throw))
        return 8;
    if (!TestImmediatePrepareFailureWithInputRollbackFailure())
        return 9;
    if (!TestEarlierPreparedOutputCancelFailureIsDurable())
        return 10;
    if (!TestInstantCompletionFailureKeepsOperationIdentity())
        return 11;
    if (!TestCheckedTimeBoundaries())
        return 12;
    if (!TestJournalSequenceSurvivesSnapshot())
        return 13;
    // Milestone 2: RestoreSnapshot preserves live state at allocation boundaries.
    ProcessesService restore_fault_service;
    (void)Configure(restore_fault_service, ProcessTimingPolicy::Timed, ProcessPersistencePolicy::Transient, 0, {});
    const auto allocation_before = restore_fault_service.CaptureSnapshot();
    bool saw_restore_allocation_failure = false;
    for (long long fail_after = 0; fail_after < 32; ++fail_after)
    {
        auto allocation_target = allocation_before;
        bool failed = false;
        try
        {
            epidemic::tests::allocation_fault::FailAfter fault(fail_after);
            const auto restored_under_fault = restore_fault_service.RestoreSnapshot(std::move(allocation_target));
            failed = !restored_under_fault;
        }
        catch (const std::bad_alloc &)
        {
            failed = true;
        }
        if (!failed)
            break;
        saw_restore_allocation_failure = true;
        if (restore_fault_service.CaptureSnapshot().revision != allocation_before.revision)
            return 937;
    }
    if (!saw_restore_allocation_failure)
        return 938;
    return 0;
}
