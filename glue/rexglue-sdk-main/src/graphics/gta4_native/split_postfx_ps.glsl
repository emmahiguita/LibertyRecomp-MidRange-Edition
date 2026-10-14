#version 450

layout(set = 0, binding = 0) uniform sampler2D color_image;
layout(set = 0, binding = 1) uniform sampler2D blur_image;
layout(set = 0, binding = 2) uniform sampler2D depth_image;
layout(set = 0, binding = 3) uniform sampler2D stipple_mask_image;

layout(push_constant) uniform SplitPostFxConstants {
  ivec2 source_extent;
  ivec2 destination_extent;
  uint pass_index;
  uint depth_source;
  ivec2 full_extent;
  vec4 dof_projection;
  vec4 dof_distance;
  vec4 dof_blur;
} constants;

layout(location = 0) out vec4 output_color;

const vec2 disk_kernel[16] = vec2[](
    vec2(0.0, 0.0), vec2(0.16855472, 0.5187581),
    vec2(-0.44128203, 0.3206101), vec2(-0.44128197, -0.3206102),
    vec2(0.1685548, -0.5187581), vec2(1.0, 0.0),
    vec2(0.809017, 0.58778524), vec2(0.30901697, 0.95105654),
    vec2(-0.30901703, 0.9510565), vec2(-0.80901706, 0.5877852),
    vec2(-1.0, 0.0), vec2(-0.80901694, -0.58778536),
    vec2(-0.30901664, -0.9510566), vec2(0.30901712, -0.9510565),
    vec2(0.80901694, -0.5877853), vec2(0.54545456, 0.0));

vec2 pixel_uv() {
  return gl_FragCoord.xy / vec2(constants.destination_extent);
}

float luminance(vec3 color) {
  return dot(color, vec3(0.2125, 0.7154, 0.0721));
}

vec4 apply_stipple_filter(vec2 uv) {
  vec2 texel = 1.0 / vec2(constants.source_extent);
  vec4 center = textureLod(color_image, uv, 0.0);
  vec4 samples[4] = vec4[](
      textureLod(color_image, uv + texel * vec2(-0.5, -1.5), 0.0),
      textureLod(color_image, uv + texel * vec2(1.5, -0.5), 0.0),
      textureLod(color_image, uv + texel * vec2(0.5, 1.5), 0.0),
      textureLod(color_image, uv + texel * vec2(-1.5, 0.5), 0.0));
  vec4 average = (samples[0] + samples[1] + samples[2] + samples[3]) * 0.25;
  vec4 sample_luma = vec4(luminance(samples[0].rgb), luminance(samples[1].rgb),
                          luminance(samples[2].rgb), luminance(samples[3].rgb));
  float average_luma = dot(sample_luma, vec4(0.25));
  vec4 deviations = sample_luma - average_luma;
  float center_deviation = luminance(center.rgb) - average_luma;
  float detected = center_deviation * center_deviation >= dot(deviations, deviations) ? 1.0 : 0.0;
  float mask = (
      textureLod(stipple_mask_image, uv, 0.0).a +
      textureLod(stipple_mask_image, uv + vec2(texel.x, 0.0), 0.0).a +
      textureLod(stipple_mask_image, uv + vec2(0.0, texel.y), 0.0).a +
      textureLod(stipple_mask_image, uv + texel, 0.0).a);
  return vec4(mix(center.rgb, average.rgb, detected * clamp(mask, 0.0, 1.0)), 1.0);
}

vec4 gather_bokeh(vec2 uv) {
  // FusionShaders external/dof_blur.asm, pinned in docs/modern-dof.md.
  // c44 is FULL resolution even though the source and destination are half size.
  vec2 radius_uv = float(constants.full_extent.y) / vec2(constants.full_extent) * 0.0027777;
  vec3 samples[16];
  vec3 sum = vec3(0.0);
  vec3 positive_detail = vec3(0.0);
  for (int index = 0; index < 16; ++index) {
    samples[index] = textureLod(color_image, uv + disk_kernel[index] * radius_uv, 0.0).rgb;
    sum += samples[index];
  }
  vec3 average = sum * 0.0625;
  for (int index = 1; index < 16; ++index) {
    positive_detail += max(samples[index] - average, vec3(0.0));
  }
  positive_detail += max(samples[0] - average, vec3(0.0));
  return vec4(average + positive_detail * 0.0625, 1.0);
}

vec4 tent_filter(vec2 uv) {
  // Saturation in dof_tent.asm is AFTER multiplication by full-res texel size.
  vec2 radius = clamp((float(constants.full_extent.y) * 0.000694444 - 0.5) /
                      vec2(constants.full_extent), vec2(0.0), vec2(1.0));
  vec4 taps[4] = vec4[](
      textureLod(color_image, uv + radius, 0.0),
      textureLod(color_image, uv + radius * vec2(-1.0, 1.0), 0.0),
      textureLod(color_image, uv + radius * vec2(1.0, -1.0), 0.0),
      textureLod(color_image, uv - radius, 0.0));
  vec4 average = (taps[0] + taps[1] + taps[2] + taps[3]) * 0.25;
  vec4 positive_detail = max(taps[0] - average, vec4(0.0)) +
                         max(taps[1] - average, vec4(0.0)) +
                         max(taps[2] - average, vec4(0.0)) +
                         max(taps[3] - average, vec4(0.0));
  return vec4((average + positive_detail * 0.25).rgb, 1.0);
}

vec4 combine_dof(vec2 uv) {
  vec4 sharp = textureLod(color_image, uv, 0.0);
  vec3 blurred = textureLod(blur_image, uv, 0.0).rgb;
  float encoded_depth = textureLod(depth_image, uv, 0.0).r;
  // Xbox c209 is dofProj, NOT FusionFix's unrelated PC c209 LogDepth register.
  // Decode the title's reverse projection to the same view-space distance that
  // FusionFix reconstructs from its logarithmic buffer before computing CoC.
  float view_depth = constants.dof_projection.x * constants.dof_projection.y /
      (constants.dof_projection.x + encoded_depth *
       (constants.dof_projection.y - constants.dof_projection.x));
  float focus_width = constants.dof_distance.y * 0.5;
  float near_amount = max(constants.dof_distance.w - view_depth - focus_width, 0.0) /
                      constants.dof_distance.x;
  float far_amount = max(view_depth - constants.dof_distance.w - focus_width, 0.0) /
                     constants.dof_distance.z;
  float near_blur = min(mix(constants.dof_blur.y, constants.dof_blur.x, near_amount),
                        constants.dof_blur.x);
  float far_blur = min(mix(constants.dof_blur.y, constants.dof_blur.z, far_amount),
                       constants.dof_blur.z);
  float coc = max(near_blur, far_blur);
  return vec4(mix(sharp.rgb, blurred, coc * coc), 1.0);
}

void main() {
  vec2 uv = pixel_uv();
  if (constants.pass_index == 0u) {
    output_color = apply_stipple_filter(uv);
  } else if (constants.pass_index == 1u) {
    output_color = gather_bokeh(uv);
  } else if (constants.pass_index == 2u) {
    output_color = tent_filter(uv);
  } else {
    output_color = combine_dof(uv);
  }
}
