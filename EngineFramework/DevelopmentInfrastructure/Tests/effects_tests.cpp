#include "allocation_fault_injection.h"
#include "mutation_fault_sweep.h"
#include "pre_state_verification.h"
#include "restore_fault_sweep.h"
#include "Epidemic/GameFramework/Effects/effects.h"

#include <limits>
#include <unordered_map>
#include <optional>
#include <stdexcept>

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

class ThrowingTargetState final : public IEffectTargetStateProvider
{
  public:
    [[nodiscard]] EffectTargetState Resolve(GameplayObjectRef) const override
    {
        throw std::runtime_error("target state failed");
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
    [[nodiscard]] foundation::Result<EffectCommitResult> Commit(const EffectOperation& operation, const RegisteredEffectPayload&) noexcept override
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
    [[nodiscard]] foundation::Result<EffectCommitResult> Commit(const EffectOperation&, const RegisteredEffectPayload&) noexcept override
    {
        return foundation::Result<EffectCommitResult>::Success({EffectCommitDisposition::Applied, {}});
    }
  private:
    EffectTypeId type_{};
};

class ThrowingPrepareHandler final : public IEffectHandler
{
  public:
    explicit ThrowingPrepareHandler(EffectTypeId type) : type_(type) {}
    [[nodiscard]] EffectTypeId Type() const noexcept override { return type_; }
    [[nodiscard]] EffectHandlerCapabilities Capabilities() const noexcept override { return {}; }
    [[nodiscard]] foundation::Result<EffectPrepareResult> Prepare(const EffectOperation&) const override
    {
        throw std::runtime_error("prepare failed");
    }
    [[nodiscard]] foundation::Result<EffectCommitResult> Commit(const EffectOperation&, const RegisteredEffectPayload&) noexcept override
    {
        return foundation::Result<EffectCommitResult>::Success({});
    }
  private:
    EffectTypeId type_{};
};

struct EffectsFaultState
{
    EffectsSnapshot snapshot;
    EffectsDiagnostics diagnostics;
    ChangeCursor latest_cursor;
    std::uint64_t oldest_change_sequence = 0;
    std::vector<DeferredEffectRecord> all_deferred;
    std::vector<DeferredEffectRecord> unscheduled_deferred;
    std::size_t scheduled_lookup_count = 0;
    std::size_t scheduled_lookup_successes = 0;
    int callback_total = 0;
};

[[nodiscard]] bool SameRequestShape(const EffectRequest& left, const EffectRequest& right)
{
    return left.definition == right.definition && left.targets == right.targets && left.scale_micro == right.scale_micro &&
           left.context.tick == right.context.tick && left.context.time == right.context.time &&
           left.context.actor == right.context.actor && left.context.instigator == right.context.instigator &&
           left.context.source == right.context.source && left.context.operation == right.context.operation &&
           left.context.correlation == right.context.correlation && left.context.parent_operation == right.context.parent_operation &&
           left.context.cause_event == right.context.cause_event;
}

[[nodiscard]] bool SameDeferredRecords(const std::vector<DeferredEffectRecord>& left,
                                       const std::vector<DeferredEffectRecord>& right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        const auto& before = left[index];
        const auto& after = right[index];
        if (before.id != after.id || !SameRequestShape(before.request, after.request) || before.clock != after.clock ||
            before.due != after.due || before.schedule != after.schedule || before.persistence != after.persistence)
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool SameDiagnostics(const EffectsDiagnostics& left, const EffectsDiagnostics& right)
{
    return left.executions == right.executions && left.operations == right.operations && left.targets == right.targets &&
           left.derived_effects == right.derived_effects && left.waves == right.waves &&
           left.rejections == right.rejections && left.budget_exhaustions == right.budget_exhaustions &&
           left.deferred_effects == right.deferred_effects;
}

[[nodiscard]] int SumValues(const std::unordered_map<GameplayObjectRef, int>& values)
{
    int total = 0;
    for (const auto& [_, value] : values)
    {
        total += value;
    }
    return total;
}

struct EffectsFaultFixture
{
    EffectService service;
    std::shared_ptr<TargetState> target_state = std::make_shared<TargetState>();
    std::shared_ptr<std::unordered_map<GameplayObjectRef, int>> values =
        std::make_shared<std::unordered_map<GameplayObjectRef, int>>();
    EffectTypeId root_type = EffectTypeId::FromString("test.effect.fault.root");
    EffectDefinitionId definition_id{};
    GameplayDomainId domain = GameplayDomainId::FromString("test.effect.fault.domain");
    GameplayObjectRef a{domain, GameplayObjectId::FromRaw(1, 2)};
    GameplayObjectRef b{domain, GameplayObjectId::FromRaw(1, 4)};
    ClockId clock = ClockId::FromString("test.effect.fault.clock");
    ScheduleId schedule = ScheduleId::FromString("test.effect.fault.schedule");
    DeferredEffectId deferred{};

    EffectsFaultFixture()
    {
        values->emplace(a, 0);
        values->emplace(b, 0);
        service.SetTargetStateProvider(target_state.get());
        [[maybe_unused]] const auto handler =
            service.RegisterHandler("test.effect.fault.root", std::make_shared<CountingHandler>(root_type, *values));

        EffectDefinition definition;
        definition.canonical_name = "test.effect.fault.bundle";
        definition.steps.push_back(EffectStepDefinition{root_type, EffectTargetSelector::AllTargets, 2, {}});
        const auto registered = service.RegisterDefinition(std::move(definition));
        if (registered)
        {
            definition_id = registered.Value();
        }
        service.Freeze();
    }

    [[nodiscard]] EffectRequest MakeRequest() const
    {
        EffectRequest request;
        request.definition = definition_id;
        request.targets = {b, a, b};
        request.scale_micro = 1'000'000;
        request.context.tick = GameplayTickId{5};
        request.context.time = GameplayTimePoint{500};
        request.context.actor = a;
        request.context.instigator = b;
        request.context.source = a;
        request.context.operation = OperationId::FromString("test.effect.fault.operation");
        request.context.correlation = CorrelationId::FromString("test.effect.fault.correlation");
        request.context.parent_operation = OperationId::FromString("test.effect.fault.parent");
        request.context.cause_event = EventId::FromString("test.effect.fault.event");
        return request;
    }

    bool CreateDeferred(bool bind_schedule)
    {
        const auto created = service.Defer(MakeRequest(), clock, GameplayTimePoint{900}, DeferredEffectPersistence::Persistent);
        if (!created)
        {
            return false;
        }
        deferred = created.Value();
        if (bind_schedule)
        {
            return static_cast<bool>(service.BindDeferredSchedule(deferred, schedule));
        }
        return true;
    }
};

[[nodiscard]] EffectsFaultState CaptureEffectsFaultState(const EffectsFaultFixture& fixture)
{
    EffectsFaultState state;
    state.snapshot = fixture.service.CaptureSnapshot();
    state.diagnostics = fixture.service.GetDiagnostics();
    state.latest_cursor = fixture.service.LatestChangeCursor();
    state.oldest_change_sequence = fixture.service.OldestChangeSequence();
    state.all_deferred = fixture.service.AllDeferred();
    state.unscheduled_deferred = fixture.service.UnscheduledDeferred();
    state.callback_total = SumValues(*fixture.values);
    for (const auto& record : state.all_deferred)
    {
        if (record.schedule.has_value())
        {
            ++state.scheduled_lookup_count;
            if (fixture.service.PeekDeferredBySchedule(*record.schedule))
            {
                ++state.scheduled_lookup_successes;
            }
        }
    }
    return state;
}

[[nodiscard]] epidemic::tests::pre_state::ComparisonReport CompareEffectsFaultState(std::string scope,
                                                                                    const EffectsFaultState& before,
                                                                                    const EffectsFaultState& after)
{
    return epidemic::tests::pre_state::StateComparator(std::move(scope))
        .RequireEqual(epidemic::tests::pre_state::StateFacet::PrimaryRecords,
                      before.snapshot.deferred.size(),
                      after.snapshot.deferred.size(),
                      "deferred primary record count")
        .Require(epidemic::tests::pre_state::StateFacet::RecordPayloads,
                 SameDeferredRecords(before.snapshot.deferred, after.snapshot.deferred),
                 "deferred record payloads")
        .RequireEqual(epidemic::tests::pre_state::StateFacet::SecondaryIndexes,
                      before.scheduled_lookup_successes,
                      after.scheduled_lookup_successes,
                      "scheduled deferred lookup successes")
        .Require(epidemic::tests::pre_state::StateFacet::IdGenerators,
                 before.snapshot.execution_ids.scope == after.snapshot.execution_ids.scope &&
                     before.snapshot.execution_ids.next == after.snapshot.execution_ids.next &&
                     before.snapshot.deferred_ids.scope == after.snapshot.deferred_ids.scope &&
                     before.snapshot.deferred_ids.next == after.snapshot.deferred_ids.next,
                 "execution/deferred id generators")
        .RequireEqual(epidemic::tests::pre_state::StateFacet::Revisions,
                      before.snapshot.change_epoch,
                      after.snapshot.change_epoch,
                      "change epoch")
        .Require(epidemic::tests::pre_state::StateFacet::Journal,
                 before.latest_cursor == after.latest_cursor && before.oldest_change_sequence == after.oldest_change_sequence,
                 "journal cursor and retention window")
        .RequireEqual(epidemic::tests::pre_state::StateFacet::JournalEpoch,
                      before.snapshot.change_epoch,
                      after.snapshot.change_epoch,
                      "journal epoch")
        .RequireEqual(epidemic::tests::pre_state::StateFacet::JournalSequence,
                      before.latest_cursor,
                      after.latest_cursor,
                      "latest change cursor")
        .RequireEqual(epidemic::tests::pre_state::StateFacet::JournalRetainedRecords,
                      before.oldest_change_sequence,
                      after.oldest_change_sequence,
                      "oldest retained sequence")
        .RequireEqual(epidemic::tests::pre_state::StateFacet::JournalLatestCursor,
                      before.latest_cursor,
                      after.latest_cursor,
                      "latest cursor")
        .RequireEqual(epidemic::tests::pre_state::StateFacet::ExternalCallbacks,
                      before.callback_total,
                      after.callback_total,
                      "handler callback state")
        .Require(epidemic::tests::pre_state::StateFacet::PublicReadModels,
                 SameDeferredRecords(before.all_deferred, after.all_deferred) &&
                     SameDeferredRecords(before.unscheduled_deferred, after.unscheduled_deferred) &&
                     before.scheduled_lookup_count == after.scheduled_lookup_count &&
                     before.scheduled_lookup_successes == after.scheduled_lookup_successes &&
                     SameDiagnostics(before.diagnostics, after.diagnostics),
                 "public deferred/diagnostics read models")
        .Finish();
}

template <typename TMakeFixture, typename TInvoke>
[[nodiscard]] bool EffectsMutationSweepPassed(std::string api, TMakeFixture&& make_fixture, TInvoke&& invoke)
{
    const auto report = epidemic::tests::mutation_fault::RunObservedMutationSweep(
        std::move(api),
        2,
        std::forward<TMakeFixture>(make_fixture),
        std::forward<TInvoke>(invoke),
        [](const EffectsFaultFixture& fixture) { return CaptureEffectsFaultState(fixture); },
        [](const epidemic::tests::allocation_fault::SweepIteration& iteration,
           const EffectsFaultState& baseline,
           const EffectsFaultFixture& fixture) {
            if (iteration.failure == epidemic::tests::allocation_fault::FailureKind::None)
            {
                return true;
            }
            const auto after = CaptureEffectsFaultState(fixture);
            return CompareEffectsFaultState(iteration.api + ".pre_state", baseline, after)
                .PassedAndCovers(epidemic::tests::pre_state::RequiredExternalMutationFacets);
        });
    return epidemic::tests::mutation_fault::PassedObservedMutationSweep(report);
}

struct EffectsRegistrationFixture
{
    EffectService service;
    std::shared_ptr<std::unordered_map<GameplayObjectRef, int>> values =
        std::make_shared<std::unordered_map<GameplayObjectRef, int>>();
    EffectTypeId root_type = EffectTypeId::FromString("test.effect.registration.root");
    EffectDefinitionId definition_id = EffectDefinitionId::FromString("test.effect.registration.bundle");
    GameplayDomainId domain = GameplayDomainId::FromString("test.effect.registration.domain");
    GameplayObjectRef a{domain, GameplayObjectId::FromRaw(1, 2)};

    EffectsRegistrationFixture()
    {
        values->emplace(a, 0);
    }

    [[nodiscard]] EffectDefinition MakeDefinition() const
    {
        EffectDefinition definition;
        definition.canonical_name = "test.effect.registration.bundle";
        definition.steps.push_back(EffectStepDefinition{root_type, EffectTargetSelector::AllTargets, 2, {}});
        return definition;
    }
};

struct EffectsRegistrationState
{
    EffectsSnapshot snapshot;
    EffectsDiagnostics diagnostics;
    ChangeCursor latest_cursor;
    bool frozen = false;
    bool definition_visible = false;
};

[[nodiscard]] EffectsRegistrationState CaptureEffectsRegistrationState(const EffectsRegistrationFixture& fixture)
{
    return EffectsRegistrationState{fixture.service.CaptureSnapshot(),
                                    fixture.service.GetDiagnostics(),
                                    fixture.service.LatestChangeCursor(),
                                    fixture.service.IsFrozen(),
                                    fixture.service.FindDefinition(fixture.definition_id) != nullptr};
}

[[nodiscard]] bool SameRegistrationState(const EffectsRegistrationState& before, const EffectsRegistrationState& after)
{
    return before.snapshot.deferred.size() == after.snapshot.deferred.size() &&
           before.snapshot.execution_ids.scope == after.snapshot.execution_ids.scope &&
           before.snapshot.execution_ids.next == after.snapshot.execution_ids.next &&
           before.snapshot.deferred_ids.scope == after.snapshot.deferred_ids.scope &&
           before.snapshot.deferred_ids.next == after.snapshot.deferred_ids.next &&
           before.snapshot.change_epoch == after.snapshot.change_epoch && SameDiagnostics(before.diagnostics, after.diagnostics) &&
           before.latest_cursor == after.latest_cursor && before.frozen == after.frozen &&
           before.definition_visible == after.definition_visible;
}

[[nodiscard]] bool EffectsRegisterDefinitionSweepPassed()
{
    const auto report = epidemic::tests::mutation_fault::RunObservedMutationSweep(
        "Effects.RegisterDefinition",
        2,
        [] {
            EffectsRegistrationFixture fixture;
            [[maybe_unused]] const auto handler = fixture.service.RegisterHandler(
                "test.effect.registration.root", std::make_shared<CountingHandler>(fixture.root_type, *fixture.values));
            return fixture;
        },
        [](EffectsRegistrationFixture& fixture, epidemic::tests::allocation_fault::SweepRunContext&) {
            return fixture.service.RegisterDefinition(fixture.MakeDefinition());
        },
        [](const EffectsRegistrationFixture& fixture) { return CaptureEffectsRegistrationState(fixture); },
        [](const epidemic::tests::allocation_fault::SweepIteration& iteration,
           const EffectsRegistrationState& baseline,
           const EffectsRegistrationFixture& fixture) {
            if (iteration.failure == epidemic::tests::allocation_fault::FailureKind::None)
            {
                return fixture.service.FindDefinition(fixture.definition_id) != nullptr;
            }
            return SameRegistrationState(baseline, CaptureEffectsRegistrationState(fixture));
        });
    return epidemic::tests::mutation_fault::PassedObservedMutationSweep(report);
}

[[nodiscard]] bool EffectsRegisterHandlerSweepPassed()
{
    const auto report = epidemic::tests::mutation_fault::RunObservedMutationSweep(
        "Effects.RegisterHandler",
        2,
        [] { return EffectsRegistrationFixture{}; },
        [](EffectsRegistrationFixture& fixture, epidemic::tests::allocation_fault::SweepRunContext&) {
            return fixture.service.RegisterHandler(
                "test.effect.registration.root", std::make_shared<CountingHandler>(fixture.root_type, *fixture.values));
        },
        [](const EffectsRegistrationFixture& fixture) { return CaptureEffectsRegistrationState(fixture); },
        [](const epidemic::tests::allocation_fault::SweepIteration& iteration,
           const EffectsRegistrationState& baseline,
           EffectsRegistrationFixture& fixture) {
            if (iteration.failure == epidemic::tests::allocation_fault::FailureKind::None)
            {
                return static_cast<bool>(fixture.service.RegisterDefinition(fixture.MakeDefinition()));
            }
            const auto handler_leaked = static_cast<bool>(fixture.service.RegisterDefinition(fixture.MakeDefinition()));
            return !handler_leaked && SameRegistrationState(baseline, CaptureEffectsRegistrationState(fixture));
        });
    return epidemic::tests::mutation_fault::PassedObservedMutationSweep(report);
}

[[nodiscard]] bool EffectsInlineLifecycleChecksPassed()
{
    auto freeze_probe = epidemic::tests::allocation_fault::CountObservedAllocations([] {
        EffectService service;
        return [service = std::move(service)](epidemic::tests::allocation_fault::SweepRunContext& context) mutable {
            context.MarkMethodInvoked();
            service.Freeze();
            return service.IsFrozen();
        };
    });

    auto provider_probe = epidemic::tests::allocation_fault::CountObservedAllocations([] {
        EffectService service;
        auto target_state = std::make_shared<TargetState>();
        return [service = std::move(service), target_state](epidemic::tests::allocation_fault::SweepRunContext& context) mutable {
            context.MarkMethodInvoked();
            service.SetTargetStateProvider(target_state.get());
            return true;
        };
    });

    return freeze_probe.invocation.failure == epidemic::tests::allocation_fault::FailureKind::None &&
           freeze_probe.invocation.method_invoked && freeze_probe.observed_allocations == 0 &&
           provider_probe.invocation.failure == epidemic::tests::allocation_fault::FailureKind::None &&
           provider_probe.invocation.method_invoked && provider_probe.observed_allocations == 0;
}
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
    const auto throwing_type = EffectTypeId::FromString("test.effect.throwing_prepare");
    const auto validated_type = EffectTypeId::FromString("test.effect.validator");
    std::optional<GameplayContext> observed_derived_context;
    if (!service.RegisterHandler("test.effect.derived", std::make_shared<CountingHandler>(derived_type, values, EffectTypeId{}, &observed_derived_context)) ||
        !service.RegisterHandler("test.effect.root", std::make_shared<CountingHandler>(root_type, values, derived_type)) ||
        !service.RegisterHandler("test.effect.materialized", std::make_shared<MaterializedHandler>(materialized_type)) ||
        !service.RegisterHandler("test.effect.throwing_prepare", std::make_shared<ThrowingPrepareHandler>(throwing_type)) ||
        !service.RegisterHandler("test.effect.validator", std::make_shared<CountingHandler>(validated_type, values),
                                 TypeId::FromString("test.payload"), sizeof(std::uint32_t),
                                 [](std::span<const std::byte>) -> bool { throw std::runtime_error("validator failed"); })) return 1;

    EffectDefinition definition;
    definition.canonical_name = "test.effect.bundle";
    definition.steps.push_back(EffectStepDefinition{root_type, EffectTargetSelector::AllTargets, 2, {}});
    const auto definition_id = service.RegisterDefinition(definition);
    EffectDefinition mat_definition;
    mat_definition.canonical_name = "test.effect.mat_bundle";
    mat_definition.steps.push_back(EffectStepDefinition{materialized_type, EffectTargetSelector::AllTargets, 1, {}});
    const auto mat_id = service.RegisterDefinition(mat_definition);
    EffectDefinition throwing_definition;
    throwing_definition.canonical_name = "test.effect.throwing_bundle";
    throwing_definition.steps.push_back(EffectStepDefinition{throwing_type, EffectTargetSelector::AllTargets, 1, {}});
    const auto throwing_id = service.RegisterDefinition(throwing_definition);
    if (!definition_id || !mat_id || !throwing_id) return 2;

    // A throwing validator must be converted into a controlled registration failure.
    EffectDefinition validator_definition;
    validator_definition.canonical_name = "test.effect.validator_bundle";
    validator_definition.steps.push_back(EffectStepDefinition{validated_type, EffectTargetSelector::AllTargets, 1,
        RegisteredEffectPayload::FromTrivial(TypeId::FromString("test.payload"), std::uint32_t{7})});
    if (service.RegisterDefinition(std::move(validator_definition))) return 21;
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

    const auto executed = service.Execute(request);
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

    // Target-state provider exceptions are contained and become a failed operation.
    ThrowingTargetState throwing_target_state;
    service.SetTargetStateProvider(&throwing_target_state);
    const auto provider_failure = service.Execute(mat_request);
    if (!provider_failure || provider_failure.Value().operations.empty() ||
        provider_failure.Value().operations.front().disposition != EffectOperationDisposition::Failed) return 61;
    service.SetTargetStateProvider(&target_state);

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

    EffectRequest throwing_request;
    throwing_request.definition = throwing_id.Value();
    throwing_request.targets = {a};
    const auto throwing_result = service.Execute(throwing_request);
    if (!throwing_result || throwing_result.Value().operations.size() != 1 ||
        throwing_result.Value().operations.front().disposition != EffectOperationDisposition::Failed) return 14;

    // Restore must reject wrong generator scopes and a deferred generator behind restored IDs without mutating service state.
    if (!service.ReadChangesSince(service.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required) return 19;
    EffectService empty_journal;
    if (!empty_journal.ReadChangesSince(empty_journal.LatestChangeCursor().AtSequence(std::numeric_limits<std::uint64_t>::max())).snapshot_required) return 20;

    const auto stable_snapshot = service.CaptureSnapshot();
    auto bad_scope = stable_snapshot;
    bad_scope.execution_ids.scope ^= 0x55u;
    if (service.RestoreSnapshot(bad_scope)) return 15;
    const auto after_bad_scope = service.CaptureSnapshot();
    if (after_bad_scope.execution_ids.scope != stable_snapshot.execution_ids.scope ||
        after_bad_scope.deferred_ids.scope != stable_snapshot.deferred_ids.scope ||
        after_bad_scope.deferred.size() != stable_snapshot.deferred.size()) return 16;

    auto behind = snapshot;
    if (behind.deferred.empty()) return 17;
    behind.deferred_ids.next = behind.deferred.front().id.value.Low();
    if (service.RestoreSnapshot(std::move(behind))) return 18;

    if (!EffectsRegisterHandlerSweepPassed()) return 911;
    if (!EffectsRegisterDefinitionSweepPassed()) return 912;
    if (!EffectsInlineLifecycleChecksPassed()) return 913;

    if (!EffectsMutationSweepPassed(
            "Effects.Execute",
            [] { return EffectsFaultFixture{}; },
            [](EffectsFaultFixture& fixture, epidemic::tests::allocation_fault::SweepRunContext&) {
                return fixture.service.Execute(fixture.MakeRequest());
            }))
        return 903;

    if (!EffectsMutationSweepPassed(
            "Effects.Defer",
            [] { return EffectsFaultFixture{}; },
            [](EffectsFaultFixture& fixture, epidemic::tests::allocation_fault::SweepRunContext&) {
                return fixture.service.Defer(
                    fixture.MakeRequest(), fixture.clock, GameplayTimePoint{901}, DeferredEffectPersistence::Persistent);
            }))
        return 904;

    if (!EffectsMutationSweepPassed(
            "Effects.BindDeferredSchedule",
            [] {
                EffectsFaultFixture fixture;
                [[maybe_unused]] const auto ready = fixture.CreateDeferred(false);
                return fixture;
            },
            [](EffectsFaultFixture& fixture, epidemic::tests::allocation_fault::SweepRunContext&) {
                return fixture.service.BindDeferredSchedule(fixture.deferred, fixture.schedule);
            }))
        return 905;

    if (!EffectsMutationSweepPassed(
            "Effects.ClearDeferredSchedule",
            [] {
                EffectsFaultFixture fixture;
                [[maybe_unused]] const auto ready = fixture.CreateDeferred(true);
                return fixture;
            },
            [](EffectsFaultFixture& fixture, epidemic::tests::allocation_fault::SweepRunContext&) {
                return fixture.service.ClearDeferredSchedule(fixture.deferred);
            }))
        return 906;

    if (!EffectsMutationSweepPassed(
            "Effects.AcknowledgeDeferredBySchedule",
            [] {
                EffectsFaultFixture fixture;
                [[maybe_unused]] const auto ready = fixture.CreateDeferred(true);
                return fixture;
            },
            [](EffectsFaultFixture& fixture, epidemic::tests::allocation_fault::SweepRunContext&) {
                return fixture.service.AcknowledgeDeferredBySchedule(fixture.schedule);
            }))
        return 907;

    if (!EffectsMutationSweepPassed(
            "Effects.TakeDeferredBySchedule",
            [] {
                EffectsFaultFixture fixture;
                [[maybe_unused]] const auto ready = fixture.CreateDeferred(true);
                return fixture;
            },
            [](EffectsFaultFixture& fixture, epidemic::tests::allocation_fault::SweepRunContext&) {
                return fixture.service.TakeDeferredBySchedule(fixture.schedule);
            }))
        return 908;

    if (!EffectsMutationSweepPassed(
            "Effects.CancelDeferred",
            [] {
                EffectsFaultFixture fixture;
                [[maybe_unused]] const auto ready = fixture.CreateDeferred(true);
                return fixture;
            },
            [](EffectsFaultFixture& fixture, epidemic::tests::allocation_fault::SweepRunContext&) {
                return fixture.service.CancelDeferred(fixture.deferred);
            }))
        return 909;

    if (!EffectsMutationSweepPassed(
            "Effects.CancelDeferredTargeting",
            [] {
                EffectsFaultFixture fixture;
                [[maybe_unused]] const auto ready = fixture.CreateDeferred(true);
                return fixture;
            },
            [](EffectsFaultFixture& fixture, epidemic::tests::allocation_fault::SweepRunContext&) {
                return fixture.service.CancelDeferredTargeting(fixture.a) == 1;
            }))
        return 910;

    // Milestone 2: RestoreSnapshot preserves live state at every observed allocation failure.
    const auto allocation_before = service.CaptureSnapshot();
    const auto restore_report = epidemic::tests::restore_fault::RunObservedRestoreSweep(
        "Effects.RestoreSnapshot",
        2,
        [&] { return allocation_before; },
        [&](auto snapshot) { return service.RestoreSnapshot(std::move(snapshot)); },
        [&] { return service.CaptureSnapshot(); },
        [&](const epidemic::tests::allocation_fault::SweepIteration &iteration, const auto &allocation_baseline) {
            if (iteration.failure == epidemic::tests::allocation_fault::FailureKind::None)
                return true;
            const auto allocation_after = service.CaptureSnapshot();
            const auto diagnostics = service.GetDiagnostics();
            const auto pre_state = epidemic::tests::pre_state::StateComparator("Effects.RestoreSnapshot.pre_state")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::PrimaryRecords,
                                                     allocation_baseline.deferred.size(), allocation_after.deferred.size(),
                                                     "deferred record count")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::RecordPayloads,
                                                     allocation_baseline.deferred.size(), allocation_after.deferred.size(),
                                                     "deferred payload count")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::SecondaryIndexes,
                                                     diagnostics.deferred_effects,
                                                     static_cast<std::uint64_t>(allocation_baseline.deferred.size()),
                                                     "deferred index diagnostics")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::IdGenerators,
                                                     allocation_baseline.execution_ids.next, allocation_after.execution_ids.next,
                                                     "execution id generator next")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::Revisions,
                                                     allocation_baseline.change_epoch, allocation_after.change_epoch,
                                                     "change epoch")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::Journal,
                                                     service.ReadChangesSince(service.LatestChangeCursor()).changes.size(),
                                                     std::size_t{0},
                                                     "latest cursor has no unread changes")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::JournalEpoch,
                                                     allocation_baseline.change_epoch, allocation_after.change_epoch,
                                                     "journal epoch")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::JournalSequence,
                                                     service.LatestChangeCursor(), service.LatestChangeCursor(),
                                                     "latest change cursor stable")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::JournalRetainedRecords,
                                                     allocation_baseline.deferred.size(), allocation_after.deferred.size(),
                                                     "retained restore records")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::JournalLatestCursor,
                                                     service.LatestChangeCursor(), service.LatestChangeCursor(),
                                                     "latest cursor")
                                       .RequireEqual(epidemic::tests::pre_state::StateFacet::PublicReadModels,
                                                     diagnostics.deferred_effects,
                                                     static_cast<std::uint64_t>(allocation_baseline.deferred.size()),
                                                     "public diagnostics read model")
                                       .Finish();
            return pre_state.PassedAndCovers(epidemic::tests::pre_state::RequiredJournaledMutationFacets);
        });
    if (!epidemic::tests::restore_fault::PassedObservedRestoreSweep(restore_report))
        return 902;
    return 0;
}

