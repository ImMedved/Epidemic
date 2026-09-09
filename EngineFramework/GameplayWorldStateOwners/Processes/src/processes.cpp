#include "Epidemic/GameFramework/Processes/processes.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace epidemic::gameplay::processes
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}

[[nodiscard]] bool IsTerminal(ProcessInstanceState state) noexcept
{
    return state == ProcessInstanceState::Completed || state == ProcessInstanceState::Failed ||
           state == ProcessInstanceState::Cancelled || state == ProcessInstanceState::Expired;
}

[[nodiscard]] bool NeedsReservation(InputConsumptionPolicy policy) noexcept
{
    return policy == InputConsumptionPolicy::ConsumeOnStart ||
           policy == InputConsumptionPolicy::ConsumeOnCompletion ||
           policy == InputConsumptionPolicy::ReserveThenConsume || policy == InputConsumptionPolicy::Catalyst;
}

[[nodiscard]] bool ConsumesAt(InputConsumptionPolicy policy, InputConsumptionPolicy phase) noexcept
{
    if (phase == InputConsumptionPolicy::ConsumeOnStart)
        return policy == InputConsumptionPolicy::ConsumeOnStart;
    return policy == InputConsumptionPolicy::ConsumeOnCompletion ||
           policy == InputConsumptionPolicy::ReserveThenConsume;
}

[[nodiscard]] bool HasAllExact(const GameplayTagSet &available, const GameplayTagSet &required) noexcept
{
    for (const auto tag : required.Values())
        if (!available.HasExact(tag))
            return false;
    return true;
}

[[nodiscard]] std::uint64_t Low(GameplayObjectId id) noexcept
{
    return id.Low();
}

template <class TId>
void AdvanceGeneratorPastAcceptedId(MonotonicIdGenerator<GameplayObjectId> &generator, TId id) noexcept
{
    auto snapshot = generator.GetSnapshot();
    if (!id.IsValid() || id.value.High() != snapshot.scope || snapshot.next == 0)
        return;
    const auto low = id.value.Low();
    if (low < snapshot.next)
        return;
    snapshot.next = low == std::numeric_limits<std::uint64_t>::max() ? 0 : low + 1;
    generator.Restore(snapshot);
}

[[nodiscard]] foundation::Error CallbackError(std::string_view stage)
{
    return foundation::Error::Create("gameplay.processes.provider_exception", stage);
}
} // namespace

ProcessesService::ProcessesService() : station_ids_(0x30320001), instance_ids_(0x30320002), reservation_ids_(0x30320003)
{
}

foundation::Result<ProcessDefinitionId> ProcessesService::RegisterDefinition(ProcessDefinition definition)
{
    if (frozen_)
        return foundation::Result<ProcessDefinitionId>::Failure(
            Error("gameplay.processes.registry_frozen", "process registry is frozen"));
    if (definition.canonical_name.empty() || !definition.kind.IsValid())
        return foundation::Result<ProcessDefinitionId>::Failure(
            Error("gameplay.processes.invalid_definition", "process definition name and kind are required"));
    const auto canonical = ProcessDefinitionId::FromString(definition.canonical_name);
    if (!definition.id.IsValid())
        definition.id = canonical;
    if (definition.id != canonical || definitions_.contains(definition.id))
        return foundation::Result<ProcessDefinitionId>::Failure(
            Error("gameplay.processes.invalid_definition", "invalid or duplicate process definition"));

    std::unordered_set<ProcessStepId, IdHash> step_ids;
    for (const auto &step : definition.steps)
    {
        if (!step.id.IsValid() || !step.step_type.IsValid() || step.duration.ticks < 0 || !step_ids.insert(step.id).second)
            return foundation::Result<ProcessDefinitionId>::Failure(
                Error("gameplay.processes.invalid_definition", "process definition has an invalid or duplicate step"));
    }
    if (definition.timing == ProcessTimingPolicy::Instant &&
        std::any_of(definition.steps.begin(), definition.steps.end(), [](const auto &step) { return step.duration.ticks > 0; }))
        return foundation::Result<ProcessDefinitionId>::Failure(
            Error("gameplay.processes.invalid_definition", "instant process cannot contain timed steps"));

    Bump();
    definition.revision = revision_;
    const auto id = definition.id;
    definitions_.emplace(id, std::move(definition));
    return foundation::Result<ProcessDefinitionId>::Success(id);
}

foundation::Result<ProcessRecipeId> ProcessesService::RegisterRecipe(ProcessRecipe recipe)
{
    if (frozen_)
        return foundation::Result<ProcessRecipeId>::Failure(
            Error("gameplay.processes.registry_frozen", "process registry is frozen"));
    if (recipe.canonical_name.empty() || !definitions_.contains(recipe.process))
        return foundation::Result<ProcessRecipeId>::Failure(
            Error("gameplay.processes.invalid_recipe", "recipe name or process reference is invalid"));
    const auto canonical = ProcessRecipeId::FromString(recipe.canonical_name);
    if (!recipe.id.IsValid())
        recipe.id = canonical;
    if (recipe.id != canonical || recipes_.contains(recipe.id))
        return foundation::Result<ProcessRecipeId>::Failure(
            Error("gameplay.processes.invalid_recipe", "invalid or duplicate process recipe"));

    std::unordered_set<ProcessInputId, IdHash> input_ids;
    for (const auto &input : recipe.inputs)
    {
        if (!input.id.IsValid() || !input.type.IsValid() || input.amount < 0 || !input_ids.insert(input.id).second)
            return foundation::Result<ProcessRecipeId>::Failure(
                Error("gameplay.processes.invalid_input", "recipe has invalid or duplicate input"));
    }
    std::unordered_set<ProcessOutputId, IdHash> output_ids;
    for (const auto &output : recipe.outputs)
    {
        if (!output.id.IsValid() || !output.type.IsValid() || output.amount < 0 || !output_ids.insert(output.id).second)
            return foundation::Result<ProcessRecipeId>::Failure(
                Error("gameplay.processes.invalid_output", "recipe has invalid or duplicate output"));
    }
    Bump();
    recipe.revision = revision_;
    const auto id = recipe.id;
    recipes_.emplace(id, std::move(recipe));
    return foundation::Result<ProcessRecipeId>::Success(id);
}

