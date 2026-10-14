#include <catch2/catch_test_macros.hpp>
#include "graphics/gta4_native/native_resolve_descriptor_cache.h"
using namespace rex::graphics::gta4_native;
TEST_CASE("resolve descriptors require the full recording and source lifetime", "[resolve-descriptors]") {
 NativeResolveDescriptorCache<uint64_t> c;
 using Key=decltype(c)::Key;const Key key{11,12,13,14};
 c.Insert(key,100);CHECK(c.Find(key)==0);
 c.BeginScope(1,2);c.Insert(key,100);CHECK(c.Find(key)==100);
 for(Key different: {Key{21,12,13,14},Key{11,22,13,14},Key{11,12,23,14},Key{11,12,13,24}})CHECK(c.Find(different)==0);
 c.BeginScope(1,2);CHECK(c.Find(key)==100);
 c.BeginScope(3,2);CHECK(c.Find(key)==0);
 c.Insert(key,101);c.BeginScope(3,4);CHECK(c.Find(key)==0);
 c.Insert(key,102);c.Reset();c.BeginScope(3,4);CHECK(c.Find(key)==0);
 c.Insert({11,0,13,14},103);CHECK(c.Find({11,0,13,14})==0);
 c.Insert(key,0);CHECK(c.Find(key)==0);
}
TEST_CASE("resolve descriptor lookup storage is bounded and preserves pool ownership", "[resolve-descriptors]") {
 NativeResolveDescriptorCache<uint64_t> c;c.BeginScope(1,2);
 for(uint64_t i=1;i<=10000;++i){decltype(c)::Key key{i,i+1,3,4};CHECK(c.Find(key)==0);c.Insert(key,i+100);CHECK(c.Find(key)==i+100);CHECK(c.size()<=decltype(c)::kCapacity);}
 CHECK(c.statistics().hits==10000);CHECK(c.statistics().inserts==10000);
 c.Reset();CHECK(c.size()==0);
}
