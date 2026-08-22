#include "Epidemic/Core/task_scheduler.h"
#include "Epidemic/GameFramework/Effects/effects.h"

#include <unordered_map>
#include <optional>

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::effects;

namespace
{
class TargetState final : public IEffectTargetStateProvider
{
  public:
    [[nodiscard]] EffectTargetState Resolve(GameplayObjectRef target) const override
    {
        return {target.IsValid(), target.id.Low() % 2 == 0, target.id.Low() % 2 == 0};
    }
};

class CountingHandler final : public IEffectHandler
{
  public:
    CountingHandler(EffectTypeId type, std::unordered_map<GameplayObjectRef, int>& values, EffectTypeId derived = {},
                    std::optional<GameplayContext>* observed_context = nullptr)
        : type_(type), values_(values), derived_(derived), observed_context_(observed_context)
    {
    }
    [[nodiscard]] EffectTypeId Type() const noexcept override { return type_; }
    [[nodiscard]] EffectHandlerCapabilities Capabilities() const noexcept override { return {true, false, false, true}; }
    [[nodiscard]] foundation::Result<EffectPrepareResult> Prepare(const EffectOperation& operation) const override
    {
        return foundation::Result<EffectPrepareResult>::Success(
            {operation.magnitude_micro < 0 ? EffectPrepareDisposition::Rejected : EffectPrepareDisposition::Accepted, {}});
    }
    [[nodiscard]] foundation::Result<EffectCommitResult> Commit(const EffectOperation& operation, const RegisteredEffectPayload&) override
    {
        values_[operation.target] += static_cast<int>(operation.magnitude_micro);
        if (observed_context_ != nullptr) *observed_context_ = operation.context;
        EffectCommitResult result;
        if (derived_.IsValid())
        {
            EffectOperation derived;
            derived.type = derived_;
            derived.target = operation.target;
            derived.magnitude_micro = 1;
            // Intentionally leave derived.context empty. EffectService must inherit
            // the complete causal GameplayContext from the parent operation.
            result.derived_effects.push_back(derived);
        }
        return foundation::Result<EffectCommitResult>::Success(std::move(result));
    }

  private:
    EffectTypeId type_{};
    std::unordered_map<GameplayObjectRef, int>& values_;
    EffectTypeId derived_{};
    std::optional<GameplayContext>* observed_context_ = nullptr;
};

class MaterializedHandler final : public IEffectHandler
{
  public:
    explicit MaterializedHandler(EffectTypeId type) : type_(type) {}
    [[nodiscard]] EffectTypeId Type() const noexcept override { return type_; }
    [[nodiscard]] EffectHandlerCapabilities Capabilities() const noexcept override { return {false, true, true, true}; }
    [[nodiscard]] foundation::Result<EffectPrepareResult> Prepare(const EffectOperation&) const override
    {
        return foundation::Result<EffectPrepareResult>::Success({EffectPrepareDisposition::Accepted, {}});
    }
    [[nodiscard]] foundation::Result<EffectCommitResult> Commit(const EffectOperation&, const RegisteredEffectPayload&) override
    {
        return foundation::Result<EffectCommitResult>::Success({EffectCommitDisposition::Applied, {}});
    }
  private:
    EffectTypeId type_{};
};
}

