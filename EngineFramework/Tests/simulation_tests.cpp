#include "Epidemic/GameFramework/Simulation/simulation.h"
using namespace epidemic;
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::simulation;
namespace { class Exec final : public ISimulationLayerExecutor { public: int commits=0; SimulationLayerId id{SimulationLayerId::FromString("test.layer")}; [[nodiscard]] SimulationLayerId Layer() const noexcept override { return id; } [[nodiscard]] foundation::Result<SimulationLayerSummary> Prepare(const SimulationTask&) override { return foundation::Result<SimulationLayerSummary>::Success({id, SimulationTaskState::Completed, 3, {1}}); } [[nodiscard]] foundation::Result<void> Commit(const SimulationTask&, const SimulationLayerSummary&) override { ++commits; return foundation::Result<void>::Success(); } }; }
int main()
{
    SimulationService s; Exec e;
    auto region = s.RegisterRegion({SimulationRegionId::FromString("test.region"), "test.region", {GameplayDomainId::FromString("test"), GameplayObjectId::FromString("area")}, SimulationDetailLevel::Abstract, {}, {}, {}}); if (!region) return 1;
    auto layer = s.RegisterLayer({e.id, "test.layer", {}, 10, SimulationMaterializationPolicy::AbstractCapable, {}}, &e); if (!layer) return 2;
    s.Freeze();
    auto summary = s.SimulateInterval(region.Value(), GameplayTimePoint{0}, GameplayTimePoint{10}); if (!summary || e.commits != 1) return 3;
    auto summaries = s.FindSummaries(region.Value()); if (summaries.size() != 1 || summaries[0].layers.size() != 1 || summaries[0].layers[0].operations != 3) return 4;
    auto snap = s.CaptureSnapshot(); SimulationService restored; Exec e2; auto rlayer = restored.RegisterLayer({e2.id, "test.layer", {}, 10, SimulationMaterializationPolicy::AbstractCapable, {}}, &e2); if (!rlayer) return 7;
    if (!restored.RestoreSnapshot(std::move(snap))) return 5;
    if (restored.FindSummaries(region.Value()).size() != 1) return 6;
    return 0;
}
