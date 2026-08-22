#include "Epidemic/GameFramework/Traversal/traversal.h"
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::traversal;
int main()
{
    TraversalService s;
    auto walk = TraversalModeId::FromString("game.walk");
    auto swim = TraversalModeId::FromString("game.swim");
    auto cap = TraversalCapabilityId::FromString("game.can_swim");
    TraversalModeDefinition w;
    w.id = walk;
    w.canonical_name = "game.walk";
    if (!s.RegisterMode(w))
        return 1;
    TraversalModeDefinition sw;
    sw.id = swim;
    sw.canonical_name = "game.swim";
    sw.required_capability = cap;
    if (!s.RegisterMode(sw))
        return 2;
    TraversalProfile p;
    p.id = TraversalProfileId::FromString("game.human");
    p.canonical_name = "game.human";
    p.default_mode = walk;
    p.allowed_modes = {walk, swim};
    p.capabilities = {cap};
    if (!s.RegisterProfile(p))
        return 3;
    s.Freeze();
    GameplayObjectRef actor{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("actor")};
    if (!s.AssignProfile(actor, p.id))
        return 4;
    auto changed = s.ChangeMode({actor, swim, {}});
    if (!changed || changed.Value().kind != TraversalResultKind::Accepted)
        return 5;
    GameplayObjectRef horse{GameplayDomainId::FromString("test.entity"), GameplayObjectId::FromString("horse")};
    auto role = TraversalCarrierRoleId::FromString("game.rider");
    if (!s.BoardCarrier(actor, horse, walk, role))
        return 8;
    if (s.FindCarrierPassengers(horse).size() != 1)
        return 9;
    if (!s.Disembark(actor))
        return 10;
    auto session = s.StartSession(actor, swim, {}, {}, {});
    if (!session)
        return 6;
    if (!s.FindState(actor) || !s.FindState(actor)->active_session)
        return 7;
    auto snap = s.CaptureSnapshot();
    TraversalService restored;
    if (!restored.RegisterMode(w))
        return 15;
    if (!restored.RegisterMode(sw))
        return 16;
    if (!restored.RegisterProfile(p))
        return 17;
    restored.Freeze();
    if (!restored.RestoreSnapshot(std::move(snap)))
        return 11;
    if (!restored.FindSession(session.Value()))
        return 12;
    if (!restored.CompleteSession(session.Value()))
        return 13;
    if (restored.GetDiagnostics().active_sessions != 0)
        return 14;
    return 0;
}