foundation::Result<ProcessStationId> ProcessesService::RegisterStation(ProcessStation station)
{
    if (!station.station_object.IsValid() || station.efficiency_micro <= 0)
        return foundation::Result<ProcessStationId>::Failure(
            Error("gameplay.processes.invalid_station", "station object and efficiency are required"));
    if (!station.id.IsValid())
    {
        const auto next = station_ids_.Next();
        if (!next.IsValid())
            return foundation::Result<ProcessStationId>::Failure(
                Error("gameplay.processes.id_exhausted", "process station id generator is exhausted"));
        station.id = ProcessStationId{next};
    }
    if (stations_.contains(station.id) || station_by_object_.contains(station.station_object))
        return foundation::Result<ProcessStationId>::Failure(
            Error("gameplay.processes.invalid_station", "duplicate process station"));
    AdvanceGeneratorPastAcceptedId(station_ids_, station.id);
    Bump();
    station.revision = revision_;
    const auto id = station.id;
    const auto station_object = station.station_object;
    station_by_object_.emplace(station_object, id);
    stations_.emplace(id, std::move(station));
    diagnostics_.stations = stations_.size();
    ProcessChange change{};
    change.kind = ProcessChangeKind::StationRegistered;
    change.actor = station_object;
    change.station = id;
    change.revision = revision_;
    Record(std::move(change));
    return foundation::Result<ProcessStationId>::Success(id);
}

const ProcessDefinition *ProcessesService::FindDefinition(ProcessDefinitionId id) const noexcept
{
    const auto it = definitions_.find(id);
    return it == definitions_.end() ? nullptr : &it->second;
}
const ProcessRecipe *ProcessesService::FindRecipe(ProcessRecipeId id) const noexcept
{
    const auto it = recipes_.find(id);
    return it == recipes_.end() ? nullptr : &it->second;
}
const ProcessStation *ProcessesService::FindStationByObject(GameplayObjectRef station) const noexcept
{
    const auto it = station_by_object_.find(station);
    if (it == station_by_object_.end())
        return nullptr;
    const auto sit = stations_.find(it->second);
    return sit == stations_.end() ? nullptr : &sit->second;
}
const ProcessInstance *ProcessesService::FindInstance(ProcessInstanceId id) const noexcept
{
    const auto it = instances_.find(id);
    return it == instances_.end() ? nullptr : &it->second;
}

Fixed ProcessesService::EvaluateProgress(ProcessInstanceId id, GameplayTimePoint now) const noexcept
{
    const auto *instance = FindInstance(id);
    if (!instance)
        return 0;
    if (instance->state == ProcessInstanceState::Completed)
        return 1'000'000;
    if (instance->total_duration.ticks <= 0)
        return 0;

    const auto raw_remaining = instance->state == ProcessInstanceState::Paused
                                   ? instance->paused_remaining.ticks
                                   : SaturatingDifference(instance->due_at, now).ticks;
    const auto remaining = std::clamp<std::int64_t>(raw_remaining, 0, instance->total_duration.ticks);
    const auto elapsed = instance->total_duration.ticks - remaining;
    const long double ratio = static_cast<long double>(elapsed) / static_cast<long double>(instance->total_duration.ticks);
    return static_cast<Fixed>(std::clamp<long double>(ratio * 1'000'000.0L, 0.0L, 1'000'000.0L));
}

GameplayDuration ProcessesService::DurationFor(const ProcessRecipe &, const ProcessDefinition &definition,
                                               const ProcessStation *station) const noexcept
{
    GameplayDuration duration{};
    for (const auto &step : definition.steps)
    {
        if (step.duration.ticks > 0 && duration.ticks > std::numeric_limits<std::int64_t>::max() - step.duration.ticks)
        {
            duration.ticks = std::numeric_limits<std::int64_t>::max();
            break;
        }
        duration.ticks += std::max<std::int64_t>(0, step.duration.ticks);
    }
    if (definition.timing == ProcessTimingPolicy::Instant)
        return {};
    if (definition.timing == ProcessTimingPolicy::ExternalCompletion)
        return {};
    if (duration.ticks == 0)
        duration.ticks = 1;
    if (station)
    {
        const auto e = std::max<Fixed>(1, station->efficiency_micro);
        const auto quotient = duration.ticks / e;
        const auto remainder = duration.ticks % e;
        if (quotient > std::numeric_limits<std::int64_t>::max() / 1'000'000)
            duration.ticks = std::numeric_limits<std::int64_t>::max();
        else
        {
            const auto base = quotient * 1'000'000;
            const auto extra = remainder > 0 && remainder > std::numeric_limits<std::int64_t>::max() / 1'000'000
                                   ? std::numeric_limits<std::int64_t>::max()
                                   : (remainder * 1'000'000) / e;
            duration.ticks = extra == std::numeric_limits<std::int64_t>::max() ||
                                     base > std::numeric_limits<std::int64_t>::max() - extra
                                 ? std::numeric_limits<std::int64_t>::max()
                                 : std::max<std::int64_t>(1, base + extra);
        }
    }
    return duration;
}

foundation::Result<void> ProcessesService::ValidateProviderInput(const ProcessInputDefinition &input,
                                                                 const StartProcessRequest &request,
                                                                 ProcessInstanceId instance) const
{
    if (!input_provider_)
        return foundation::Result<void>::Failure(
            Error("gameplay.processes.input_provider_missing", "process has inputs but no input provider"));
    try
    {
        return input_provider_->Validate(input, request, instance);
    }
    catch (const std::exception &)
    {
        return foundation::Result<void>::Failure(CallbackError("input validation callback threw"));
    }
    catch (...)
    {
        return foundation::Result<void>::Failure(CallbackError("input validation callback threw unknown exception"));
    }
}

