#include <rex/ui/publication_progress.h>
#include "graphics/gta4_metal/resolve_reuse.h"
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <thread>
static unsigned checks;
static void Check(bool ok) { ++checks; if (!ok) throw std::runtime_error("policy check failed"); }
int main() {
  try {
    using rex::ui::PublicationProgress;
    PublicationProgress p;
    Check(!p.pending()); p.Publish(1); Check(p.pending()); p.Accept(1); Check(!p.pending());
    p.Publish(1); Check(!p.pending()); p.Publish(2); p.Accept(1); Check(p.pending());
    p.Accept(3); p.Publish(3); Check(!p.pending()); p.Publish(2); Check(!p.pending());
    p.Publish(4); Check(p.pending()); p.Accept(4); Check(!p.pending());
    PublicationProgress racing;
    std::atomic<bool> go{false};
    std::thread producer([&] { for (; !go.load(); ) std::this_thread::yield();
      for (uint64_t i = 1; i <= 100000; ++i) racing.Publish(i); });
    std::thread consumer([&] { go = true;
      for (uint64_t i = 1; i <= 100000; ++i) { racing.Accept(i); (void)racing.pending(); } });
    producer.join(); consumer.join(); Check(!racing.pending());
    using namespace rex::graphics::gta4_metal;
    ResolveReuseKey key{}; key.recording=1; key.source_image=2; key.source_generation=3;
    key.source_writer=4; key.destination_image=5; key.destination_generation=6;
    ResolveReuseRecord record{key,7}; Check(record.Matches(key,7));
    Check(!record.Matches(key,0)); Check(!record.Matches(key,8));
    for (unsigned field=0;field<11;++field) {
      auto different=key;
      switch(field) {
        case 0: ++different.recording; break; case 1: ++different.source_image; break;
        case 2: ++different.source_generation; break; case 3: ++different.source_writer; break;
        case 4: ++different.destination_image; break; case 5: ++different.destination_generation; break;
        case 6: ++different.level; break; case 7: ++different.slice; break;
        case 8: ++different.source_format; break; case 9: ++different.destination_format; break;
        case 10: ++different.direct; break;
      }
      Check(!record.Matches(different,7));
    }
    for (size_t i=0;i<key.conversion.size();++i) {auto v=key;++v.conversion[i];Check(!record.Matches(v,7));}
    for (size_t i=0;i<key.image_extents.size();++i) {auto v=key;++v.image_extents[i];Check(!record.Matches(v,7));}
    auto alias=key;alias.destination_image=alias.source_image;Check(!alias.valid());
    auto abandoned=key;abandoned.recording=0;Check(!abandoned.valid());
    for(bool direct:{false,true})for(bool full:{false,true})for(bool old:{false,true})
      Check(MergeResolveInitialization(direct,full,old)==(!direct&&!full&&!old));
    std::printf("policy_pass checks=%u\n",checks);return 0;
  } catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
}
