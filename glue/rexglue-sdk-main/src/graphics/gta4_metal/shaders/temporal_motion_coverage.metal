// AA reactivity measures color instability, independently of geometry motion.
// Transparent layers preserve the opaque motion/depth beneath them. Missing
// geometry uses our previous-depth sentinel and never means stationary.
bool liberty_coverage_finite(float value) {
  // Keep this classification valid even when the library enables fast math.
  return (as_type<uint>(value) & 0x7f800000u) != 0x7f800000u;
}
bool liberty_coverage_depth(float value) {
  return liberty_coverage_finite(value) && value >= 0 && value <= 1;
}
kernel void liberty_temporal_motion_coverage(
    texture2d<float, access::read> reactive [[texture(0)]],
    texture2d<float, access::read> motion [[texture(1)]],
    texture2d<float, access::read> depth [[texture(2)]],
    texture2d<float, access::read> previous_depth [[texture(3)]],
    device atomic_uint& invalid [[buffer(0)]],
    uint2 pixel [[thread_position_in_grid]], uint lane [[thread_index_in_threadgroup]]) {
  threadgroup atomic_uint tile_invalid;
  if (lane == 0) atomic_store_explicit(&tile_invalid, 0u, memory_order_relaxed);
  threadgroup_barrier(mem_flags::mem_threadgroup);
  uint flags = 0;
  if (pixel.x < motion.get_width() && pixel.y < motion.get_height()) {
    const float mask = reactive.read(pixel).r;
    const float2 velocity = motion.read(pixel).xy;
    if (!liberty_coverage_finite(mask)) flags |= 1u;
    if (!liberty_coverage_finite(velocity.x) || !liberty_coverage_finite(velocity.y))
      flags |= 2u;
    if (!liberty_coverage_depth(depth.read(pixel).r)) flags |= 4u;
    if (!liberty_coverage_depth(previous_depth.read(pixel).r)) flags |= 8u;
  }
  if (flags) atomic_fetch_or_explicit(&tile_invalid, flags, memory_order_relaxed);
  threadgroup_barrier(mem_flags::mem_threadgroup);
  if (lane == 0) {
    flags = atomic_load_explicit(&tile_invalid, memory_order_relaxed);
    if (flags) atomic_fetch_or_explicit(&invalid, flags, memory_order_relaxed);
  }
}
