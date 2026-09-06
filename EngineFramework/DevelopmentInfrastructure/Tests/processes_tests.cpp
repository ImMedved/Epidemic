#include "Epidemic/Foundation/error.h"
#include "Epidemic/GameFramework/Processes/processes.h"

using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::processes;

namespace
{
class MockInput final : public IProcessInputProvider
{
  public:
    Fixed available = 100;
    Fixed consumed = 0;
    [[nodiscard]] foundation::Result<void> Validate(const ProcessInputDefinition &, const StartProcessRequest &, ProcessInstanceId) override
    {
        return foundation::Result<void>::Success();
    }
    [[nodiscard]] foundation::Result<ReservedProcessInput> Reserve(const ProcessInputDefinition &input,
                                                                   const StartProcessRequest &,
                                                                   ProcessInstanceId instance) override
    {
        if (available < input.amount)
            return foundation::Result<ReservedProcessInput>::Failure(foundation::Error::Create("shortage", "shortage"));
        ReservedProcessInput r;
        r.id = ProcessReservationId::FromRaw(instance.value.High(), input.id.value.Raw());
        r.input = input.id;
        r.type = input.type;
        r.amount = input.amount;
        return foundation::Result<ReservedProcessInput>::Success(r);
    }
    [[nodiscard]] foundation::Result<void> Consume(const ReservedProcessInput &r, GameplayContext) override
    {
        available -= r.amount;
        consumed += r.amount;
        return foundation::Result<void>::Success();
    }
    [[nodiscard]] foundation::Result<void> Release(const ReservedProcessInput &, GameplayContext) override
    {
        return foundation::Result<void>::Success();
    }
};
class MockOutput final : public IProcessOutputHandler
{
  public:
    Fixed produced = 0;
    [[nodiscard]] bool Supports(ProcessOutputTypeId type) const noexcept override
    {
        return type == ProcessOutputTypeId::FromString("test.output");
    }
    [[nodiscard]] foundation::Result<PreparedProcessOutput> Prepare(const ProcessOutputDefinition &output, const ProcessInstance &,
                                                                    GameplayContext) override
    {
        PreparedProcessOutput prepared;
        prepared.output = output.id;
        prepared.type = output.type;
        prepared.delivery = output.delivery;
        std::vector<std::byte> encoded(8);
        const auto raw = static_cast<std::uint64_t>(output.amount);
        for (std::size_t i = 0; i < 8; ++i) encoded[i] = static_cast<std::byte>((raw >> (i * 8)) & 0xffu);
        prepared.provider_token = RegisteredPayload::FromVersioned(TypeId::FromString("test.prepared.amount"), 1, std::move(encoded));
        return foundation::Result<PreparedProcessOutput>::Success(std::move(prepared));
    }
    [[nodiscard]] foundation::Result<void> Commit(const PreparedProcessOutput &output, const ProcessInstance &, GameplayContext) override
    {
        if (output.provider_token.type != TypeId::FromString("test.prepared.amount") ||
            output.provider_token.schema_version != 1 || output.provider_token.bytes.size() != 8)
            return foundation::Result<void>::Failure(foundation::Error::Create("invalid", "invalid prepared output"));
        std::uint64_t raw = 0;
        for (std::size_t i = 0; i < 8; ++i) raw |= static_cast<std::uint64_t>(std::to_integer<unsigned int>(output.provider_token.bytes[i])) << (i * 8);
        produced += static_cast<Fixed>(raw);
        return foundation::Result<void>::Success();
    }
    [[nodiscard]] foundation::Result<void> Cancel(const PreparedProcessOutput &, const ProcessInstance &, GameplayContext) override
    {
        return foundation::Result<void>::Success();
    }
};
} // namespace

int main()
{
    ProcessesService service;
    MockInput input;
    MockOutput output;
    service.SetInputProvider(&input);
    service.AddOutputHandler(&output);

    ProcessDefinition def;
    def.canonical_name = "test.process.smith";
    def.kind = ProcessKindId::FromString("test.kind");
    def.timing = ProcessTimingPolicy::Timed;
    def.persistence = ProcessPersistencePolicy::Persistent;
    def.steps.push_back(
        {ProcessStepId::FromString("test.step"), TypeId::FromString("test.step.kind"), GameplayDuration{10}, {}, {}});
    auto def_id = service.RegisterDefinition(def);
    if (!def_id)
        return 1;
    ProcessRecipe recipe;
    recipe.canonical_name = "test.recipe.sword";
    recipe.process = def_id.Value();
    recipe.inputs.push_back({ProcessInputId::FromString("test.input.iron"),
                             ProcessInputTypeId::FromString("test.input"),
                             20,
                             InputConsumptionPolicy::ReserveThenConsume,
                             {},
                             {}});
    recipe.outputs.push_back({ProcessOutputId::FromString("test.output.sword"),
                              ProcessOutputTypeId::FromString("test.output"),
                              1,
                              OutputDeliveryPolicy::OnCompletion,
                              {}});
    auto recipe_id = service.RegisterRecipe(recipe);
    if (!recipe_id)
        return 2;
    service.Freeze();
    GameplayObjectRef actor{GameplayDomainId::FromString("test"), GameplayObjectId::FromString("actor")};
    auto instance = service.StartProcess({recipe_id.Value(), actor, {}, {}, GameplayTimePoint{0}, 7, {}});
    if (!instance)
        return 3;
    if (service.Complete(instance.Value(), GameplayTimePoint{5}))
        return 4;
    auto done = service.Complete(instance.Value(), GameplayTimePoint{10});
    if (!done || input.consumed != 20 || output.produced != 1)
        return 5;
    auto snapshot = service.CaptureSnapshot();
    ProcessesService restored;
    auto rdef = restored.RegisterDefinition(def);
    auto rrec = restored.RegisterRecipe(recipe);
    if (!rdef || !rrec)
        return 8;
    restored.Freeze();
    restored.SetInputProvider(&input);
    restored.AddOutputHandler(&output);
    if (!restored.RestoreSnapshot(std::move(snapshot)))
        return 6;
    if (restored.GetDiagnostics().completed_processes != 1)
        return 7;
    return 0;
}
