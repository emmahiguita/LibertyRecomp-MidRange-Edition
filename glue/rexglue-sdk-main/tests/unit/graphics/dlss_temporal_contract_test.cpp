#include <cstdlib>
#include <iostream>
#include <limits>

#include <rex/ui/frame_pacer.h>
#include <rex/ui/publication_progress.h>
#include <rex/ui/vulkan/generated_frame.h>
#include <rex/ui/vulkan/presenter.h>

#include "graphics/gta4_native/temporal/dlss_frame_generation.h"
#include "graphics/gta4_native/temporal/dlss_upscaler.h"

using namespace rex::graphics::gta4_native::temporal;

namespace {
void Check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

Image Resource(uintptr_t identity, VkFormat format, Extent extent, VkImageUsageFlags usage) {
  return {reinterpret_cast<VkImage>(identity), reinterpret_cast<VkImageView>(identity), format,
          extent, VK_IMAGE_LAYOUT_GENERAL, usage, VK_IMAGE_ASPECT_COLOR_BIT};
}

FrameInput Scene() {
  FrameInput frame;
  frame.render_extent = {1280, 720};
  frame.output_extent = {1920, 1080};
  frame.color = Resource(1, VK_FORMAT_R16G16B16A16_SFLOAT, frame.render_extent, VK_IMAGE_USAGE_SAMPLED_BIT);
  frame.depth = Resource(2, VK_FORMAT_R32_SFLOAT, frame.render_extent, VK_IMAGE_USAGE_SAMPLED_BIT);
  frame.motion = Resource(3, VK_FORMAT_R16G16_SFLOAT, frame.render_extent, VK_IMAGE_USAGE_SAMPLED_BIT);
  frame.output = Resource(4, VK_FORMAT_R16G16B16A16_SFLOAT, frame.output_extent, VK_IMAGE_USAGE_STORAGE_BIT);
  frame.sequence = 1;
  frame.epoch = 1;
  frame.frame_time_ms = 16.0f;
  frame.camera_near = 0.1f;
  frame.camera_far = 1000.0f;
  frame.vertical_fov_radians = 1.0f;
  frame.motion_complete = true;
  frame.scene_without_ui = true;
  return frame;
}

DlssFrameGenerationInput Presentation(const FrameInput& frame) {
  DlssFrameGenerationInput input;
  input.present_color = Resource(5, VK_FORMAT_R8G8B8A8_UNORM, frame.output_extent,
                                 VK_IMAGE_USAGE_SAMPLED_BIT);
  input.generated_output = Resource(6, VK_FORMAT_R8G8B8A8_UNORM, frame.output_extent,
                                    VK_IMAGE_USAGE_STORAGE_BIT);
  const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  input.camera_view_to_clip = identity;
  input.clip_to_camera_view = identity;
  input.clip_to_previous_clip = identity;
  input.previous_clip_to_clip = identity;
  input.camera_up = {0, 1, 0};
  input.camera_right = {1, 0, 0};
  input.camera_forward = {0, 0, 1};
  input.camera_aspect_ratio = 1.7777777777777777f;  // Python: 1920 / 1080.
  input.camera_valid = true;
  return input;
}
}  // namespace

