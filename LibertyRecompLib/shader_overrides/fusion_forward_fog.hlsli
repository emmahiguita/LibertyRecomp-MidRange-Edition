// Shared radial height fog for forward materials. The Xbox viewport is inverted
// explicitly so reconstructed positions use the guest row-major matrix convention.
#ifndef LIBERTY_FUSION_FORWARD_FOG
#define LIBERTY_FUSION_FORWARD_FOG
float4 FusionOriginalFogParameters() {
  return vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 0x290, 16);
}
bool FusionForwardFogAvailable() {
  const uint required = 0x009CB6F0u;
  const uint valid = vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 0x2D0);
  const float4 viewport = vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x4E0, 16);
  const float2 depth = vk::RawBufferLoad<float2>(g_PushConstants.SharedConstants + 0x4F0, 8);
  return vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 0x428) != 0 &&
      (valid & required) == required && all(viewport.zw > 0.0) && depth.x != depth.y
#ifdef FUSION_FOG_START_EXEMPTION
      && FusionOriginalFogParameters().x < 1000.0
#endif
      ;
}
float4 FusionForwardFogParameters() {
  // Disable the original piecewise blend while retaining the material's depth
  // desaturation, lighting, alpha, and all other title computations.
  return FusionForwardFogAvailable() ? float4(1.0e20, 2.0e20, 0.0, 0.0) : FusionOriginalFogParameters();
}
float3 FusionForwardFog(float3 color, float4 position) {
  if (!FusionForwardFogAvailable()) return color;
  const float4 viewport = vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x4E0, 16);
  const float2 depth = vk::RawBufferLoad<float2>(g_PushConstants.SharedConstants + 0x4F0, 8);
  float2 ndc = (position.xy - viewport.xy) / viewport.zw * float2(2.0, -2.0) + float2(-1.0, 1.0);
  ndc -= vk::RawBufferLoad<float2>(g_PushConstants.SharedConstants + 0x228, 8);
  const float4x4 inverse = float4x4(
      vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x4A0, 16),
      vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x4B0, 16),
      vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x4C0, 16),
      vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x4D0, 16));
  float4 world = mul(float4(ndc, (position.z - depth.x) / (depth.y - depth.x), 1.0), inverse);
  if (abs(world.w) < 1.0e-12) return color;
  const float3 camera = vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x270, 16).xyz;
  const float3 ray = world.xyz / world.w - camera;
  const float distance = length(ray);
  const float3 direction = ray / max(distance, 1.0e-6);
  const float4 fog = vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x260, 16);
  const float4 azimuth = vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x430, 16);
  const float4 east = vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x440, 16);
  const float4 sky = vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x450, 16);
  const float4 sun = vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x460, 16);
  const float3 sun_direction = vk::RawBufferLoad<float4>(g_PushConstants.SharedConstants + 0x470, 16).xyz;
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
  const float3 near_color = vk::RawBufferLoad<float4>(g_PushConstants.PixelShaderConstants + 0x2B0, 16).xyz;
  const float3 fog_color = lerp(sky_color, near_color, FusionOriginalFogParameters().w);
  float slope = direction.z * fog.y;
  if (abs(slope) < 1.0e-6) slope = 1.0e-6;
  const float integral = (1.0 - exp2(-slope * distance)) / slope;
  const float density = exp2(-camera.z * fog.y * fog.z) * integral * fog.x;
  const float amount = pow(saturate(1.0 - exp2(-density)), fog.w);
  return lerp(color, fog_color, amount);
}
#endif
