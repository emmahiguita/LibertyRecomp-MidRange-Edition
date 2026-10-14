#include <metal_stdlib>
using namespace metal;

// Matches rex::ui::ImmediateVertex. Scalar fields avoid float-vector padding.
struct ImmediateVertex {
  float x, y, u, v;
  uchar4 color;
};
struct Interpolators {
  float4 position [[position]];
  float2 uv;
  float4 color;
};
vertex Interpolators liberty_ui_vertex(uint index [[vertex_id]],
    device const ImmediateVertex* vertices [[buffer(0)]],
    constant float2& inverse_size [[buffer(1)]]) {
  const auto v = vertices[index];
  Interpolators out;
  out.position = float4(float2(v.x, v.y) * inverse_size * float2(2.0, -2.0) +
                        float2(-1.0, 1.0), 0.0, 1.0);
  out.uv = float2(v.u, v.v);
  out.color = float4(v.color) / 255.0;
  return out;
}
fragment float4 liberty_ui_fragment(Interpolators in [[stage_in]],
    texture2d<float> image [[texture(0)]], sampler image_sampler [[sampler(0)]]) {
  return in.color * image.sample(image_sampler, in.uv);
}

// Host UI colors are authored in sRGB. EDR surfaces require linear values;
// retain SDR white at 1.0 instead of multiplying UI brightness with scene HDR.
fragment float4 liberty_ui_fragment_linear(Interpolators in [[stage_in]],
    texture2d<float> image [[texture(0)]], sampler image_sampler [[sampler(0)]]) {
  float4 color = in.color * image.sample(image_sampler, in.uv);
  color.rgb = select(pow((max(color.rgb, 0.0) + 0.055) / 1.055, float3(2.4)),
                     color.rgb / 12.92, color.rgb <= 0.04045);
  return color;
}

struct PresentInterpolators {
  float4 position [[position]];
  float2 uv;
};
vertex PresentInterpolators liberty_present_vertex(uint index [[vertex_id]]) {
  const float2 uv = float2((index << 1) & 2, index & 2);
  return {float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0), uv};
}
fragment float4 liberty_present_fragment(PresentInterpolators in [[stage_in]],
    texture2d<float> image [[texture(0)]], sampler image_sampler [[sampler(0)]],
    constant uint& dither [[buffer(0)]]) {
  float4 color = image.sample(image_sampler, in.uv);
  if (dither) {
    const uint2 p = uint2(in.position.xy) & 3;
    constexpr uint bayer[16] = {0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
    color.rgb += ((float(bayer[p.y * 4 + p.x]) + 0.5) / 16.0 - 0.5) / 255.0;
  }
  return float4(color.rgb,1.0);
}

// The cache already contains the final color transfer and dither. Read the
// matching pixel exactly, including alpha; UI is composited afterwards.
fragment float4 liberty_cached_present_fragment(PresentInterpolators in [[stage_in]],
    texture2d<float, access::read> image [[texture(0)]]) {
  return image.read(uint2(in.position.xy));
}
