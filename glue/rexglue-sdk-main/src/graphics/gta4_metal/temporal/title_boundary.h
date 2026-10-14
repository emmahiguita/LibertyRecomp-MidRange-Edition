#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace gta4::temporal_boundary {
inline constexpr uint32_t kGBufferVtable=0x82044D4C;
inline constexpr uint32_t kDrawSceneVtable=0x82013144;
inline constexpr uint32_t kViewportOffset=176;
inline constexpr uint32_t kCompositeFlagsOffset=2272;
inline constexpr uint32_t kSceneListOffset=2344;
inline constexpr uint32_t kCompositeFlag=0x1000;
constexpr bool DeclaresComposite(uint32_t flags,uint32_t scene_list) {
  return (flags&kCompositeFlag)!=0&&scene_list!=UINT32_MAX;
}
struct Decision {
  uint32_t composite_phase=0;
  const char* reason="no current boundary association";
  explicit operator bool() const { return composite_phase!=0; }
};
// Both retail update methods receive the same render-context argument. Entries
// are observations of that exact device/frame update, never a prior-frame guess.
// Callers serialize access; an overflow or conflicting association fails closed.
class Registry {
 public:
  void GBuffer(uint32_t device,uint64_t sequence,uint32_t phase,uint32_t context) {
    if(!Prepare(device,sequence)||!phase||!context)return;
    for(auto& entry:gbuffers_)if(entry.phase==phase){
      entry.conflict|=entry.context!=context;return;
    }
    if(gbuffers_.size()>=128){overflow_=true;return;}
    gbuffers_.push_back({phase,context,false});
  }
  void Composite(uint32_t device,uint64_t sequence,uint32_t context,uint32_t phase,bool enabled) {
    if(!Prepare(device,sequence)||!phase||!context)return;
    for(auto& entry:composites_)if(entry.context==context){
      entry.conflict|=entry.phase!=phase||entry.enabled!=enabled;return;
    }
    if(composites_.size()>=128){overflow_=true;return;}
    composites_.push_back({phase,context,enabled,false});
  }
  Decision Find(uint32_t device,uint64_t sequence,uint32_t phase) const {
    if(!device||!sequence||device!=device_||sequence!=sequence_)
      return {0,"no current boundary association"};
    if(overflow_)return {0,"boundary association limit"};
    const auto g=std::find_if(gbuffers_.begin(),gbuffers_.end(),[&](const auto& e){return e.phase==phase;});
    if(g==gbuffers_.end())return {0,"primary context was not observed this frame"};
    if(g->conflict)return {0,"ambiguous primary context"};
    const auto c=std::find_if(composites_.begin(),composites_.end(),[&](const auto& e){return e.context==g->context;});
    if(c==composites_.end())return {0,"matching composite was not declared this frame"};
    if(c->conflict)return {0,"ambiguous composite context"};
    if(!c->enabled)return {0,"title bypasses final composite"};
    return {c->phase,"same-frame title composite declared"};
  }
 private:
  struct GBufferEntry {uint32_t phase,context;bool conflict;};
  struct CompositeEntry {uint32_t phase,context;bool enabled,conflict;};
  uint32_t device_=0;uint64_t sequence_=0;bool overflow_=false;
  std::vector<GBufferEntry> gbuffers_;
  std::vector<CompositeEntry> composites_;
  bool Prepare(uint32_t device,uint64_t sequence) {
    if(!device||!sequence)return false;
    if(device!=device_||sequence!=sequence_){
      device_=device;sequence_=sequence;overflow_=false;gbuffers_.clear();composites_.clear();
    }
    return true;
  }
};
Decision DeclaredBoundary(uint8_t* base,uint32_t device,uint64_t sequence,uint32_t gbuffer_phase);
}  // namespace gta4::temporal_boundary
