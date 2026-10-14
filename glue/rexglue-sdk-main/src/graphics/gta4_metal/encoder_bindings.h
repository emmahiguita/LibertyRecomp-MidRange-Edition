#pragma once
#import <Metal/Metal.h>
#include <array>
#include <bit>
#include <cstdint>
#include <optional>
namespace rex::graphics::gta4_metal {
// Valid only for one render encoder. No state is inherited across an encoder
// boundary, no Metal hazard tracking is disabled, and buffers remain immutable.
class EncoderBindings {
 public:
  uint64_t calls = 0, skipped = 0, residency_calls = 0, residency_skipped = 0;
  void Reset(bool enabled) {
    enabled_ = enabled;
    pipeline_ = nil; depth_ = nil;
    for (auto& value : vertices_) value = {};
    arguments_ = {}; push_.reset(); viewport_.reset(); scissor_.reset();
    cull_.reset(); winding_.reset(); fill_.reset(); clip_.reset(); stencil_.reset();
    blend_.reset(); bias_.reset();
    resources_.fill(0);
  }
  void Pipeline(id<MTLRenderCommandEncoder> encoder, id<MTLRenderPipelineState> value) {
    if (enabled_ && pipeline_ == value) {++skipped; return;}
    pipeline_ = value; ++calls; [encoder setRenderPipelineState:value];
  }
  void Depth(id<MTLRenderCommandEncoder> encoder, id<MTLDepthStencilState> value) {
    if (enabled_ && depth_ == value) {++skipped; return;}
    depth_ = value; ++calls; [encoder setDepthStencilState:value];
  }
  void Vertex(id<MTLRenderCommandEncoder> encoder, id<MTLBuffer> value, NSUInteger offset, size_t index) {
    auto& previous = vertices_.at(index);
    if (enabled_ && previous.known && previous.buffer == value && previous.offset == offset) {++skipped; return;}
    previous = {value, offset, true}; ++calls; [encoder setVertexBuffer:value offset:offset atIndex:index];
  }
  void Arguments(id<MTLRenderCommandEncoder> encoder, id<MTLBuffer> value,
                 const std::array<NSUInteger, 5>& offsets) {
    if (enabled_ && arguments_.known && arguments_.buffer == value && arguments_.offsets == offsets) {skipped += 2; return;}
    arguments_ = {value, offsets, true};
    std::array<id<MTLBuffer> __unsafe_unretained, 5> buffers{}; buffers.fill(value); calls += 2;
    [encoder setVertexBuffers:buffers.data() offsets:offsets.data() withRange:NSMakeRange(0,5)];
    [encoder setFragmentBuffers:buffers.data() offsets:offsets.data() withRange:NSMakeRange(0,5)];
  }
  void Push(id<MTLRenderCommandEncoder> encoder, const std::array<uint64_t, 3>& value) {
    if (!Changed(push_, value, 2)) return;
    [encoder setVertexBytes:value.data() length:sizeof(value) atIndex:8];
    [encoder setFragmentBytes:value.data() length:sizeof(value) atIndex:8];
  }
  // All callers here declare Read for both stages. Do not use this helper for
  // writes or a different stage mask without extending the cache's key.
  // Keep the caller's pointer ownership qualification. A concrete autoreleasing
  // pointer parameter otherwise creates an ARC bridge array per draw.
  template<class ResourcePointer>
  void ReadResources(id<MTLRenderCommandEncoder> encoder, ResourcePointer values, size_t count) {
    // Call-local views borrow the caller's resources until Metal retains them.
    // Neither the temporary batch nor the identity memo needs separate ownership.
    std::array<id<MTLResource> __unsafe_unretained, 32> pending{};
    size_t size = 0;
    auto flush = [&] {
      if (!size) return;
      [encoder useResources:pending.data() count:size usage:MTLResourceUsageRead
          stages:MTLRenderStageVertex|MTLRenderStageFragment];
      ++residency_calls; size = 0;
    };
    for (size_t i=0; i<count; ++i) {
      if (!values[i]) continue;
      if (!NeedResource(values[i])) {++residency_skipped; continue;}
      pending[size++] = values[i];
      if (size == pending.size()) flush();
    }
    flush();
  }
  void Cull(id<MTLRenderCommandEncoder> e, MTLCullMode v) { if(Changed(cull_,v)) [e setCullMode:v]; }
  void Winding(id<MTLRenderCommandEncoder> e, MTLWinding v) {if(Changed(winding_,v)) [e setFrontFacingWinding:v];}
  void Fill(id<MTLRenderCommandEncoder> e, MTLTriangleFillMode v) {if(Changed(fill_,v)) [e setTriangleFillMode:v];}
  void Clip(id<MTLRenderCommandEncoder> e, MTLDepthClipMode v) {if(Changed(clip_,v)) [e setDepthClipMode:v];}
  void Stencil(id<MTLRenderCommandEncoder> e, uint32_t front, uint32_t back) {
    if (Changed(stencil_,std::array<uint32_t,2>{front,back})) [e setStencilFrontReferenceValue:front backReferenceValue:back];
  }
  void Blend(id<MTLRenderCommandEncoder> e, float r,float g,float b,float a) {
    if(Changed(blend_,std::bit_cast<std::array<uint32_t,4>>(std::array<float,4>{r,g,b,a})))
      [e setBlendColorRed:r green:g blue:b alpha:a];
  }
  void Viewport(id<MTLRenderCommandEncoder> e, MTLViewport v) {
    const std::array<double,6> fields{v.originX,v.originY,v.width,v.height,v.znear,v.zfar};
    if(Changed(viewport_,std::bit_cast<std::array<uint64_t,6>>(fields))) [e setViewport:v];
  }
  void Scissor(id<MTLRenderCommandEncoder> e, MTLScissorRect v) {
    if(Changed(scissor_,std::array<NSUInteger,4>{v.x,v.y,v.width,v.height})) [e setScissorRect:v];
  }
  void Bias(id<MTLRenderCommandEncoder> e, float constant,float slope,float clamp) {
    if(Changed(bias_,std::bit_cast<std::array<uint32_t,3>>(std::array<float,3>{constant,slope,clamp})))
      [e setDepthBias:constant slopeScale:slope clamp:clamp];
  }
 private:
  template<class T> bool Changed(std::optional<T>& old,const T& value,uint64_t count=1) {
    if(enabled_ && old && *old==value) {skipped+=count;return false;}
    old=value;calls+=count;return true;
  }
  bool NeedResource(id<MTLResource> value) {
    if (!enabled_) return true;
    const auto pointer = reinterpret_cast<uintptr_t>((__bridge void*)value);
    size_t slot = ((pointer >> 4) ^ (pointer >> 13)) & (resources_.size()-1);
    const size_t home=slot;
    for(size_t visited=0;visited<8;++visited,slot=(slot+1)&(resources_.size()-1)) {
      if(resources_[slot]==pointer) return false;
      if(!resources_[slot]) {resources_[slot]=pointer;return true;}
    }
    // A crowded memo must not scan every slot for every subsequent resource.
    // Replacement loses only a skip opportunity, never a residency declaration.
    resources_[home]=pointer;
    return true;
  }
  bool enabled_ = true;
  id<MTLRenderPipelineState> pipeline_ = nil;
  id<MTLDepthStencilState> depth_ = nil;
  struct VertexBinding {id<MTLBuffer> buffer=nil;NSUInteger offset=0;bool known=false;};
  struct ArgumentBinding {id<MTLBuffer> buffer=nil;std::array<NSUInteger,5> offsets{};bool known=false;};
  std::array<VertexBinding,31> vertices_{};
  ArgumentBinding arguments_{};
  // A recorded resource is retained by the command buffer, so its identity
  // cannot be recycled in this encoder. This table is a memo, not another owner.
  std::array<uintptr_t,256> resources_{};
  std::optional<std::array<uint64_t,3>> push_;
  std::optional<std::array<uint64_t,6>> viewport_;
  std::optional<std::array<NSUInteger,4>> scissor_;
  std::optional<MTLCullMode> cull_;
  std::optional<MTLWinding> winding_;
  std::optional<MTLTriangleFillMode> fill_;
  std::optional<MTLDepthClipMode> clip_;
  std::optional<std::array<uint32_t,2>> stencil_;
  std::optional<std::array<uint32_t,4>> blend_;
  std::optional<std::array<uint32_t,3>> bias_;
};
}  // namespace rex::graphics::gta4_metal
