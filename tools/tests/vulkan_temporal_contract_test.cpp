#include "graphics/gta4_native/temporal/frame_input.h"

#include <cstdio>
#include <limits>
#include <type_traits>

using namespace rex::graphics::gta4_native::temporal;

namespace {
template <typename Handle>
Handle HandleForTest(uintptr_t value) {
  if constexpr (std::is_pointer_v<Handle>) return reinterpret_cast<Handle>(value);
  else return static_cast<Handle>(value);
}

Image ImageForTest(uintptr_t identity, VkFormat format, Extent extent) {
  return {HandleForTest<VkImage>(identity), HandleForTest<VkImageView>(identity), format,
          extent, VK_IMAGE_LAYOUT_GENERAL,
          VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT};
}

FrameInput ValidScene() {
  FrameInput frame;
  frame.render_extent = {1280, 720};
  frame.output_extent = {1920, 1080};
  frame.color = ImageForTest(1, VK_FORMAT_R16G16B16A16_SFLOAT, frame.render_extent);
  frame.depth = ImageForTest(2, VK_FORMAT_R32_SFLOAT, frame.render_extent);
  frame.motion = ImageForTest(3, VK_FORMAT_R16G16_SFLOAT, frame.render_extent);
  frame.output = ImageForTest(4, VK_FORMAT_R16G16B16A16_SFLOAT, frame.output_extent);
  frame.sequence = 1;
  frame.epoch = 1;
  frame.frame_time_ms = 20.0f;
  frame.camera_near = 0.1f;
  frame.camera_far = 1000.0f;
  frame.vertical_fov_radians = 1.0f;
  frame.motion_complete = true;
  frame.scene_without_ui = true;
  return frame;
}
}  // namespace

int main() {
  unsigned checks = 0;
  unsigned failures = 0;
  const auto check = [&](bool value, const char* name) {
    ++checks;
    if (!value) {
      ++failures;
      std::fprintf(stderr, "FAIL: %s\n", name);
    }
  };
  const auto reject = [&](const char* name, auto mutate) {
    auto frame = ValidScene();
    mutate(frame);
    check(!ValidFrameInput(frame), name);
  };
  check(ValidFrameInput(ValidScene()), "distinct scene resources accepted");
  // Regression for the retired presenter's color-as-depth/color-as-motion path.
  reject("color cannot stand in for depth", [](auto& f) { f.depth = f.color; });
  reject("color cannot stand in for motion", [](auto& f) { f.motion = f.color; });
  reject("aliased depth with forged metadata", [](auto& f) { f.depth.image = f.color.image; });
  reject("aliased motion with forged metadata", [](auto& f) { f.motion.image = f.color.image; });
  reject("depth cannot stand in for motion", [](auto& f) { f.motion.image = f.depth.image; });
  reject("output must not overwrite input", [](auto& f) { f.output.image = f.color.image; });
  reject("missing object motion rejected", [](auto& f) { f.motion_complete = false; });
  reject("HUD-contaminated input rejected", [](auto& f) { f.scene_without_ui = false; });
  reject("untransitioned image rejected", [](auto& f) { f.depth.layout = VK_IMAGE_LAYOUT_UNDEFINED; });
  reject("output requires storage usage", [](auto& f) { f.output.usage = VK_IMAGE_USAGE_SAMPLED_BIT; });
  reject("depth requires sampled usage", [](auto& f) { f.depth.usage = VK_IMAGE_USAGE_STORAGE_BIT; });
  reject("depth from another resolution", [](auto& f) { f.depth.extent = f.output_extent; });
  reject("display motion cannot masquerade as render motion", [](auto& f) { f.motion.extent = f.output_extent; });
  reject("output size mismatch", [](auto& f) { f.output.extent = f.render_extent; });
  reject("zero render extent", [](auto& f) { f.render_extent = {}; });
  reject("zero frame interval", [](auto& f) { f.frame_time_ms = 0; });
  reject("nonfinite interval", [](auto& f) { f.frame_time_ms = std::numeric_limits<float>::infinity(); });
  reject("nonfinite jitter", [](auto& f) { f.jitter_x = std::numeric_limits<float>::quiet_NaN(); });
  reject("invalid pre-exposure", [](auto& f) { f.pre_exposure = 0; });
  reject("degenerate camera range", [](auto& f) { f.camera_far = f.camera_near; });
  reject("FOV degrees cannot masquerade as radians", [](auto& f) { f.vertical_fov_radians = 60; });
  reject("dangling optional view", [](auto& f) { f.reactive.view = HandleForTest<VkImageView>(5); });
  reject("mask at display resolution", [](auto& f) { f.reactive = ImageForTest(5, VK_FORMAT_R8_UNORM, f.output_extent); });
  reject("mask cannot alias output", [](auto& f) {
    f.reactive = ImageForTest(4, VK_FORMAT_R8_UNORM, f.render_extent);
  });
  auto frame = ValidScene();
  frame.reactive = ImageForTest(5, VK_FORMAT_R8_UNORM, frame.render_extent);
  frame.composition = ImageForTest(6, VK_FORMAT_R8_UNORM, frame.render_extent);
  frame.exposure = ImageForTest(7, VK_FORMAT_R32_SFLOAT, {1, 1});
  check(ValidFrameInput(frame), "independent reactive/composition/exposure inputs accepted");
  frame.exposure.extent = frame.render_extent;
  check(!ValidFrameInput(frame), "exposure is a scalar image");
  frame = ValidScene();
  frame.depth.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
  frame.depth.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  check(ValidFrameInput(frame), "sampled device-depth view accepted");
  std::printf("vulkan_temporal_contract: checks=%u failures=%u\n", checks, failures);
  return failures ? 1 : 0;
}