int main()
{
    EffectService service;
    TargetState target_state;
    service.SetTargetStateProvider(&target_state);
    std::unordered_map<GameplayObjectRef, int> values;

    const auto derived_type = EffectTypeId::FromString("test.effect.derived");
    const auto root_type = EffectTypeId::FromString("test.effect.root");
    const auto materialized_type = EffectTypeId::FromString("test.effect.materialized");
    std::optional<GameplayContext> observed_derived_context;
    if (!service.RegisterHandler("test.effect.derived", std::make_shared<CountingHandler>(derived_type, values, EffectTypeId{}, &observed_derived_context)) ||
        !service.RegisterHandler("test.effect.root", std::make_shared<CountingHandler>(root_type, values, derived_type)) ||
        !service.RegisterHandler("test.effect.materialized", std::make_shared<MaterializedHandler>(materialized_type))) return 1;

    EffectDefinition definition;
    definition.canonical_name = "test.effect.bundle";
    definition.steps.push_back(EffectStepDefinition{root_type, EffectTargetSelector::AllTargets, 2, {}});
    const auto definition_id = service.RegisterDefinition(definition);
    EffectDefinition mat_definition;
    mat_definition.canonical_name = "test.effect.mat_bundle";
    mat_definition.steps.push_back(EffectStepDefinition{materialized_type, EffectTargetSelector::AllTargets, 1, {}});
    const auto mat_id = service.RegisterDefinition(mat_definition);
    if (!definition_id || !mat_id) return 2;
    service.Freeze();
    EffectDefinition late_definition;
    late_definition.canonical_name = "test.effect.late";
    late_definition.steps.push_back(EffectStepDefinition{root_type, EffectTargetSelector::AllTargets, 1, {}});
    if (service.RegisterDefinition(std::move(late_definition))) return 3;

    const GameplayDomainId domain = GameplayDomainId::FromString("test.domain");
    const GameplayObjectRef a{domain, GameplayObjectId::FromRaw(1, 2)};
    const GameplayObjectRef b{domain, GameplayObjectId::FromRaw(1, 4)};
    EffectRequest request;
    request.definition = definition_id.Value();
    request.targets = {b, a, b};
    request.scale_micro = 1'000'000;
    request.context.tick = GameplayTickId{1};
    request.context.time = GameplayTimePoint{123};
    request.context.actor = a;
    request.context.instigator = b;
    request.context.source = a;
    request.context.operation = OperationId::FromString("test.effect.operation");
    request.context.correlation = CorrelationId::FromString("test.correlation");
    request.context.parent_operation = OperationId::FromString("test.effect.parent");
    request.context.cause_event = EventId::FromString("test.effect.event");

    core::tasks::SimpleTaskScheduler scheduler(4);
    const auto executed = service.Execute(request, {}, &scheduler);
    if (!executed || executed.Value().disposition != EffectBatchDisposition::Succeeded || executed.Value().waves != 2) return 4;
    if (values[a] != 3 || values[b] != 3 || service.GetDiagnostics().derived_effects != 2) return 5;
    if (!observed_derived_context.has_value()) return 51;
    const auto& inherited = *observed_derived_context;
    if (inherited.tick != request.context.tick || inherited.time != request.context.time ||
        inherited.actor != request.context.actor || inherited.instigator != request.context.instigator ||
        inherited.source != request.context.source || inherited.operation != request.context.operation ||
        inherited.correlation != request.context.correlation || inherited.parent_operation != request.context.parent_operation ||
        inherited.cause_event != request.context.cause_event) return 52;

    // Materialized-only handler rejects an abstract target without triggering materialization.
    const GameplayObjectRef abstract_target{domain, GameplayObjectId::FromRaw(1, 3)};
    EffectRequest mat_request;
    mat_request.definition = mat_id.Value();
    mat_request.targets = {abstract_target};
    const auto unavailable = service.Execute(mat_request);
    if (!unavailable || unavailable.Value().operations.front().disposition != EffectOperationDisposition::Unavailable) return 6;

    // Budget protects against derived effect storms.
    EffectExecutionBudget tiny;
    tiny.max_effects = 1;
    tiny.max_waves = 1;
    const auto budgeted = service.Execute(request, tiny);
    if (!budgeted || budgeted.Value().disposition != EffectBatchDisposition::Failed || service.GetDiagnostics().budget_exhaustions == 0) return 7;

    const auto clock = ClockId::FromString("test.clock");
    const auto deferred = service.Defer(request, clock, GameplayTimePoint{50}, DeferredEffectPersistence::Persistent);
    if (!deferred) return 8;
    const auto schedule = ScheduleId::FromString("test.schedule");
    if (!service.BindDeferredSchedule(deferred.Value(), schedule)) return 9;
    const auto snapshot = service.CaptureSnapshot();
    if (snapshot.deferred.size() != 1) return 10;
    auto taken = service.TakeDeferredBySchedule(schedule);
    if (!taken || service.FindDeferred(deferred.Value()) != nullptr) return 11;
    if (!service.RestoreSnapshot(snapshot) || service.FindDeferred(deferred.Value()) == nullptr) return 12;
    if (service.CancelDeferredTargeting(a) != 1) return 13;

    scheduler.Shutdown();
    return 0;
}

