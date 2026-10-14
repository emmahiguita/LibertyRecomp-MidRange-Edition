// Xbox PS_GTADepthEffects (rage_postfx_ps19.bin) with its original depth,
// HDR and depth-desaturation behavior preserved. A valid environmental snapshot
// enables FusionShaders' radial height integration, sky-derived fog color, and
// below-horizon sky treatment, using the Xbox reciprocal depth convention.

#define SPEC_CONSTANT_ALPHA_TEST (1u << 1u)
#define SPEC_CONSTANT_ALPHA_TEST_FUNCTION_SHIFT 8u
#define SPEC_CONSTANT_ALPHA_TEST_FUNCTION_MASK 7u

bool AlphaTestPass(float alpha, float reference, uint function) {
  if (function == 0u) return false;
  if (function == 1u) return alpha < reference;
  if (function == 2u) return alpha == reference;
  if (function == 3u) return alpha <= reference;
  if (function == 4u) return alpha > reference;
  if (function == 5u) return isnan(alpha) || isnan(reference) || alpha != reference;
  if (function == 6u) return alpha >= reference;
  if (function == 7u) return true;
  return false;
}

struct PushConstants {
  uint64_t VertexShaderConstants;
  uint64_t PixelShaderConstants;
  uint64_t SharedConstants;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> g_PushConstants;
[[vk::constant_id(0)]] const uint g_SpecConstants = 0;

Texture2D<float4> g_Texture2DDescriptorHeap[] : register(t0, space0);
SamplerState g_SamplerDescriptorHeap[] : register(s0, space4);

struct Interpolators {
  float4 iPos : SV_Position;
  float4 iTexCoord0 : TEXCOORD0;
  float4 iTexCoord1 : TEXCOORD1;
  float4 iTexCoord2 : TEXCOORD2;
};

struct PixelShaderOutput {
  float4 oC0 : SV_Target0;
};

#ifndef XENOS_RECOMP_LATE_FRAGMENT_TESTS
[earlydepthstencil]
#endif
PixelShaderOutput shaderMain(Interpolators input) {
  const uint depth_texture_index =
      vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 0x000);
  const uint hdr_texture_index =
      vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 0x004);
  const uint depth_sampler_index =
      vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 0x1A0);
  const uint hdr_sampler_index =
      vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 0x1A4);
  // Preserve explicit LOD sampling while applying the guest sampler bias,
  // already clamped by the host. Depth uses guest slot zero; HDR uses slot one.
  const float depth_sampler_lod_bias =
      vk::RawBufferLoad<float>(g_PushConstants.SharedConstants + 752);
  const float hdr_sampler_lod_bias =
      vk::RawBufferLoad<float>(g_PushConstants.SharedConstants + 756);
  const float4 dofProj =
      vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 0xD00, 0x10);
  const float4 gDepthFxParams =
      vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 0x100, 0x10);
  const float4 globalFogParams =
      vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 0x290, 0x10);
  const float4 globalFogColor =
      vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 0x2A0, 0x10);
  const float4 globalFogColorN =
      vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 0x2B0, 0x10);

  Texture2D<float4> depth_texture = g_Texture2DDescriptorHeap[depth_texture_index];
  Texture2D<float4> hdr_texture = g_Texture2DDescriptorHeap[hdr_texture_index];
  SamplerState depth_sampler = g_SamplerDescriptorHeap[depth_sampler_index];
  SamplerState hdr_sampler = g_SamplerDescriptorHeap[hdr_sampler_index];

  float4 r0 = input.iTexCoord0;
  float4 r1 = input.iTexCoord1;
  float4 r2 = 0.0;
  float4 r3 = 0.0;
  float4 r4 = 0.0;
  float4 r5 = 0.0;
  float4 r6 = 0.0;

  r3.xyz = hdr_texture.SampleLevel(hdr_sampler, r0.xy, 0.0 + hdr_sampler_lod_bias).xyz;
  r3.w = 1.0;
  r0.w = depth_texture.SampleLevel(depth_sampler, r0.xy, 0.0 + depth_sampler_lod_bias).x;
  r6.x = 1.0 - globalFogParams.w;
  r4.xy = gDepthFxParams.xy - 1.0;
  r0.z = dofProj.y * dofProj.x;
  r6.yzw = globalFogColor.xyz - globalFogColorN.xyz;
  r0.x = globalFogParams.y - globalFogParams.x;
  r1.yz = r0.ww * dofProj.yx;
  r1.x = gDepthFxParams.w - gDepthFxParams.z;
  r0.y = r1.y - r1.z;
  r1.w = rcp(globalFogParams.x);
  r0.y += dofProj.x;
  r1.x = rcp(r1.x);
  r1.z = r0.w == 0.0;
  r0.y = rcp(r0.y);
  r0.y = r0.z * r0.y;
  const float decoded_depth = r0.y;
  const bool is_geometry = r0.w != 0.0;
  r1.y = rcp(r0.x);
  r3.w = dot(float4(0.0722, 0.2125, 0.7154, 1.0e-7), r3.zxyw);
  r0.z = r0.y - globalFogParams.x;
  r2.w = dot(r3.zxy, float3(0.0722, 0.2125, 0.7154));
  r0.x = gDepthFxParams.w - r0.y;
  r1.xyw = saturate(r0.yxz * r1.wxy);
  r2.xyz = r1.www * r6.yzw + globalFogColorN.xyz;
  r3.xyz -= r2.www;
  r0.y = r1.z;
  r0.xz = r1.xz * globalFogParams.ww;
  r5.z = 1.0 - r0.y;
  r0.y = r1.w * r6.x + r0.x;
  r5.y = r0.y + globalFogParams.z;
  r5.x = 1.0 - r1.y;
  r0.xw = r5.xy * r5.zz;
  r0.xy = r0.xx * r4.xy;
  r1.x = log2(abs(r3.w));
  r0.y *= r1.x;
  r1.x = r0.x + 1.0;
  r0.y = exp2(r0.y);
  r1.xyz = (r3.xyz * r1.xxx + r2.www) * r0.yyy;
  r3.xyz = globalFogColorN.xyz - r1.xyz;
  r0.xyz = r0.zzz * r3.xyz + r1.xyz;

  PixelShaderOutput output;
  output.oC0.xyz = lerp(r0.xyz, r2.xyz, r0.w);
  const uint valid =
      vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 0x2D0);
  const uint required = 0x001CB6F0u;
  if ((valid & required) == required && dot(input.iTexCoord2.xyz,
                                             input.iTexCoord2.xyz) > 0.0) {
    const float4 fog =
        vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x260);
    const float camera_altitude =
        vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x270).z;
    const float4 azimuth = vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x430);
    const float4 east = vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x440);
    const float4 sky = vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x450);
    const float4 sun = vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x460);
    const float3 sun_direction = vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x470).xyz;
    float3 camera_to_world = -input.iTexCoord2.xyz * decoded_depth + 1.0e-6;
    const float world_z = camera_to_world.z + camera_altitude;
    float distance = length(camera_to_world);
    const float3 direction = camera_to_world / max(distance, 1.0e-6);
    if (!is_geometry && world_z < 0.0)
      distance = max(camera_altitude / -direction.z, 0.0);

    float3 sky_color = lerp(east.xyz, azimuth.xyz, direction.x * 0.5 + 0.5);
    sky_color = sky_color * (east.w * (1.0 - saturate(direction.z * azimuth.w))) + sky.xyz;
    const float sun_dot = dot(direction, sun_direction);
    float a = saturate(sun_dot * -0.0625 + 0.9375);
    a = a * a * a;
    const float b = saturate(sun_dot * 0.5 + 0.5);
    const float c = saturate((b - a * saturate(0.6 + abs(sun_direction.z))) * 0.5) * 4.0;
    const float d = saturate(1.0 - c);
    float3 scatter = c * sun.xyz;
    scatter = scatter * scatter * scatter * scatter + c * sun.xyz;
    sky_color = min((saturate(sky_color * d * sun.w) + scatter * sun.w) * sky.w, 30.0);
    const float3 far_color = lerp(sky_color, globalFogColorN.xyz, globalFogParams.w);

    float slope = direction.z * fog.y;
    if (abs(slope) < 1.0e-6) slope = 1.0e-6;
    const float integral = (1.0 - exp2(-slope * distance)) / slope;
    const float density = exp2(-camera_altitude * fog.y * fog.z) * integral * fog.x;
    const float amount = pow(saturate(1.0 - exp2(-density)), fog.w);
    const float3 fog_color = lerp(globalFogColorN.xyz, far_color, is_geometry ? amount : 1.0);
    // Sky below ground suppresses near-fog influence and has a black base.
    const float near_weight = (!is_geometry && world_z < 0.0) ? 0.0 : globalFogParams.w;
    float3 base_color = lerp(r1.xyz, globalFogColorN.xyz, near_weight);
    if (!is_geometry && world_z < 0.0) base_color = 0.0;
    output.oC0.xyz = lerp(base_color, fog_color, amount);
  }

  output.oC0.w = 1.0;
  if (g_SpecConstants & SPEC_CONSTANT_ALPHA_TEST) {
    const float alpha_threshold =
        vk::RawBufferLoad<float>(g_PushConstants.SharedConstants + 0x244);
    const uint alpha_test_function =
        (g_SpecConstants >> SPEC_CONSTANT_ALPHA_TEST_FUNCTION_SHIFT) &
        SPEC_CONSTANT_ALPHA_TEST_FUNCTION_MASK;
    const bool alpha_test_pass =
        AlphaTestPass(output.oC0.w, alpha_threshold, alpha_test_function);
    clip(alpha_test_pass ? 1.0 : -1.0);
  }
  return output;
}
