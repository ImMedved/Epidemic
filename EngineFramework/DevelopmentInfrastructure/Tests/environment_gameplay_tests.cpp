#include "Epidemic/GameFramework/Environment/environment.h"
#include <cstdlib>
#define CHECK(expr) do { if (!(expr)) std::abort(); } while (false)
using namespace epidemic::gameplay; using namespace epidemic::gameplay::environment;
int main(){EnvironmentService e;auto t=EnvironmentLayerTypeId::FromString("game.weather");CHECK(e.RegisterLayerType(t,"game.weather"));
 EnvironmentLayer base;base.type=t;base.priority=0;base.blend=EnvironmentBlendPolicy::Override;base.values.temperature_milli_c=10000;base.values.humidity=500;base.persistence=EnvironmentPersistence::Persistent;CHECK(e.AddLayer(base));
 EnvironmentLayer rain;rain.type=t;rain.priority=10;rain.blend=EnvironmentBlendPolicy::Add;rain.bounds=EnvironmentAabb{{0,0,0},{1000,1000,1000}};rain.values.precipitation=700;rain.values.humidity=300;CHECK(e.AddLayer(rain));
 auto s=e.Sample({100,100,100},GameplayTimePoint{1});CHECK(s.values.temperature_milli_c==10000);CHECK(s.values.humidity==800);CHECK(s.values.precipitation==700);CHECK(s.layers.size()==2);
 auto outside=e.Sample({5000,0,0},GameplayTimePoint{1});CHECK(outside.values.precipitation==0);CHECK(outside.layers.size()==1);
 auto snap=e.CaptureSnapshot();CHECK(snap.layers.size()==1);e.Freeze();CHECK(!e.RegisterLayerType(EnvironmentLayerTypeId::FromString("x"),"x"));return 0;}
