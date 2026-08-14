#include "Epidemic/GameFramework/World/world.h"
#include <cstdlib>
#define CHECK(expr) do { if (!(expr)) std::abort(); } while (false)
using namespace epidemic::gameplay;
using namespace epidemic::gameplay::world;
int main(){
 WorldService w; auto at=WorldAlterationTypeId::FromString("game.test.terrain"); auto ft=WorldFeatureTypeId::FromString("game.test.road");
 CHECK(w.RegisterAlterationType(at,"game.test.terrain")); CHECK(w.RegisterFeatureType(ft,"game.test.road"));
 WorldRegionDefinition r; r.id=WorldRegionId::FromString("region.a"); r.canonical_name="region.a"; CHECK(w.RegisterRegion(r));
 WorldAreaDefinition a{WorldAreaId::FromString("area.a"),"area.a",{{0,0,0},{10000,10000,10000}},{r.id},{},{},{}}; CHECK(w.RegisterArea(a));
 LocationDefinition l{LocationId::FromString("loc.a"),"loc.a",{100,100,100},{r.id},{a.id},{},{}}; CHECK(w.RegisterLocation(l));
 CHECK(w.FindAreasAt({500,500,500}).size()==1); CHECK(w.FindLocationsInArea(a.id).size()==1);
 w.Freeze(); WorldRegionDefinition frozen; frozen.id=WorldRegionId::FromString("x"); frozen.canonical_name="x"; CHECK(!w.RegisterRegion(frozen));
 auto tx=w.BeginTransaction(); WorldAlterationRecord rec;rec.type=at;rec.affected_area={{0,0,0},{1000,1000,1000}};rec.persistence=WorldAlterationPersistence::Persistent;auto id=tx.Create(rec);CHECK(id);CHECK(tx.Commit());
 CHECK(w.FindAlteration(id.Value())!=nullptr); auto snap=w.CaptureSnapshot(); CHECK(snap.alterations.size()==1);
 WorldService restored; CHECK(restored.RegisterAlterationType(at,"game.test.terrain")); CHECK(restored.RegisterFeatureType(ft,"game.test.road")); restored.Freeze(); CHECK(restored.RestoreSnapshot(snap)); CHECK(restored.FindAlteration(id.Value())!=nullptr);
 return 0;
}
