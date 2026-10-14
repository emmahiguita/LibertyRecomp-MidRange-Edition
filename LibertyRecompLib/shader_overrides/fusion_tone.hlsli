// FusionShaders rage_postfxPS11: gamma 2.2 -> PQ -> default Frostbite LUT.
// The flattened BGRA8 LUT is identical on Vulkan and Metal (no sRGB decode).
#ifndef LIBERTY_FUSION_TONE
#define LIBERTY_FUSION_TONE
float3 FusionLutTexel(uint64_t address, uint3 p) {
  uint index = p.y * 1024u + p.z * 32u + p.x;
  uint bgra = vk::RawBufferLoad<uint>(address + uint64_t(index) * 4u);
  return float3((bgra >> 16u) & 255u, (bgra >> 8u) & 255u, bgra & 255u) / 255.0;
}
float3 FusionToneMap(float3 color) {
  uint enabled = vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 0x428);
  uint64_t address = vk::RawBufferLoad<uint64_t>(g_PushConstants.SharedConstants + 0x420, 8);
  if (!enabled || address == 0) return color;
  // The upstream assembly uses log(abs(rgb)), including negative color-correction values.
  float3 linear_color = pow(abs(color), 2.2);
  float3 p = pow(linear_color * 0.01, 0.159301757813);
  float3 pq = saturate(pow((18.8515625 * p + 0.8359375) / (18.6875 * p + 1.0), 78.84375));
  float3 position = pq * 31.0;
  uint3 lo = uint3(position);
  uint3 hi = min(lo + 1u, 31u);
  float3 f = frac(position);
  float3 a = lerp(FusionLutTexel(address, uint3(lo.x, lo.y, lo.z)), FusionLutTexel(address, uint3(hi.x, lo.y, lo.z)), f.x);
  float3 b = lerp(FusionLutTexel(address, uint3(lo.x, hi.y, lo.z)), FusionLutTexel(address, uint3(hi.x, hi.y, lo.z)), f.x);
  float3 c = lerp(FusionLutTexel(address, uint3(lo.x, lo.y, hi.z)), FusionLutTexel(address, uint3(hi.x, lo.y, hi.z)), f.x);
  float3 d = lerp(FusionLutTexel(address, uint3(lo.x, hi.y, hi.z)), FusionLutTexel(address, uint3(hi.x, hi.y, hi.z)), f.x);
  return lerp(lerp(a, b, f.y), lerp(c, d, f.y), f.z);
}
// Preserve bloom hue when subtracting the threshold (FusionFix default).
float3 FusionBloomThreshold(float3 exposed, float threshold) {
  float brightness = sqrt(dot(exposed * exposed, float3(0.212500006, 0.715399981, 0.0720999986)));
  return exposed * (brightness > 0.0 ? max((brightness - threshold) / brightness, 0.0) : 0.0);
}
bool FusionModernEnabled() {
  return vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 0x428) != 0;
}
#endif
