#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>

#include <rex/ui/vulkan/api.h>
#include <rex/graphics/gta4_native/temporal_commands.h>

namespace rex::graphics::gta4_native::temporal {

struct Extent {
  uint32_t width = 0;
  uint32_t height = 0;
  bool operator==(const Extent&) const = default;
  explicit operator bool() const { return width && height; }
};

using Quality = TemporalUpscalerQuality;

// The caller owns these resources and retains them through submission completion.
// Views cover mip zero and one array layer of a single-sample 2D image. Both
// vendor adapters consume GENERAL and leave resources in GENERAL. The caller
// provides barriers and invalidates its graphics/compute binding cache afterward.
struct Image {
  VkImage image = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  Extent extent{};
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageUsageFlags usage = 0;
  VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
  explicit operator bool() const {
    return image && view && format != VK_FORMAT_UNDEFINED && bool(extent);
  }
};

struct Configuration {
  Extent render_extent{};
  Extent output_extent{};
  Quality quality = Quality::kQuality;
  bool hdr = true;
  bool reversed_depth = false;
  bool infinite_depth = false;
  bool operator==(const Configuration&) const = default;
};

// Render-pixel, current-to-previous motion EXCLUDES projection jitter. Color is
// linear scene color before HUD composition. Depth is the matching device depth,
// not linearized distance. Reactive and composition are independent optional
// normalized masks. Neither may substitute for missing opaque-object motion.
struct FrameInput {
  Image color;
  Image depth;
  Image motion;
  Image reactive;
  Image composition;
  Image exposure;
  Image output;
  Extent render_extent{};
  Extent output_extent{};
  uint64_t sequence = 0;
  uint64_t epoch = 0;
  uint64_t time_ns = 0;
  float jitter_x = 0.0f;
  float jitter_y = 0.0f;
  float frame_time_ms = 0.0f;
  float pre_exposure = 1.0f;
  float camera_near = 0.0f;
  float camera_far = 0.0f;
  float vertical_fov_radians = 0.0f;
  bool reset = true;
  bool motion_complete = false;
  bool scene_without_ui = false;
};

inline bool ValidFrameInput(const FrameInput& frame) {
  const auto readable = [](const Image& image) {
    return image && image.layout == VK_IMAGE_LAYOUT_GENERAL &&
           (image.usage & VK_IMAGE_USAGE_SAMPLED_BIT);
  };
  const auto optional = [&](const Image& image) {
    return image.image == VK_NULL_HANDLE ? image.view == VK_NULL_HANDLE : readable(image);
  };
  const auto mask = [&](const Image& image) {
    return optional(image) && (!image.image ||
        (image.extent == frame.render_extent && image.format == VK_FORMAT_R8_UNORM));
  };
  const auto output_alias = [&](const Image& image) {
    return image.image && image.image == frame.output.image;
  };
  return readable(frame.color) && readable(frame.depth) && readable(frame.motion) &&
         mask(frame.reactive) && mask(frame.composition) && optional(frame.exposure) &&
         (!frame.exposure.image || (frame.exposure.extent == Extent{1, 1} &&
                                   frame.exposure.format == VK_FORMAT_R32_SFLOAT)) &&
         (frame.motion.format == VK_FORMAT_R16G16_SFLOAT ||
          frame.motion.format == VK_FORMAT_R32G32_SFLOAT) &&
         (frame.depth.format == VK_FORMAT_R32_SFLOAT ||
          frame.depth.format == VK_FORMAT_D32_SFLOAT ||
          frame.depth.format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
          frame.depth.format == VK_FORMAT_D16_UNORM ||
          frame.depth.format == VK_FORMAT_D24_UNORM_S8_UINT) &&
         frame.color.image != frame.depth.image && frame.color.image != frame.motion.image &&
         frame.depth.image != frame.motion.image &&
         frame.output && frame.output.layout == VK_IMAGE_LAYOUT_GENERAL &&
         (frame.output.usage & VK_IMAGE_USAGE_STORAGE_BIT) &&
         frame.color.image != frame.output.image && frame.depth.image != frame.output.image &&
         frame.motion.image != frame.output.image && bool(frame.render_extent) &&
         !output_alias(frame.reactive) && !output_alias(frame.composition) &&
         !output_alias(frame.exposure) &&
         bool(frame.output_extent) && frame.color.extent == frame.render_extent &&
         frame.depth.extent == frame.render_extent && frame.motion.extent == frame.render_extent &&
         frame.output.extent == frame.output_extent && frame.motion_complete &&
         frame.scene_without_ui && std::isfinite(frame.jitter_x) &&
         std::isfinite(frame.jitter_y) && std::isfinite(frame.frame_time_ms) &&
         frame.frame_time_ms > 0.0f && std::isfinite(frame.pre_exposure) &&
         frame.pre_exposure > 0.0f && std::isfinite(frame.camera_near) &&
         frame.camera_near > 0.0f && std::isfinite(frame.camera_far) &&
         frame.camera_far > frame.camera_near && std::isfinite(frame.vertical_fov_radians) &&
         frame.vertical_fov_radians > 0.0f && frame.vertical_fov_radians < std::numbers::pi_v<float>;
}

}  // namespace rex::graphics::gta4_native::temporal
