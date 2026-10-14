#version 450
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
layout(set = 0, binding = 0) uniform sampler2D scene_image;
layout(set = 0, binding = 1) uniform sampler2D pass_image;
layout(set = 0, binding = 2) uniform sampler2D depth_image;
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer CloudMask { float value[]; };
layout(push_constant) uniform SunShaftConstants {
  ivec2 source_extent;
  ivec2 destination_extent;
  uint pass_index;
  uint sample_count;
  float density;
  float decay;
  vec4 sun_screen;
  vec4 view_sun_projection;
  vec4 depth_projection;
  uint64_t cloud_mask_address;
  uint cloud_width;
  uint cloud_height;
} constants;
layout(location = 0) out vec4 output_color;

float cloud_texel(ivec2 p) {
  p = clamp(p, ivec2(0), ivec2(constants.cloud_width, constants.cloud_height) - 1);
  return CloudMask(constants.cloud_mask_address).value[p.y * int(constants.cloud_width) + p.x];
}
float cloud_transmittance(vec2 uv) {
  if (constants.cloud_mask_address == 0ul || constants.cloud_width == 0u || constants.cloud_height == 0u) return 0.0;
  vec2 p = uv * vec2(constants.cloud_width, constants.cloud_height) - 0.5;
  ivec2 lo = ivec2(floor(p));
  vec2 f = fract(p);
  return mix(mix(cloud_texel(lo), cloud_texel(lo + ivec2(1, 0)), f.x),
             mix(cloud_texel(lo + ivec2(0, 1)), cloud_texel(lo + ivec2(1, 1)), f.x), f.y);
}
float sky_mask(float encoded_depth) {
  if (constants.depth_projection.w == 0.0) return encoded_depth == 0.0 ? 1.0 : 0.0;
  // Convert Xbox reciprocal depth to the PC logarithmic interval used by FusionFix.
  float near_clip = constants.depth_projection.y, far_clip = constants.depth_projection.z;
  float z = near_clip * far_clip / (encoded_depth * (far_clip - near_clip) + near_clip);
  float log_depth = log2(max(z / near_clip, 1.0)) / log2(far_clip / near_clip);
  return clamp(log_depth * 10.0 - 9.0, 0.0, 1.0);
}
vec4 prepass(vec2 uv) {
  vec3 scene = textureLod(scene_image, uv, 0.0).rgb;
  float sky = sky_mask(textureLod(depth_image, uv, 0.0).r);
  vec3 view_direction = normalize(vec3((uv.x * 2.0 - 1.0) / constants.view_sun_projection.w,
      (1.0 - uv.y * 2.0) / constants.depth_projection.x, -1.0));
  float sun = dot(view_direction, normalize(constants.view_sun_projection.xyz)) >= 0.996 ? 1.0 : 0.0;
  vec2 aspect = vec2(float(constants.source_extent.x) / float(constants.source_extent.y), 1.0);
  vec2 rectangle = min(uv, vec2(1.0) - uv) * aspect;
  float edge = clamp(32.0 * min(rectangle.x, rectangle.y), 0.0, 1.0);
  return vec4(scene * sky * sun * edge * constants.sun_screen.z *
              cloud_transmittance(uv) * constants.sun_screen.w, 1.0);
}
vec4 radial_scatter(vec2 uv) {
  vec2 delta = (uv - constants.sun_screen.xy) * (constants.density / float(constants.sample_count));
  vec3 color = textureLod(pass_image, uv, 0.0).rgb;
  float illumination_decay = 1.0;
  for (uint i = 0u; i < constants.sample_count; ++i) {
    uv -= delta;
    color += textureLod(pass_image, uv, 0.0).rgb * illumination_decay;
    illumination_decay *= constants.decay;
  }
  // FusionFix intentionally leaves the two 24-sample sums unnormalized.
  return vec4(color, 1.0);
}
void main() {
  // Native pixel centers replace D3D9's full/half-resolution half-texel adjustment.
  vec2 uv = gl_FragCoord.xy / vec2(constants.destination_extent);
  if (constants.pass_index == 0u) output_color = prepass(uv);
  else if (constants.pass_index < 3u) output_color = radial_scatter(uv);
  else {
    vec4 scene = textureLod(scene_image, uv, 0.0);
    output_color = vec4(scene.rgb + textureLod(pass_image, uv, 0.0).rgb, scene.a);
  }
}
