#include "Epidemic/GameFramework/Processes/processes.h"
#include "Epidemic/Foundation/error.h"

#include <algorithm>
#include <iterator>

#include <limits>

namespace epidemic::gameplay::processes
{
namespace
{
[[nodiscard]] foundation::Error Error(std::string_view code, std::string_view message)
{
    return foundation::Error::Create(code, message);
}
[[nodiscard]] GameplayTimePoint Add(GameplayTimePoint p, GameplayDuration d) noexcept
{
    return GameplayTimePoint{p.ticks + d.ticks};
}
[[nodiscard]] GameplayDuration ScaleDuration(GameplayDuration d, Fixed efficiency_micro) noexcept
{
    if (d.ticks <= 0)
        return d;
    const auto e = std::max<Fixed>(1, efficiency_micro);
    return GameplayDuration{std::max<std::int64_t>(1, (d.ticks * 1'000'000) / e)};
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
    if (definition.canonical_name.empty())
        return foundation::Result<ProcessDefinitionId>::Failure(
            Error("gameplay.processes.invalid_definition", "process definition name is required"));
    const auto canonical = ProcessDefinitionId::FromString(definition.canonical_name);
    if (!definition.id.IsValid())
        definition.id = canonical;
    if (definition.id != canonical || definitions_.contains(definition.id))
        return foundation::Result<ProcessDefinitionId>::Failure(
            Error("gameplay.processes.invalid_definition", "invalid or duplicate process definition"));
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
    for (const auto &input : recipe.inputs)
    {
        if (!input.id.IsValid() || !input.type.IsValid() || input.amount < 0)
            return foundation::Result<ProcessRecipeId>::Failure(
                Error("gameplay.processes.invalid_input", "recipe has invalid input"));
    }
    for (const auto &output : recipe.outputs)
    {
        if (!output.id.IsValid() || !output.type.IsValid() || output.amount < 0)
            return foundation::Result<ProcessRecipeId>::Failure(
                Error("gameplay.processes.invalid_output", "recipe has invalid output"));
    }
    Bump();
    recipe.revision = revision_;
    const auto id = recipe.id;
    recipes_.emplace(id, std::move(recipe));
    return foundation::Result<ProcessRecipeId>::Success(id);
}

foundation::Result<ProcessStationId> ProcessesService::RegisterStation(ProcessStation station)
{
    if (!station.station_object.IsValid())
        return foundation::Result<ProcessStationId>::Failure(
            Error("gameplay.processes.invalid_station", "station object is required"));
    if (!station.id.IsValid())
        station.id = ProcessStationId{station_ids_.Next()};
    if (stations_.contains(station.id) || station_by_object_.contains(station.station_object))
        return foundation::Result<ProcessStationId>::Failure(
            Error("gameplay.processes.invalid_station", "duplicate process station"));
    Bump();
    station.revision = revision_;
    const auto id = station.id;
    station_by_object_.emplace(station.station_object, id);
    stations_.emplace(id, std::move(station));
    diagnostics_.stations = stations_.size();
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

GameplayDuration ProcessesService::DurationFor(const ProcessRecipe &recipe, const ProcessDefinition &definition,
                                               const ProcessStation *station) const noexcept
{
    GameplayDuration duration{};
    for (const auto &step : definition.steps)
        duration.ticks += std::max<std::int64_t>(0, step.duration.ticks);
    if (duration.ticks == 0 && definition.timing == ProcessTimingPolicy::Instant)
        return duration;
    if (duration.ticks == 0)
        duration.ticks = 1;
    (void)recipe;
    if (station)
        duration = ScaleDuration(duration, station->efficiency_micro);
    return duration;
}

foundation::Result<void> ProcessesService::ReserveInputs(ProcessInstance &instance, const ProcessRecipe &recipe,
                                                         const StartProcessRequest &request)
{
    if (recipe.inputs.empty())
        return foundation::Result<void>::Success();
    if (!input_provider_)
        return foundation::Result<void>::Failure(
            Error("gameplay.processes.input_provider_missing", "process has inputs but no input provider"));
    for (const auto &input : recipe.inputs)
    {
        if (input.consumption == InputConsumptionPolicy::ToolNotConsumed ||
            input.consumption == InputConsumptionPolicy::ConditionRequired)
            continue;
        auto reserved = input_provider_->Reserve(input, request, instance.id);
        if (!reserved)
        {
            auto released = ReleaseInputs(instance, request.context);
            (void)released;
            return foundation::Result<void>::Failure(reserved.GetError());
        }
        if (!reserved.Value().id.IsValid())
            reserved.Value().id = ProcessReservationId{reservation_ids_.Next()};
        instance.reserved_inputs.push_back(std::move(reserved).Value());
        Record({0, ProcessChangeKind::InputReserved, instance.id, instance.recipe, instance.actor, request.now,
                request.context, revision_});
    }
    diagnostics_.reservations += instance.reserved_inputs.size();
    return foundation::Result<void>::Success();
}

foundation::Result<void> ProcessesService::ConsumeInputs(ProcessInstance &instance, GameplayContext context)
{
    if (!input_provider_)
        return instance.reserved_inputs.empty()
                   ? foundation::Result<void>::Success()
                   : foundation::Result<void>::Failure(
                         Error("gameplay.processes.input_provider_missing", "input provider missing"));
    for (const auto &reservation : instance.reserved_inputs)
    {
        auto consumed = input_provider_->Consume(reservation, context);
        if (!consumed)
            return consumed;
        Record({0, ProcessChangeKind::InputConsumed, instance.id, instance.recipe, instance.actor, context.time,
                context, revision_});
    }
    instance.reserved_inputs.clear();
    return foundation::Result<void>::Success();
}

foundation::Result<void> ProcessesService::ReleaseInputs(ProcessInstance &instance, GameplayContext context)
{
    if (!input_provider_)
        return instance.reserved_inputs.empty()
                   ? foundation::Result<void>::Success()
                   : foundation::Result<void>::Failure(
                         Error("gameplay.processes.input_provider_missing", "input provider missing"));
    for (const auto &reservation : instance.reserved_inputs)
    {
        auto released = input_provider_->Release(reservation, context);
        if (!released)
            return released;
    }
    instance.reserved_inputs.clear();
    return foundation::Result<void>::Success();
}

foundation::Result<void> ProcessesService::ProduceOutputs(const ProcessInstance &instance, const ProcessRecipe &recipe,
                                                          GameplayContext context)
{
    for (const auto &output : recipe.outputs)
    {
        const auto it = std::find_if(output_handlers_.begin(), output_handlers_.end(),
                                     [&](const auto *handler) { return handler && handler->Supports(output.type); });
        if (it == output_handlers_.end())
            return foundation::Result<void>::Failure(
                Error("gameplay.processes.output_handler_missing", "process output has no handler"));
        auto produced = (*it)->Produce(output, instance, context);
        if (!produced)
            return produced;
        ++diagnostics_.outputs;
        Record({0, ProcessChangeKind::OutputProduced, instance.id, instance.recipe, instance.actor, context.time,
                context, revision_});
    }
    return foundation::Result<void>::Success();
}

foundation::Result<ProcessInstanceId> ProcessesService::StartProcess(StartProcessRequest request)
{
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

    ProcessInstance instance;
    instance.id = ProcessInstanceId{instance_ids_.Next()};
    instance.recipe = recipe->id;
    instance.actor = request.actor;
    instance.station = request.station;
    instance.target = request.target;
    instance.state = ProcessInstanceState::Prepared;
    instance.started_at = request.now;
    instance.last_updated_at = request.now;
    instance.quality =
        quality_provider_
            ? quality_provider_->Resolve(*recipe, request)
            : ProcessQualityResult{ProcessQualityId::FromString("framework.quality.normal"), 1'000'000, {}};
    const auto duration = DurationFor(*recipe, *definition, station);
    instance.due_at = Add(request.now, duration);
    Bump();
    instance.revision = revision_;
    auto reserve = ReserveInputs(instance, *recipe, request);
    if (!reserve)
        return foundation::Result<ProcessInstanceId>::Failure(reserve.GetError());
    instance.state = duration.ticks <= 0 ? ProcessInstanceState::Running : ProcessInstanceState::Running;
    const auto id = instance.id;
    instances_.emplace(id, std::move(instance));
    ++diagnostics_.active_processes;
    Record({0, ProcessChangeKind::Started, id, recipe->id, request.actor, request.now, request.context, revision_});
    if (duration.ticks <= 0)
    {
        auto completed = Complete(id, request.now, request.context);
        if (!completed)
            return foundation::Result<ProcessInstanceId>::Failure(completed.GetError());
    }
    return foundation::Result<ProcessInstanceId>::Success(id);
}

foundation::Result<void> ProcessesService::Pause(ProcessInstanceId id, GameplayTimePoint now, GameplayContext context)
{
    auto it = instances_.find(id);
    if (it == instances_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.processes.instance_missing", "process instance is missing"));
    if (it->second.state != ProcessInstanceState::Running)
        return foundation::Result<void>::Failure(Error("gameplay.processes.invalid_state", "process is not running"));
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
        return foundation::Result<void>::Failure(
            Error("gameplay.processes.instance_missing", "process instance is missing"));
    if (it->second.state != ProcessInstanceState::Paused)
        return foundation::Result<void>::Failure(Error("gameplay.processes.invalid_state", "process is not paused"));
    Bump();
    it->second.state = ProcessInstanceState::Running;
    it->second.last_updated_at = now;
    it->second.revision = revision_;
    Record({0, ProcessChangeKind::Resumed, id, it->second.recipe, it->second.actor, now, context, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> ProcessesService::Cancel(ProcessInstanceId id, GameplayTimePoint now, GameplayContext context)
{
    auto it = instances_.find(id);
    if (it == instances_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.processes.instance_missing", "process instance is missing"));
    if (it->second.state == ProcessInstanceState::Completed || it->second.state == ProcessInstanceState::Cancelled)
        return foundation::Result<void>::Success();
    auto released = ReleaseInputs(it->second, context);
    if (!released)
        return released;
    Bump();
    it->second.state = ProcessInstanceState::Cancelled;
    it->second.last_updated_at = now;
    it->second.revision = revision_;
    Record({0, ProcessChangeKind::Cancelled, id, it->second.recipe, it->second.actor, now, context, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<void> ProcessesService::Complete(ProcessInstanceId id, GameplayTimePoint now,
                                                    GameplayContext context)
{
    auto it = instances_.find(id);
    if (it == instances_.end())
        return foundation::Result<void>::Failure(
            Error("gameplay.processes.instance_missing", "process instance is missing"));
    if (it->second.state == ProcessInstanceState::Completed)
        return foundation::Result<void>::Success();
    if (it->second.state != ProcessInstanceState::Running && it->second.state != ProcessInstanceState::Reserved)
        return foundation::Result<void>::Failure(
            Error("gameplay.processes.invalid_state", "process is not completable"));
    if (now.ticks < it->second.due_at.ticks)
        return foundation::Result<void>::Failure(Error("gameplay.processes.not_due", "process is not due"));
    const auto *recipe = FindRecipe(it->second.recipe);
    if (!recipe)
        return foundation::Result<void>::Failure(
            Error("gameplay.processes.recipe_missing", "process recipe is missing"));
    auto consumed = ConsumeInputs(it->second, context);
    if (!consumed)
        return consumed;
    auto produced = ProduceOutputs(it->second, *recipe, context);
    if (!produced)
        return produced;
    Bump();
    it->second.state = ProcessInstanceState::Completed;
    it->second.progress_micro = 1'000'000;
    it->second.last_updated_at = now;
    it->second.revision = revision_;
    ++diagnostics_.completed_processes;
    Record({0, ProcessChangeKind::Completed, id, it->second.recipe, it->second.actor, now, context, revision_});
    return foundation::Result<void>::Success();
}
foundation::Result<std::vector<ProcessInstanceId>> ProcessesService::CompleteDue(GameplayTimePoint now)
{
    std::vector<ProcessInstanceId> due;
    for (const auto &[id, instance] : instances_)
    {
        if (instance.state == ProcessInstanceState::Running && instance.due_at.ticks <= now.ticks)
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
std::vector<ProcessChange> ProcessesService::ChangesSince(std::uint64_t sequence) const
{
    std::vector<ProcessChange> out;
    std::copy_if(changes_.begin(), changes_.end(), std::back_inserter(out),
                 [&](const auto &c) { return c.sequence > sequence; });
    return out;
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
        if (instance.state != ProcessInstanceState::Completed ||
            definitions_.at(recipes_.at(instance.recipe).process).persistence == ProcessPersistencePolicy::Persistent)
            snapshot.instances.push_back(instance);
    }
    std::sort(snapshot.stations.begin(), snapshot.stations.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    std::sort(snapshot.instances.begin(), snapshot.instances.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    snapshot.station_ids = station_ids_.GetSnapshot();
    snapshot.instance_ids = instance_ids_.GetSnapshot();
    snapshot.reservation_ids = reservation_ids_.GetSnapshot();
    snapshot.revision = revision_;
    return snapshot;
}
foundation::Result<void> ProcessesService::RestoreSnapshot(ProcessesSnapshot snapshot)
{
    stations_.clear();
    station_by_object_.clear();
    instances_.clear();
    for (auto &station : snapshot.stations)
    {
        if (!station.id.IsValid() || !station.station_object.IsValid())
            return foundation::Result<void>::Failure(
                Error("gameplay.processes.restore_invalid", "invalid station in snapshot"));
        station_by_object_[station.station_object] = station.id;
        stations_[station.id] = std::move(station);
    }
    for (auto &instance : snapshot.instances)
    {
        if (!instance.id.IsValid() || !recipes_.contains(instance.recipe))
            return foundation::Result<void>::Failure(
                Error("gameplay.processes.restore_invalid", "invalid process instance in snapshot"));
        instances_[instance.id] = std::move(instance);
    }
    station_ids_.Restore(snapshot.station_ids);
    instance_ids_.Restore(snapshot.instance_ids);
    reservation_ids_.Restore(snapshot.reservation_ids);
    revision_ = snapshot.revision;
    changes_.clear();
    next_change_sequence_ = 1;
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
        if (instance.state == ProcessInstanceState::Running || instance.state == ProcessInstanceState::Paused ||
            instance.state == ProcessInstanceState::Reserved)
            ++d.active_processes;
    }
    return d;
}
void ProcessesService::Record(ProcessChange change)
{
    change.sequence = next_change_sequence_++;
    changes_.push_back(change);
}
} // namespace epidemic::gameplay::processes
