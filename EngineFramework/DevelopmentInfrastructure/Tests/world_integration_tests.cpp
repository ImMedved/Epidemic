#include "Epidemic/GameFramework/WorldIntegration/world_adapters.h"
#include <cstdlib>
#define CHECK(expr)                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(expr))                                                                                                   \
            std::abort();                                                                                              \
    } while (false)
using namespace epidemic::gameplay;
int main()
{
    world::WorldService w;
    environment::EnvironmentService e;
    interaction::InteractionService i;
    queries::GameplayQueryService q;
    facts::GameplayFactsService f;
    auto wt = world::WorldAlterationTypeId::FromString("test.alt");
    CHECK(w.RegisterAlterationType(wt, "test.alt"));
    auto et = environment::EnvironmentLayerTypeId::FromString("test.env");
    CHECK(e.RegisterLayerType(et, "test.env"));
    world::WorldAreaDefinition a;
    a.id = world::WorldAreaId::FromString("test.area");
    a.canonical_name = "test.area";
    a.bounds = {{0, 0, 0}, {1000, 1000, 1000}};
    CHECK(w.RegisterArea(a));
    world_integration::WorldQueryAdapter qa(w, e, i, q);
    CHECK(qa.RegisterProviders());
    world_integration::WorldFactsAdapter fa(w, e, i, f);
    CHECK(fa.RegisterContracts());
    q.Freeze();
    f.Freeze();
    auto qr = q.Execute(world_integration::AreasAtPositionQuery{{10, 10, 10}}, queries::QueryContext{});
    CHECK(qr && qr.Value().value && qr.Value().value->size() == 1);
    auto tx = w.BeginTransaction();
    world::WorldAlterationRecord rec;
    rec.type = wt;
    rec.affected_area = a.bounds;
    CHECK(tx.Create(rec));
    CHECK(tx.Commit());
    auto p = fa.PublishPending();
    CHECK(p && p.Value() == 1);
    auto dispatch = f.Dispatch();
    CHECK(dispatch);
    return 0;
}
