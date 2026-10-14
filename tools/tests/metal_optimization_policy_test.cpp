#include "graphics/gta4_metal/pending_clear.h"
#include "graphics/gta4_metal/sampler_key.h"
#include "graphics/gta4_metal/profile_records.h"
#include "graphics/gta4_metal/frame_work_qos.h"
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <unordered_map>
using namespace rex::graphics::gta4_metal;
static size_t checks=0;
static void Check(bool condition){++checks;if(!condition)throw std::runtime_error("policy assertion "+std::to_string(checks));}
int main(){try {
  for(uint32_t initial=0;initial<8;++initial)for(uint32_t next=1;next<8;++next) {
    PendingClear state;state.aspects=initial;state.depth=0.75f;state.stencil=0x11;
    state.Merge(next,true,initial!=0,{1,2,3,4},0.5f,0xABCD);
    Check((state.aspects&next)==next);
    if(next&2)Check(state.depth==0.5f);
    if(next&4)Check(state.stencil==0xCD);
    if(!initial)Check((state.aspects&6)==6);
  }
  std::unordered_map<SamplerKey,uint32_t,SamplerKeyHash> values;
  for(size_t field=0;field<10;++field)for(uint32_t value=1;value<1024;++value) {
    SamplerKey key;key.words[field]=value;Check(!values.contains(key));values.emplace(key,value);
    Check(values.at(key)==value);
  }
  ScopeProfile scope;scope.Begin(3,4,256,128);
  for(uint32_t i=0;i<10000;++i)scope.Draw({1,2,3,i%4,1},3,0);
  Check(scope.members().size()==4 && scope.draws()==10000 && scope.overflow()==0);
  for(uint64_t i=10;i<100;++i)scope.Draw({i,2,3,1,1},0,6);
  Check(scope.members().size()==64 && scope.overflow()>0);
  scope.End();scope.Draw({},3,0);Check(scope.draws()==10090);
  ResolveProfile resolves;resolves.Begin();ResolveReuseKey key;key.destination_generation=5;key.level=1;key.slice=2;
  resolves.Write(key,7,9,10,0x100,false);
  resolves.Read(5,1,2,6,1);Check(!resolves.records()[0].first_read);
  resolves.Read(5,1,2,7,2);Check(resolves.records()[0].first_read && resolves.records()[0].read_kind==2);
  for(size_t i=0;i<300;++i)resolves.Write(key,8,9,10,0,false);
  Check(resolves.records().size()==256 && resolves.overflow()>0);
  resolves.Begin();Check(resolves.records().empty() && !resolves.overflow());
  std::thread worker([]{
    const auto before=qos_class_self();FrameWorkQos qos;
    for(size_t i=0;i<1024;++i) {
      if(!qos.Begin(true))throw std::runtime_error("QoS start failed");
      if(!qos.Begin(true))throw std::runtime_error("QoS repeated start failed");
      if(qos_class_self()!=before)throw std::runtime_error("Override changed base QoS");
      qos.End();if(qos.active()||qos.last_end_error())throw std::runtime_error("QoS cleanup failed");
    }
    if(qos.begun()!=1024||qos.ended()!=1024||qos.failed())throw std::runtime_error("QoS unbalanced");
  });worker.join();
  std::printf("optimization_policy=passed checks=%zu qos_cycles=1024\n",checks);
  return 0;
} catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
