#include <metal_stdlib>
using namespace metal;

// Opt-in diagnostic only: integer classification avoids fast-math assumptions
// about NaN/Inf. Each invocation accumulates locally before touching atomics.
kernel void liberty_trace_finite(texture2d<float,access::read> image [[texture(0)]],
    device atomic_uint* result [[buffer(0)]], uint tid [[thread_position_in_grid]],
    uint stride [[threads_per_grid]]) {
  const uint width=image.get_width(), count=width*image.get_height();
  uint nan_count=0,inf_count=0,first_nan=0xffffffffu,first_inf=0xffffffffu;
  for (uint i=tid;i<count;i+=stride) {
    const uint4 bits=as_type<uint4>(image.read(uint2(i%width,i/width)));
    const uint4 magnitude=bits&0x7fffffffu;
    if (any(magnitude>0x7f800000u)) { ++nan_count; first_nan=min(first_nan,i); }
    if (any(magnitude==0x7f800000u)) { ++inf_count; first_inf=min(first_inf,i); }
  }
  if (nan_count) {
    atomic_fetch_add_explicit(result,nan_count,memory_order_relaxed);
    atomic_fetch_min_explicit(result+2,first_nan,memory_order_relaxed);
  }
  if (inf_count) {
    atomic_fetch_add_explicit(result+1,inf_count,memory_order_relaxed);
    atomic_fetch_min_explicit(result+3,first_inf,memory_order_relaxed);
  }
}
kernel void liberty_trace_nonfinite_sample(texture2d<float,access::read> image [[texture(0)]],
    device atomic_uint* result [[buffer(0)]]) {
  for (uint kind=0;kind<2;++kind) {
    const uint index=atomic_load_explicit(result+2+kind,memory_order_relaxed);
    if (index==0xffffffffu) continue;
    const uint4 bits=as_type<uint4>(image.read(uint2(index%image.get_width(),index/image.get_width())));
    for (uint channel=0;channel<4;++channel)
      atomic_store_explicit(result+4+kind*4+channel,bits[channel],memory_order_relaxed);
  }
}

// These helpers cover API operations with no Metal render-encoder equivalent
// (partial attachment clears and a selected depth/stencil sample transfer).
// Game, post-processing and presentation arithmetic comes from existing sources.
fragment float4 liberty_clear_color(constant float4& value [[buffer(0)]]) {
  return value;
}
struct DepthValue { float depth [[depth(any)]]; };
fragment DepthValue liberty_clear_depth(constant float& value [[buffer(0)]]) {
  return {value};
}
fragment void liberty_clear_stencil() {}

struct DepthStencilValue {
  float depth [[depth(any)]];
  uint stencil [[stencil]];
};
struct CopyDepthConstants {
  int2 source_origin;
  int2 destination_origin;
  uint source_guest_sample_type;
  uint requested_guest_sample_type;
  uint destination_guest_sample_type;
  uint sample_select;
  uint mode;
  uint physical_source_sample_type;
  uint physical_destination_sample_type;
  uint flags;
  uint2 source_extent;
  uint2 destination_extent;
};
inline uint2 depth_source_coordinate(float4 position,constant CopyDepthConstants& c,uint2 size) {
  float2 relative=position.xy-float2(c.destination_origin);
  float2 scale=float2(c.source_extent)/max(float2(c.destination_extent),float2(1));
  return uint2(clamp(int2(floor(relative*scale))+c.source_origin,int2(0),int2(size)-1));
}
fragment DepthStencilValue liberty_copy_depth_stencil(float4 position [[position]],
    texture2d<float> depth [[texture(0)]],texture2d<uint> stencil [[texture(1)]],
    constant CopyDepthConstants& constants [[buffer(0)]]) {
  uint2 coordinate=depth_source_coordinate(position,constants,uint2(depth.get_width(),depth.get_height()));
  return {depth.read(coordinate).x,stencil.read(coordinate).x};
}
fragment DepthStencilValue liberty_copy_depth_stencil_msaa(float4 position [[position]],
    texture2d_ms<float> depth [[texture(0)]],texture2d_ms<uint> stencil [[texture(1)]],
    constant CopyDepthConstants& constants [[buffer(0)]]) {
  uint2 coordinate=depth_source_coordinate(position,constants,uint2(depth.get_width(),depth.get_height()));
  uint sample=min(constants.sample_select,depth.get_num_samples()-1);
  return {depth.read(coordinate,sample).x,stencil.read(coordinate,sample).x};
}