foundation::Result<void> ProcessesService::ReserveInputs(ProcessInstance &instance, const ProcessRecipe &recipe,
                                                         const StartProcessRequest &request)
{
    if (recipe.inputs.empty())
        return foundation::Result<void>::Success();

    std::size_t reservation_count = 0;
    for (const auto &input : recipe.inputs)
        if (NeedsReservation(input.consumption))
            ++reservation_count;
    // All vector allocation is completed before the first external Reserve(), so a successful
    // provider token can be staged without any subsequent allocation window.
    instance.reserved_inputs.reserve(instance.reserved_inputs.size() + reservation_count);

    for (const auto &input : recipe.inputs)
    {
        auto validated = ValidateProviderInput(input, request, instance.id);
        if (!validated)
            return validated;
        if (!NeedsReservation(input.consumption))
            continue;

        // Prepare the Framework-owned identity before the external call. Work on a staged copy so
        // a provider failure does not consume a sequence value, while exhaustion is still detected
        // before external state can be created.
        auto staged_ids = reservation_ids_;
        const auto generated = staged_ids.Next();
        if (!generated.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.processes.id_exhausted", "process reservation id generator is exhausted"));
        const ProcessReservationId framework_reservation_id{generated};

        try
        {
            auto reserved = input_provider_->Reserve(input, request, instance.id);
            if (!reserved)
            {
                auto released = ReleaseInputs(instance, request.context);
                if (!released)
                {
                    instance.state = ProcessInstanceState::ReconciliationRequired;
                    return foundation::Result<void>::Failure(
                        Error("gameplay.processes.reconciliation_required", "input reservation rollback failed"));
                }
                return foundation::Result<void>::Failure(reserved.GetError());
            }

            auto value = std::move(reserved).Value();
            // Provider-returned id is intentionally not authoritative. The provider's stable
            // identity belongs in provider_token; the Framework id is globally canonical here.
            value.id = framework_reservation_id;
            value.input = input.id;
            value.type = input.type;
            value.amount = input.amount;
            value.consumption = input.consumption;
            value.state = ProcessInputCommitState::Reserved;

            instance.reserved_inputs.push_back(std::move(value));
            reservation_ids_.Restore(staged_ids.GetSnapshot());
            auto &stored = instance.reserved_inputs.back();

            const auto *definition = FindDefinition(recipe.process);
            if (definition && definition->persistence == ProcessPersistencePolicy::Persistent &&
                !stored.provider_token.IsPortable())
            {
                foundation::Result<void> released = foundation::Result<void>::Failure(
                    Error("gameplay.processes.reconciliation_required", "input token compensation was not attempted"));
                try
                {
                    released = input_provider_->Release(stored, request.context);
                }
                catch (const std::exception &)
                {
                    instance.state = ProcessInstanceState::ReconciliationRequired;
                    return foundation::Result<void>::Failure(CallbackError("input release callback threw"));
                }
                catch (...)
                {
                    instance.state = ProcessInstanceState::ReconciliationRequired;
                    return foundation::Result<void>::Failure(CallbackError("input release callback threw unknown exception"));
                }
                if (!released)
                {
                    instance.state = ProcessInstanceState::ReconciliationRequired;
                    return foundation::Result<void>::Failure(released.GetError());
                }
                stored.state = ProcessInputCommitState::Released;
                return foundation::Result<void>::Failure(
                    Error("gameplay.processes.nonportable_token", "persistent process input token must use versioned encoding"));
            }

            Record({0, ProcessChangeKind::InputReserved, instance.id, instance.recipe, instance.actor, request.now,
                    request.context, revision_});
        }
        catch (const std::exception &)
        {
            auto released = ReleaseInputs(instance, request.context);
            if (!released)
                instance.state = ProcessInstanceState::ReconciliationRequired;
            return foundation::Result<void>::Failure(CallbackError("input reserve callback threw"));
        }
        catch (...)
        {
            auto released = ReleaseInputs(instance, request.context);
            if (!released)
                instance.state = ProcessInstanceState::ReconciliationRequired;
            return foundation::Result<void>::Failure(CallbackError("input reserve callback threw unknown exception"));
        }
    }
    diagnostics_.reservations += instance.reserved_inputs.size();
    return foundation::Result<void>::Success();
}

foundation::Result<void> ProcessesService::ConsumeInputs(ProcessInstance &instance, InputConsumptionPolicy phase,
                                                         GameplayContext context)
{
    if (!input_provider_)
        return instance.reserved_inputs.empty()
                   ? foundation::Result<void>::Success()
                   : foundation::Result<void>::Failure(
                         Error("gameplay.processes.input_provider_missing", "input provider missing"));
    for (auto &reservation : instance.reserved_inputs)
    {
        if (reservation.state != ProcessInputCommitState::Reserved || !ConsumesAt(reservation.consumption, phase))
            continue;
        try
        {
            auto consumed = input_provider_->Consume(reservation, context);
            if (!consumed)
            {
                instance.state = ProcessInstanceState::ReconciliationRequired;
                return consumed;
            }
        }
        catch (const std::exception &)
        {
            instance.state = ProcessInstanceState::ReconciliationRequired;
            return foundation::Result<void>::Failure(CallbackError("input consume callback threw"));
        }
        catch (...)
        {
            instance.state = ProcessInstanceState::ReconciliationRequired;
            return foundation::Result<void>::Failure(CallbackError("input consume callback threw unknown exception"));
        }
        Bump();
        reservation.state = ProcessInputCommitState::Consumed;
        instance.revision = revision_;
        Record({0, ProcessChangeKind::InputConsumed, instance.id, instance.recipe, instance.actor, context.time,
                context, revision_});
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> ProcessesService::ReleaseInputs(ProcessInstance &instance, GameplayContext context)
{
    if (!input_provider_)
        return instance.reserved_inputs.empty()
                   ? foundation::Result<void>::Success()
                   : foundation::Result<void>::Failure(
                         Error("gameplay.processes.input_provider_missing", "input provider missing"));
    for (auto &reservation : instance.reserved_inputs)
    {
        if (reservation.state != ProcessInputCommitState::Reserved)
            continue;
        try
        {
            auto released = input_provider_->Release(reservation, context);
            if (!released)
            {
                instance.state = ProcessInstanceState::ReconciliationRequired;
                return released;
            }
        }
        catch (const std::exception &)
        {
            instance.state = ProcessInstanceState::ReconciliationRequired;
            return foundation::Result<void>::Failure(CallbackError("input release callback threw"));
        }
        catch (...)
        {
            instance.state = ProcessInstanceState::ReconciliationRequired;
            return foundation::Result<void>::Failure(CallbackError("input release callback threw unknown exception"));
        }
        reservation.state = ProcessInputCommitState::Released;
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> ProcessesService::PrepareOutputs(ProcessInstance &instance, const ProcessRecipe &recipe,
                                                          OutputDeliveryPolicy phase, GameplayContext context)
{
    std::size_t phase_outputs = 0;
    for (const auto &output : recipe.outputs)
        if (output.delivery == phase)
            ++phase_outputs;
    // Prepare all local storage before invoking an external handler. Once Prepare succeeds the
    // returned provider token can therefore be staged immediately with no allocation gap.
    instance.prepared_outputs.reserve(instance.prepared_outputs.size() + phase_outputs);

    for (const auto &output : recipe.outputs)
    {
        if (output.delivery != phase)
            continue;
        const auto existing = std::find_if(instance.prepared_outputs.begin(), instance.prepared_outputs.end(),
                                           [&](const auto &prepared) { return prepared.output == output.id; });
        if (existing != instance.prepared_outputs.end())
            continue;
        const auto it = std::find_if(output_handlers_.begin(), output_handlers_.end(),
                                     [&](const auto *handler) { return handler && handler->Supports(output.type); });
        if (it == output_handlers_.end())
            return foundation::Result<void>::Failure(
                Error("gameplay.processes.output_handler_missing", "process output has no handler"));
        try
        {
            auto prepared = (*it)->Prepare(output, instance, context);
            if (!prepared)
            {
                auto cancelled = CancelPreparedOutputs(instance, context);
                if (!cancelled)
                    instance.state = ProcessInstanceState::ReconciliationRequired;
                return foundation::Result<void>::Failure(prepared.GetError());
            }

            auto token = std::move(prepared).Value();
            token.output = output.id;
            token.type = output.type;
            token.delivery = output.delivery;
            token.state = ProcessOutputCommitState::Prepared;
            instance.prepared_outputs.push_back(std::move(token));
            auto &stored = instance.prepared_outputs.back();

            const auto *definition = FindDefinition(recipe.process);
            if (definition && definition->persistence == ProcessPersistencePolicy::Persistent &&
                !stored.provider_token.IsPortable())
            {
                auto cancelled = CancelPreparedOutputs(instance, context);
                if (!cancelled)
                {
                    instance.state = ProcessInstanceState::ReconciliationRequired;
                    return foundation::Result<void>::Failure(cancelled.GetError());
                }
                return foundation::Result<void>::Failure(
                    Error("gameplay.processes.nonportable_token", "persistent process output token must use versioned encoding"));
            }
        }
        catch (const std::exception &)
        {
            auto cancelled = CancelPreparedOutputs(instance, context);
            if (!cancelled)
                instance.state = ProcessInstanceState::ReconciliationRequired;
            return foundation::Result<void>::Failure(CallbackError("output prepare callback threw"));
        }
        catch (...)
        {
            auto cancelled = CancelPreparedOutputs(instance, context);
            if (!cancelled)
                instance.state = ProcessInstanceState::ReconciliationRequired;
            return foundation::Result<void>::Failure(CallbackError("output prepare callback threw unknown exception"));
        }
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> ProcessesService::CommitOutputs(ProcessInstance &instance, OutputDeliveryPolicy phase,
                                                         GameplayContext context)
{
    for (auto &prepared : instance.prepared_outputs)
    {
        if (prepared.delivery != phase || prepared.state != ProcessOutputCommitState::Prepared)
            continue;
        const auto it = std::find_if(output_handlers_.begin(), output_handlers_.end(),
                                     [&](const auto *handler) { return handler && handler->Supports(prepared.type); });
        if (it == output_handlers_.end())
        {
            instance.state = ProcessInstanceState::ReconciliationRequired;
            return foundation::Result<void>::Failure(
                Error("gameplay.processes.output_handler_missing", "prepared process output handler is missing"));
        }
        try
        {
            auto committed = (*it)->Commit(prepared, instance, context);
            if (!committed)
            {
                instance.state = ProcessInstanceState::ReconciliationRequired;
                return committed;
            }
        }
        catch (const std::exception &)
        {
            instance.state = ProcessInstanceState::ReconciliationRequired;
            return foundation::Result<void>::Failure(CallbackError("output commit callback threw"));
        }
        catch (...)
        {
            instance.state = ProcessInstanceState::ReconciliationRequired;
            return foundation::Result<void>::Failure(CallbackError("output commit callback threw unknown exception"));
        }
        Bump();
        prepared.state = ProcessOutputCommitState::Committed;
        instance.revision = revision_;
        ++diagnostics_.outputs;
        Record({0, ProcessChangeKind::OutputProduced, instance.id, instance.recipe, instance.actor, context.time,
                context, revision_});
    }
    return foundation::Result<void>::Success();
}

foundation::Result<void> ProcessesService::CancelPreparedOutputs(ProcessInstance &instance, GameplayContext context)
{
    for (auto &prepared : instance.prepared_outputs)
    {
        if (prepared.state != ProcessOutputCommitState::Prepared)
            continue;
        const auto it = std::find_if(output_handlers_.begin(), output_handlers_.end(),
                                     [&](const auto *handler) { return handler && handler->Supports(prepared.type); });
        if (it == output_handlers_.end())
            return foundation::Result<void>::Failure(
                Error("gameplay.processes.output_handler_missing", "prepared process output handler is missing"));
        try
        {
            auto cancelled = (*it)->Cancel(prepared, instance, context);
            if (!cancelled)
                return cancelled;
        }
        catch (const std::exception &)
        {
            return foundation::Result<void>::Failure(CallbackError("output cancel callback threw"));
        }
        catch (...)
        {
            return foundation::Result<void>::Failure(CallbackError("output cancel callback threw unknown exception"));
        }
        prepared.state = ProcessOutputCommitState::Cancelled;
    }
    return foundation::Result<void>::Success();
}

foundation::Result<ProcessInstanceId> ProcessesService::StartProcess(StartProcessRequest request)
{
    if (!frozen_)
        return foundation::Result<ProcessInstanceId>::Failure(
            Error("gameplay.processes.registry_not_frozen", "process definitions must be frozen before execution"));
    const auto *recipe = FindRecipe(request.recipe);
    if (!recipe)
        return foundation::Result<ProcessInstanceId>::Failure(
            Error("gameplay.processes.recipe_missing", "process recipe is missing"));
    const auto *definition = FindDefinition(recipe->process);
    if (!definition)
        return foundation::Result<ProcessInstanceId>::Failure(
            Error("gameplay.processes.definition_missing", "process definition is missing"));
    const auto *station = request.station.IsValid() ? FindStationByObject(request.station) : nullptr;
    if (request.station.IsValid() && (!station || station->state != StationState::Active))
        return foundation::Result<ProcessInstanceId>::Failure(
            Error("gameplay.processes.station_unavailable", "process station is unavailable"));
    if (!recipe->station_capabilities.Values().empty() &&
        (!station || !HasAllExact(station->capabilities, recipe->station_capabilities)))
        return foundation::Result<ProcessInstanceId>::Failure(
            Error("gameplay.processes.station_capability_missing", "process station lacks required capability"));

    const auto generated = instance_ids_.Next();
    if (!generated.IsValid())
        return foundation::Result<ProcessInstanceId>::Failure(
            Error("gameplay.processes.id_exhausted", "process instance id generator is exhausted"));
    ProcessInstance staged;
    staged.id = ProcessInstanceId{generated};
    staged.recipe = recipe->id;
    staged.actor = request.actor;
    staged.station = request.station;
    staged.target = request.target;
    staged.simulation_area = request.simulation_area;
    staged.state = ProcessInstanceState::Prepared;
    staged.started_at = request.now;
    staged.last_updated_at = request.now;
    try
    {
        staged.quality = quality_provider_
                             ? quality_provider_->Resolve(*recipe, request)
                             : ProcessQualityResult{ProcessQualityId::FromString("framework.quality.normal"), 1'000'000, {}};
    }
    catch (...)
    {
        return foundation::Result<ProcessInstanceId>::Failure(CallbackError("quality provider threw"));
    }
    const auto duration = DurationFor(*recipe, *definition, station);
    staged.total_duration = duration;
    staged.paused_remaining = duration;
    staged.due_at = definition->timing == ProcessTimingPolicy::ExternalCompletion
                        ? GameplayTimePoint{}
                        : SaturatingAdd(request.now, duration);

    Bump();
    staged.revision = revision_;
    const auto id = staged.id;
    auto [instance_it, inserted] = instances_.emplace(id, std::move(staged));
    if (!inserted)
        return foundation::Result<ProcessInstanceId>::Failure(
            Error("gameplay.processes.duplicate_instance", "process instance id already exists"));
    auto &instance = instance_it->second;

    const auto has_unresolved_legs = [&]() noexcept {
        return std::any_of(instance.reserved_inputs.begin(), instance.reserved_inputs.end(),
                           [](const auto &leg) { return leg.state == ProcessInputCommitState::Reserved; }) ||
               std::any_of(instance.prepared_outputs.begin(), instance.prepared_outputs.end(),
                           [](const auto &leg) { return leg.state == ProcessOutputCommitState::Prepared; });
    };
    const auto mark_reconciliation = [&]() {
        Bump();
        instance.state = ProcessInstanceState::ReconciliationRequired;
        instance.last_updated_at = request.now;
        instance.revision = revision_;
        Record({0, ProcessChangeKind::ReconciliationRequired, id, recipe->id, request.actor, request.now,
                request.context, revision_});
        return foundation::Result<ProcessInstanceId>::Success(id);
    };

    auto reserve = ReserveInputs(instance, *recipe, request);
    if (!reserve)
    {
        if (instance.state == ProcessInstanceState::ReconciliationRequired || has_unresolved_legs())
            return mark_reconciliation();
        instances_.erase(id);
        return foundation::Result<ProcessInstanceId>::Failure(reserve.GetError());
    }

    auto prepared_immediate = PrepareOutputs(instance, *recipe, OutputDeliveryPolicy::Immediate, request.context);
    if (!prepared_immediate)
    {
        auto released = ReleaseInputs(instance, request.context);
        if (!released || instance.state == ProcessInstanceState::ReconciliationRequired || has_unresolved_legs())
            return mark_reconciliation();
        instances_.erase(id);
        return foundation::Result<ProcessInstanceId>::Failure(prepared_immediate.GetError());
    }

    auto start_consumed = ConsumeInputs(instance, InputConsumptionPolicy::ConsumeOnStart, request.context);
    if (!start_consumed)
        return mark_reconciliation();

    auto immediate = CommitOutputs(instance, OutputDeliveryPolicy::Immediate, request.context);
    if (!immediate)
        return mark_reconciliation();

    Bump();
    instance.state = ProcessInstanceState::Running;
    instance.revision = revision_;
    Record({0, ProcessChangeKind::Started, id, recipe->id, request.actor, request.now, request.context, revision_});
    if (definition->timing == ProcessTimingPolicy::Instant)
    {
        auto completed = Complete(id, request.now, request.context);
        if (!completed)
        {
            auto current = instances_.find(id);
            if (current != instances_.end() && !IsTerminal(current->second.state))
            {
                if (current->second.state != ProcessInstanceState::ReconciliationRequired)
                {
                    Bump();
                    current->second.state = ProcessInstanceState::ReconciliationRequired;
                    current->second.last_updated_at = request.now;
                    current->second.revision = revision_;
                    Record({0, ProcessChangeKind::ReconciliationRequired, id, recipe->id, request.actor, request.now,
                            request.context, revision_});
                }
                return foundation::Result<ProcessInstanceId>::Success(id);
            }
            return foundation::Result<ProcessInstanceId>::Failure(completed.GetError());
        }
    }
    return foundation::Result<ProcessInstanceId>::Success(id);
}

foundation::Result<void> ProcessesService::Pause(ProcessInstanceId id, GameplayTimePoint now, GameplayContext context)
{
    auto it = instances_.find(id);
    if (it == instances_.end())
        return foundation::Result<void>::Failure(Error("gameplay.processes.instance_missing", "process instance is missing"));
    if (it->second.state != ProcessInstanceState::Running)
        return foundation::Result<void>::Failure(Error("gameplay.processes.invalid_state", "process is not running"));
    const auto *recipe = FindRecipe(it->second.recipe);
    const auto *definition = recipe ? FindDefinition(recipe->process) : nullptr;
    if (!definition || definition->timing != ProcessTimingPolicy::Timed)
        return foundation::Result<void>::Failure(Error("gameplay.processes.invalid_state", "only timed process can be paused"));
    const auto remaining = SaturatingDifference(it->second.due_at, now).ticks;
    it->second.paused_remaining = GameplayDuration{std::max<std::int64_t>(0, remaining)};
    Bump();
    it->second.state = ProcessInstanceState::Paused;
    it->second.last_updated_at = now;
    it->second.revision = revision_;
    Record({0, ProcessChangeKind::Paused, id, it->second.recipe, it->second.actor, now, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ProcessesService::Resume(ProcessInstanceId id, GameplayTimePoint now, GameplayContext context)
{
    auto it = instances_.find(id);
    if (it == instances_.end())
        return foundation::Result<void>::Failure(Error("gameplay.processes.instance_missing", "process instance is missing"));
    if (it->second.state != ProcessInstanceState::Paused)
        return foundation::Result<void>::Failure(Error("gameplay.processes.invalid_state", "process is not paused"));
    Bump();
    it->second.state = ProcessInstanceState::Running;
    it->second.due_at = SaturatingAdd(now, it->second.paused_remaining);
    it->second.last_updated_at = now;
    it->second.revision = revision_;
    Record({0, ProcessChangeKind::Resumed, id, it->second.recipe, it->second.actor, now, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ProcessesService::Cancel(ProcessInstanceId id, GameplayTimePoint now, GameplayContext context)
{
    auto it = instances_.find(id);
    if (it == instances_.end())
        return foundation::Result<void>::Failure(Error("gameplay.processes.instance_missing", "process instance is missing"));
    if (it->second.state == ProcessInstanceState::Cancelled)
        return foundation::Result<void>::Success();
    if (it->second.state == ProcessInstanceState::Completed)
        return foundation::Result<void>::Failure(Error("gameplay.processes.invalid_state", "completed process cannot be cancelled"));
    auto cancelled_outputs = CancelPreparedOutputs(it->second, context);
    if (!cancelled_outputs)
    {
        it->second.state = ProcessInstanceState::ReconciliationRequired;
        return cancelled_outputs;
    }
    auto released = ReleaseInputs(it->second, context);
    if (!released)
    {
        it->second.state = ProcessInstanceState::ReconciliationRequired;
        return released;
    }
    Bump();
    it->second.state = ProcessInstanceState::Cancelled;
    it->second.last_updated_at = now;
    it->second.revision = revision_;
    Record({0, ProcessChangeKind::Cancelled, id, it->second.recipe, it->second.actor, now, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<void> ProcessesService::Complete(ProcessInstanceId id, GameplayTimePoint now, GameplayContext context)
{
    auto it = instances_.find(id);
    if (it == instances_.end())
        return foundation::Result<void>::Failure(Error("gameplay.processes.instance_missing", "process instance is missing"));
    auto &instance = it->second;
    if (instance.state == ProcessInstanceState::Completed)
        return foundation::Result<void>::Success();
    if (instance.state != ProcessInstanceState::Running && instance.state != ProcessInstanceState::Reserved &&
        instance.state != ProcessInstanceState::ReconciliationRequired)
        return foundation::Result<void>::Failure(Error("gameplay.processes.invalid_state", "process is not completable"));
    const auto *recipe = FindRecipe(instance.recipe);
    const auto *definition = recipe ? FindDefinition(recipe->process) : nullptr;
    if (!recipe || !definition)
        return foundation::Result<void>::Failure(Error("gameplay.processes.recipe_missing", "process recipe is missing"));
    if (definition->timing == ProcessTimingPolicy::Timed && now.ticks < instance.due_at.ticks)
        return foundation::Result<void>::Failure(Error("gameplay.processes.not_due", "process is not due"));

    auto prepared = PrepareOutputs(instance, *recipe, OutputDeliveryPolicy::OnCompletion, context);
    if (!prepared)
        return prepared;
    auto consumed = ConsumeInputs(instance, InputConsumptionPolicy::ConsumeOnCompletion, context);
    if (!consumed)
    {
        Record({0, ProcessChangeKind::ReconciliationRequired, id, instance.recipe, instance.actor, now, context,
                revision_});
        return consumed;
    }
    auto produced = CommitOutputs(instance, OutputDeliveryPolicy::OnCompletion, context);
    if (!produced)
    {
        Record({0, ProcessChangeKind::ReconciliationRequired, id, instance.recipe, instance.actor, now, context,
                revision_});
        return produced;
    }
    // Catalysts/tools are held for process lifetime and released only after every consuming/output leg committed.
    auto released = ReleaseInputs(instance, context);
    if (!released)
    {
        instance.state = ProcessInstanceState::ReconciliationRequired;
        Record({0, ProcessChangeKind::ReconciliationRequired, id, instance.recipe, instance.actor, now, context,
                revision_});
        return released;
    }
    Bump();
    instance.state = ProcessInstanceState::Completed;
    instance.last_updated_at = now;
    instance.revision = revision_;
    ++diagnostics_.completed_processes;
    Record({0, ProcessChangeKind::Completed, id, instance.recipe, instance.actor, now, context, revision_});
    return foundation::Result<void>::Success();
}

foundation::Result<std::vector<ProcessInstanceId>> ProcessesService::CompleteDue(GameplayTimePoint now)
{
    std::vector<ProcessInstanceId> due;
    for (const auto &[id, instance] : instances_)
    {
        const auto *recipe = FindRecipe(instance.recipe);
        const auto *definition = recipe ? FindDefinition(recipe->process) : nullptr;
        if (instance.state == ProcessInstanceState::Running && definition &&
            definition->timing == ProcessTimingPolicy::Timed && instance.due_at.ticks <= now.ticks)
            due.push_back(id);
    }
    std::sort(due.begin(), due.end());
    for (auto id : due)
    {
        GameplayContext context;
        context.time = now;
        auto completed = Complete(id, now, context);
        if (!completed)
            return foundation::Result<std::vector<ProcessInstanceId>>::Failure(completed.GetError());
    }
    return foundation::Result<std::vector<ProcessInstanceId>>::Success(std::move(due));
}

std::vector<ProcessInstance> ProcessesService::FindDueProcessesForSimulation(GameplayObjectRef simulation_area,
                                                                              GameplayTimePoint from,
                                                                              GameplayTimePoint to) const
{
    std::vector<ProcessInstance> out;
    if (!simulation_area.IsValid() || to.ticks <= from.ticks)
        return out;
    for (const auto &[id, instance] : instances_)
    {
        (void)id;
        if (instance.state == ProcessInstanceState::Running && instance.simulation_area == simulation_area &&
            instance.due_at.ticks > from.ticks && instance.due_at.ticks <= to.ticks)
            out.push_back(instance);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

foundation::Result<std::vector<ProcessInstanceId>> ProcessesService::CompletePreparedDueForSimulation(
    GameplayObjectRef simulation_area, std::span<const ProcessInstanceId> process_ids, GameplayTimePoint now,
    GameplayContext context)
{
    if (!simulation_area.IsValid())
        return foundation::Result<std::vector<ProcessInstanceId>>::Failure(
            Error("gameplay.processes.simulation_area_missing", "simulation area is required"));
    std::vector<ProcessInstanceId> unique(process_ids.begin(), process_ids.end());
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());

    for (auto id : unique)
    {
        const auto it = instances_.find(id);
        if (it == instances_.end())
            return foundation::Result<std::vector<ProcessInstanceId>>::Failure(
                Error("gameplay.processes.instance_missing", "prepared process instance is missing"));
        const auto &instance = it->second;
        if (instance.simulation_area != simulation_area)
            return foundation::Result<std::vector<ProcessInstanceId>>::Failure(
                Error("gameplay.processes.simulation_area_mismatch", "prepared process belongs to another simulation area"));
        if (instance.state != ProcessInstanceState::Running && instance.state != ProcessInstanceState::Completed)
            return foundation::Result<std::vector<ProcessInstanceId>>::Failure(
                Error("gameplay.processes.invalid_state", "prepared process is not due-completable"));
        if (instance.state == ProcessInstanceState::Running && instance.due_at.ticks > now.ticks)
            return foundation::Result<std::vector<ProcessInstanceId>>::Failure(
                Error("gameplay.processes.not_due", "prepared process is not due"));
    }

    std::vector<ProcessInstanceId> completed_ids;
    completed_ids.reserve(unique.size());
    for (auto id : unique)
    {
        auto it = instances_.find(id);
        if (it != instances_.end() && it->second.state == ProcessInstanceState::Completed)
        {
            completed_ids.push_back(id);
            continue;
        }
        context.time = now;
        auto completed = Complete(id, now, context);
        if (!completed)
            return foundation::Result<std::vector<ProcessInstanceId>>::Failure(completed.GetError());
        completed_ids.push_back(id);
    }
    return foundation::Result<std::vector<ProcessInstanceId>>::Success(std::move(completed_ids));
}
std::vector<ProcessInstance> ProcessesService::FindProcessesByActor(GameplayObjectRef actor) const
{
    std::vector<ProcessInstance> out;
    for (const auto &[id, instance] : instances_)
    {
        (void)id;
        if (instance.actor == actor)
            out.push_back(instance);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}
std::vector<ProcessInstance> ProcessesService::FindProcessesByStation(GameplayObjectRef station) const
{
    std::vector<ProcessInstance> out;
    for (const auto &[id, instance] : instances_)
    {
        (void)id;
        if (instance.station == station)
            out.push_back(instance);
    }
    std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    return out;
}

std::vector<ProcessChange> ProcessesService::ChangesSinceSequence(std::uint64_t sequence) const
{
    return ReadChangesSinceSequence(sequence).changes;
}

ProcessChangeBatch ProcessesService::ReadChangesSinceSequence(std::uint64_t sequence) const
{
    ProcessChangeBatch batch;
    const auto latest = next_change_sequence_ == 0 ? std::numeric_limits<std::uint64_t>::max()
                                                    : next_change_sequence_ - 1;
    batch.latest_sequence = latest;
    batch.oldest_available_sequence = changes_.empty() ? next_change_sequence_ : changes_.front().sequence;

    if (changes_.empty())
    {
        // A restored snapshot intentionally does not carry the journal body. Any consumer behind
        // the captured sequence must rebuild from the authoritative snapshot instead of silently
        // assuming that no changes occurred.
        if (sequence < latest)
            batch.snapshot_required = true;
        return batch;
    }
    if (sequence < changes_.front().sequence - 1)
    {
        batch.snapshot_required = true;
        return batch;
    }
    for (const auto &change : changes_)
        if (change.sequence > sequence)
            batch.changes.push_back(change);
    return batch;
}

foundation::Result<void> ProcessesService::PruneTerminalProcesses(std::size_t keep_recent)
{
    std::vector<ProcessInstanceId> terminal;
    for (const auto &[id, instance] : instances_)
        if (IsTerminal(instance.state))
            terminal.push_back(id);
    std::sort(terminal.begin(), terminal.end());
    if (terminal.size() <= keep_recent)
        return foundation::Result<void>::Success();
    const auto remove_count = terminal.size() - keep_recent;
    for (std::size_t i = 0; i < remove_count; ++i)
        instances_.erase(terminal[i]);
    return foundation::Result<void>::Success();
}

ProcessesSnapshot ProcessesService::CaptureSnapshot() const
{
    ProcessesSnapshot snapshot;
    for (const auto &[id, station] : stations_)
    {
        (void)id;
        snapshot.stations.push_back(station);
    }
    for (const auto &[id, instance] : instances_)
    {
        (void)id;
        const auto *recipe = FindRecipe(instance.recipe);
        const auto *definition = recipe ? FindDefinition(recipe->process) : nullptr;
        if (!definition)
            continue;
        if (definition->persistence == ProcessPersistencePolicy::Persistent || !IsTerminal(instance.state))
            snapshot.instances.push_back(instance);
    }
    std::sort(snapshot.stations.begin(), snapshot.stations.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.instances.begin(), snapshot.instances.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
    snapshot.station_ids = station_ids_.GetSnapshot();
    snapshot.instance_ids = instance_ids_.GetSnapshot();
    snapshot.reservation_ids = reservation_ids_.GetSnapshot();
    snapshot.next_change_sequence = next_change_sequence_;
    snapshot.revision = revision_;
    snapshot.change_epoch = journal_epoch_;
    return snapshot;
}

foundation::Result<void> ProcessesService::RestoreSnapshot(ProcessesSnapshot snapshot)
{
    const auto next_journal_epoch = CheckedNextChangeEpoch(snapshot.change_epoch > journal_epoch_ ? snapshot.change_epoch : journal_epoch_);
    if (!next_journal_epoch)
    {
        return foundation::Result<void>::Failure(
            foundation::Error::Create("gameplay.change_journal.epoch_exhausted", "change journal epoch is exhausted"));
    }
    std::unordered_map<ProcessStationId, ProcessStation, IdHash> new_stations;
    std::unordered_map<GameplayObjectRef, ProcessStationId> new_station_by_object;
    std::unordered_map<ProcessInstanceId, ProcessInstance, IdHash> new_instances;
    std::unordered_set<ProcessReservationId, IdHash> reservation_ids;
    std::uint64_t max_station = 0, max_instance = 0, max_reservation = 0;

    for (auto &station : snapshot.stations)
    {
        if (!station.id.IsValid() || !station.station_object.IsValid() || station.efficiency_micro <= 0 ||
            new_stations.contains(station.id) || new_station_by_object.contains(station.station_object))
            return foundation::Result<void>::Failure(
                Error("gameplay.processes.restore_invalid", "invalid or duplicate station in snapshot"));
        if (station.id.value.High() == station_ids_.Scope().Raw())
            max_station = std::max(max_station, Low(station.id.value));
        new_station_by_object.emplace(station.station_object, station.id);
        new_stations.emplace(station.id, std::move(station));
    }
    for (auto &instance : snapshot.instances)
    {
        const auto *recipe = FindRecipe(instance.recipe);
        const auto *definition = recipe ? FindDefinition(recipe->process) : nullptr;
        if (!instance.id.IsValid() || !recipe || !definition || new_instances.contains(instance.id) ||
            instance.total_duration.ticks < 0)
            return foundation::Result<void>::Failure(
                Error("gameplay.processes.restore_invalid", "invalid process instance in snapshot"));
        if (instance.station.IsValid() && !new_station_by_object.contains(instance.station))
            return foundation::Result<void>::Failure(
                Error("gameplay.processes.restore_invalid", "process references missing station"));
        if (instance.state == ProcessInstanceState::Paused && instance.paused_remaining.ticks < 0)
            return foundation::Result<void>::Failure(
                Error("gameplay.processes.restore_invalid", "paused process has invalid remaining time"));

        std::unordered_set<ProcessInputId, IdHash> seen_inputs;
        for (const auto &reservation : instance.reserved_inputs)
        {
            const auto input = std::find_if(recipe->inputs.begin(), recipe->inputs.end(),
                                            [&](const auto &value) { return value.id == reservation.input; });
            if (!reservation.id.IsValid() || input == recipe->inputs.end() || reservation.type != input->type ||
                reservation.amount != input->amount || reservation.consumption != input->consumption ||
                !reservation.provider_token.IsPortable() || !seen_inputs.insert(reservation.input).second ||
                !reservation_ids.insert(reservation.id).second)
                return foundation::Result<void>::Failure(
                    Error("gameplay.processes.restore_invalid", "invalid process input reservation in snapshot"));
            if (reservation.id.value.High() == reservation_ids_.Scope().Raw())
                max_reservation = std::max(max_reservation, Low(reservation.id.value));
        }
        std::unordered_set<ProcessOutputId, IdHash> seen_outputs;
        for (const auto &prepared : instance.prepared_outputs)
        {
            const auto output = std::find_if(recipe->outputs.begin(), recipe->outputs.end(),
                                             [&](const auto &value) { return value.id == prepared.output; });
            if (output == recipe->outputs.end() || prepared.type != output->type || prepared.delivery != output->delivery ||
                !prepared.provider_token.IsPortable() || !seen_outputs.insert(prepared.output).second)
                return foundation::Result<void>::Failure(
                    Error("gameplay.processes.restore_invalid", "invalid prepared output in snapshot"));
        }
        if (!instance.runtime_payload.IsPortable())
            return foundation::Result<void>::Failure(
                Error("gameplay.processes.restore_invalid", "process runtime payload is not versioned/portable"));
        if (instance.state == ProcessInstanceState::Completed)
        {
            if (std::any_of(instance.reserved_inputs.begin(), instance.reserved_inputs.end(),
                            [](const auto &r) { return r.state == ProcessInputCommitState::Reserved; }) ||
                std::any_of(instance.prepared_outputs.begin(), instance.prepared_outputs.end(),
                            [](const auto &o) { return o.state == ProcessOutputCommitState::Prepared; }))
                return foundation::Result<void>::Failure(
                    Error("gameplay.processes.restore_invalid", "completed process has unfinished transaction legs"));
        }
        if (instance.id.value.High() == instance_ids_.Scope().Raw())
            max_instance = std::max(max_instance, Low(instance.id.value));
        new_instances.emplace(instance.id, std::move(instance));
    }

    const auto station_generator = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        snapshot.station_ids, station_ids_.Scope(), max_station);
    const auto instance_generator = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        snapshot.instance_ids, instance_ids_.Scope(), max_instance);
    const auto reservation_generator = ValidateMonotonicIdGeneratorSnapshot<GameplayObjectId>(
        snapshot.reservation_ids, reservation_ids_.Scope(), max_reservation);
    if (!station_generator || !instance_generator || !reservation_generator)
        return foundation::Result<void>::Failure(
            Error("gameplay.processes.restore_invalid", "process id generator snapshot is invalid"));

    stations_ = std::move(new_stations);
    station_by_object_ = std::move(new_station_by_object);
    instances_ = std::move(new_instances);
    station_ids_.Restore(snapshot.station_ids);
    instance_ids_.Restore(snapshot.instance_ids);
    reservation_ids_.Restore(snapshot.reservation_ids);
    revision_ = snapshot.revision;
    changes_.clear();
    next_change_sequence_ = snapshot.next_change_sequence;
    diagnostics_ = {};
    diagnostics_.stations = stations_.size();
    for (const auto &[id, instance] : instances_)
    {
        (void)id;
        if (instance.state == ProcessInstanceState::Completed)
            ++diagnostics_.completed_processes;
        else if (instance.state == ProcessInstanceState::Failed)
            ++diagnostics_.failed_processes;
        if (!IsTerminal(instance.state))
            ++diagnostics_.active_processes;
        for (const auto &reservation : instance.reserved_inputs)
            if (reservation.state == ProcessInputCommitState::Reserved)
                ++diagnostics_.reservations;
        for (const auto &output : instance.prepared_outputs)
            if (output.state == ProcessOutputCommitState::Committed)
                ++diagnostics_.outputs;
    }
    journal_epoch_ = *next_journal_epoch;
    return foundation::Result<void>::Success();
}

ProcessesDiagnostics ProcessesService::GetDiagnostics() const noexcept
{
    auto d = diagnostics_;
    d.stations = stations_.size();
    d.active_processes = 0;
    for (const auto &[id, instance] : instances_)
    {
        (void)id;
        if (!IsTerminal(instance.state))
            ++d.active_processes;
    }
    return d;
}

void ProcessesService::Record(ProcessChange change)
{
    if (next_change_sequence_ == 0)
        return;
    change.sequence = next_change_sequence_;
    if (next_change_sequence_ == std::numeric_limits<std::uint64_t>::max())
        next_change_sequence_ = 0;
    else
        ++next_change_sequence_;
    changes_.push_back(std::move(change));
    while (changes_.size() > change_journal_capacity_)
        changes_.pop_front();
}
} // namespace epidemic::gameplay::processes