int main() {
  const auto good_frame = Scene();
  const auto good_input = Presentation(good_frame);
  Check(ValidDlssSuperResolutionInput(good_frame, true), "Complete DLSS SR input was rejected");
  const auto valid = [](const FrameInput& frame, const DlssFrameGenerationInput& input) {
    return ValidDlssFrameGenerationInput(frame, input, VK_FORMAT_R8G8B8A8_UNORM);
  };
  Check(valid(good_frame, good_input), "Complete temporal inputs were rejected");
  auto frame = good_frame;
  frame.color.format = VK_FORMAT_R8G8B8A8_UNORM;
  Check(!ValidDlssSuperResolutionInput(frame, true), "Normalized input claimed linear HDR");
  Check(ValidDlssSuperResolutionInput(frame, false), "Supported SDR color was rejected");
  frame = good_frame;
  frame.depth.format = VK_FORMAT_D32_SFLOAT;
  Check(!valid(frame, good_input) && !ValidDlssSuperResolutionInput(frame, true),
         "Depth image with color aspect was accepted");
  frame.depth.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  Check(valid(frame, good_input) && ValidDlssSuperResolutionInput(frame, true),
         "Depth-only sampled image was rejected");
  frame = good_frame;
  frame.output.image = frame.color.image;
  Check(!ValidDlssSuperResolutionInput(frame, true), "Aliased SR input and output accepted");
  frame = good_frame;
  frame.motion_complete = false;
  Check(!valid(frame, good_input), "Incomplete object motion was accepted");
  frame = good_frame;
  frame.scene_without_ui = false;
  Check(!valid(frame, good_input), "Scene color containing HUD was accepted");
  frame = good_frame;
  frame.motion.extent = frame.output_extent;
  Check(!valid(frame, good_input), "Wrong motion-vector extent was accepted");
  frame = good_frame;
  frame.motion.format = VK_FORMAT_R8_UNORM;
  Check(!valid(frame, good_input), "Scalar motion-vector resource was accepted");
  frame = good_frame;
  frame.jitter_x = 0.75f;
  Check(!valid(frame, good_input), "Out-of-pixel jitter was accepted");
  frame = good_frame;
  frame.pre_exposure = 0.0f;
  Check(!valid(frame, good_input), "Invalid pre-exposure was accepted");

  auto input = good_input;
  input.camera_valid = false;
  Check(!valid(good_frame, input), "Unverified camera metadata was accepted");
  input = good_input;
  input.clip_to_camera_view[0] = 2.0f;
  Check(!valid(good_frame, input), "Wrong inverse projection was accepted");
  input = good_input;
  input.clip_to_previous_clip[0] = std::numeric_limits<float>::quiet_NaN();
  Check(!valid(good_frame, input), "Non-finite reprojection matrix was accepted");
  input = good_input;
  input.camera_up = input.camera_forward;
  Check(!valid(good_frame, input), "Degenerate camera basis was accepted");
  input = good_input;
  input.camera_forward = {0, 0, 2};
  Check(!valid(good_frame, input), "Non-unit camera direction was accepted");
  input = good_input;
  input.generated_output.image = input.present_color.image;
  Check(!valid(good_frame, input), "Aliased interpolation input/output was accepted");
  input = good_input;
  input.generated_output.image = good_frame.motion.image;
  Check(!valid(good_frame, input), "FG overwrote its motion-vector input");
  input = good_input;
  input.generated_output.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
  Check(!valid(good_frame, input), "FG accepted a depth-aspect output view");
  input = good_input;
  input.generated_output.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  Check(!valid(good_frame, input), "Non-writable interpolation output layout was accepted");
  input = good_input;
  input.retained_real_output.image = reinterpret_cast<VkImage>(uintptr_t{7});
  Check(!valid(good_frame, input), "Incomplete optional real-output resource was accepted");
  input = good_input;
  input.retained_real_output = input.generated_output;
  Check(!valid(good_frame, input), "Aliased generated and real outputs were accepted");
  input = good_input;
  input.retained_real_output = Resource(7, VK_FORMAT_R8G8B8A8_UNORM, good_frame.output_extent,
                                       VK_IMAGE_USAGE_STORAGE_BIT);
  Check(valid(good_frame, input), "Independent retained real output was rejected");

  DlssUpscaler upscaler;
  DlssFrameGenerator generator;
  Check(!upscaler.available() && !generator.available(), "Uninitialized providers reported support");
  const Configuration configuration{good_frame.render_extent, good_frame.output_extent};
  Check(!upscaler.Configure(configuration), "SR configured without a selected Vulkan device");
  Check(!generator.Configure(configuration, VK_FORMAT_R8G8B8A8_UNORM, false),
         "FG configured without a selected Vulkan device");
  Check(!upscaler.Encode(VK_NULL_HANDLE, good_frame), "SR encoded without a command buffer");
  Check(!generator.Encode(VK_NULL_HANDLE, good_frame, good_input),
         "FG encoded without a command buffer");
  Check(!generator.has_generated_frame(), "Failed encode advertised a generated frame");
  upscaler.OnDiscarded();
  generator.OnDiscarded();
  upscaler.Shutdown();
  generator.Shutdown();
  Check(!generator.has_generated_frame(), "Discarded/shut-down FG retained a presentation");

  using rex::ui::vulkan::GeneratedFrame;
  using rex::ui::vulkan::GeneratedFrameDelivery;
  using Stage = GeneratedFrameDelivery::Stage;
  GeneratedFrame pair{
      reinterpret_cast<VkImage>(uintptr_t{8}), reinterpret_cast<VkImageView>(uintptr_t{8}),
      reinterpret_cast<VkImage>(uintptr_t{9}), reinterpret_cast<VkImageView>(uintptr_t{9}),
      {1920, 1080}, std::make_shared<int>(0), 1, 16'666'666};
  const auto mailbox = reinterpret_cast<VkImage>(uintptr_t{10});
  Check(pair.valid(mailbox, {1920, 1080}), "Complete retained presentation pair was rejected");
  auto bad_pair = pair;
  bad_pair.real = mailbox;
  Check(!bad_pair.valid(mailbox, {1920, 1080}), "Overwriteable mailbox accepted as retained real");
  bad_pair = pair;
  bad_pair.real = pair.generated;
  Check(!bad_pair.valid(mailbox, {1920, 1080}), "Aliased generated and real presentation images");
  bad_pair = pair;
  bad_pair.lease.reset();
  Check(!bad_pair.valid(mailbox, {1920, 1080}), "Presentation without retained lifetime accepted");
  bad_pair = pair;
  bad_pair.interval_ns = 0;
  Check(!bad_pair.valid(mailbox, {1920, 1080}), "Unpaced presentation pair was accepted");
  bad_pair.interval_ns = 100'000'001;
  Check(!bad_pair.valid(mailbox, {1920, 1080}), "Out-of-range interpolation interval accepted");
  Check(!pair.valid(mailbox, {1280, 720}), "Wrong presentation extent accepted");
  bool is_8bpc = false;
  rex::ui::vulkan::VulkanPresenter::VulkanGuestOutputRefreshContext refresh(
      is_8bpc, mailbox, reinterpret_cast<VkImageView>(uintptr_t{10}), 1, false,
      false, 1.0f, 1.0f, {}, {1920, 1080});
  Check(refresh.PairedPresentation() == false, "Unrecorded callback claimed paired presentation");
  Check(refresh.SetGeneratedFrame(pair.generated, pair.generated_view, pair.real, pair.real_view,
                                  pair.extent, pair.lease, pair.epoch, pair.interval_ns),
         "Valid generated pair could not be attached to refresh callback");
  Check(refresh.PairedPresentation() == false, "Unsubmitted pair claimed paired presentation");
  refresh.MarkImageAccessSubmitted();
  Check(refresh.PairedPresentation() == true, "Submitted pair did not update publication provenance");

  GeneratedFrameDelivery delivery;
  Check(delivery.Select(41, false, 7) == Stage::kUnpaired, "Unavailable interpolation was selected");
  Check(delivery.Select(41, true, 7) == Stage::kGenerated, "Generated image was not first");
  Check(delivery.Select(41, true, 7) == Stage::kGenerated, "Retry advanced unsubmitted generation");
  delivery.Presented(Stage::kGenerated, 41, 7);
  delivery.Presented(Stage::kReal, 42, 7);
  Check(delivery.has_pending(7), "Wrong queue receipt retired another publication's real frame");
  Check(delivery.Select(42, true, 7) == Stage::kReal, "Newer scene replaced pending real partner");
  Check(delivery.Select(42, true, 7) == Stage::kReal, "Retry advanced unsubmitted real partner");
  delivery.Presented(Stage::kReal, 41, 7);
  Check(delivery.Select(41, true, 7) == Stage::kUnpaired, "Already delivered pair was generated again");
  Check(delivery.Select(42, true, 7) == Stage::kGenerated, "Next publication could not generate");
  delivery.Presented(Stage::kGenerated, 42, 7);
  Check(!delivery.has_pending(8), "Old-surface pending frame survived a surface epoch change");
  Check(delivery.Select(42, true, 8) == Stage::kUnpaired, "Old-surface pair was replayed");
  delivery.ResetPending();
  Check(delivery.Select(43, true, 8) == Stage::kGenerated, "Surface reset lost new publication");
  rex::ui::PublicationProgress demand;
  demand.Publish(41);
  demand.Publish(42);  // This wakeup can paint the retained real partner of 41.
  demand.Accept(41);
  Check(demand.pending(), "Real partner consumed the newer publication's only paint demand");
  demand.Accept(42);
  Check(!demand.pending(), "Completed publications would cause endless timer wakeups");

  // Python-derived timestamps model the first pair supplying its interval after
  // the common presenter has admitted an uncapped paint attempt.
  rex::ui::FramePacer pacer;
  pacer.Configure(0, 1'000'000'000);
  Check(!pacer.Plan(1'000'000'000).delay_ns, "Initial presentation unexpectedly delayed");
  pacer.SetMinimumInterval(pair.interval_ns);
  pacer.Queued(1'002'000'000);
  Check(pacer.Plan(1'003'000'000).delay_ns == 15'666'666,
         "Real partner was not separated from generated by the observed interval");
  Check(!pacer.Plan(1'018'666'666).delay_ns, "Real partner missed its common pacing slot");
  pacer.Queued(1'018'666'666);
  Check(pacer.Plan(1'035'333'332).delay_ns == 0, "Next pair did not preserve equal pacing");
  std::cout << "DLSS input, lifecycle, and generated-frame delivery contracts passed\n";
}
