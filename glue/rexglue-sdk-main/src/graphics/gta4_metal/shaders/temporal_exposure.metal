// The title keeps adaptation on the GPU. Supply the base HDR gain as a
// reconstruction visibility hint; the original composite still owns all
// additive bloom, color correction and tone mapping.
kernel void liberty_temporal_exposure_hint(
    texture2d<float, access::read> adaptation [[texture(0)]],
    texture2d<float, access::write> output [[texture(1)]],
    constant float2& factors [[buffer(0)]], uint2 id [[thread_position_in_grid]]) {
  if (any(id != uint2(0))) return;
  const float luminance = adaptation.read(uint2(0)).r;
  // Classify before arithmetic, retaining robust input validation even if
  // this helper is compiled independently under a different math mode.
  const uint bits = as_type<uint>(luminance);
  if ((bits & 0x7f800000u) == 0x7f800000u || luminance < 0) {
    output.write(float4(0x1p-14f), uint2(0));
    return;
  }
  const float reciprocal = clamp(1.0f / luminance, -MAXFLOAT, MAXFLOAT);
  const float gain = factors.x * (factors.y * reciprocal);
  // MetalFX requires a positive finite half. Bound title singularities and
  // half conversion overflow explicitly; normal finite gains are unchanged.
  const float bounded = clamp(gain, 0x1p-14f, 65504.0f);
  output.write(float4(bounded), uint2(0));
}
