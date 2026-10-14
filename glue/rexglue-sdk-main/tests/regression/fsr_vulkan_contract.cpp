// Compile with the pinned SDK headers and fsr_vulkan.cpp, with both
// REX_HAS_FIDELITYFX_VULKAN and REX_HAS_FIDELITYFX_FRAMEGENERATION defined.
// This validates API descriptors/lifecycle, not GPU execution or image quality.
#include "../../src/graphics/gta4_native/temporal/fsr_vulkan.h"

#include <cassert>
#include <cstring>
#include <limits>
#include <vector>

#include <ffx_api/ffx_api.h>
#include <ffx_api/ffx_framegeneration.h>
#include <ffx_api/ffx_upscale.h>
#include <ffx_api/vk/ffx_api_vk.h>

using namespace rex::graphics::gta4_native::temporal;
namespace {
struct MockContext { ffxCreateContextDescHeader* create; };
uint32_t create_count = 0, destroy_count = 0;
bool fail_create = false, fail_dispatch = false;
std::vector<bool> upscale_resets, generation_resets;
uint64_t prepared_sequence = 0;
bool prepared_camera = false;
void DummyFunction() {}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL Resolver(VkDevice, const char* name) {
  return std::strcmp(name, "vkGetBufferMemoryRequirements2") == 0 ? DummyFunction : nullptr;
}
Image MakeImage(uintptr_t id, VkFormat format, Extent extent, bool output = false) {
  Image result;
  result.image = reinterpret_cast<VkImage>(id);
  result.view = reinterpret_cast<VkImageView>(id);
  result.format = format;
  result.extent = extent;
  result.layout = VK_IMAGE_LAYOUT_GENERAL;
  result.usage = output ? VK_IMAGE_USAGE_STORAGE_BIT : VK_IMAGE_USAGE_SAMPLED_BIT;
  return result;
}
FrameInput MakeFrame() {
  FrameInput frame;
  frame.render_extent = {1280, 720};
  frame.output_extent = {1920, 1080};
  frame.color = MakeImage(1, VK_FORMAT_R16G16B16A16_SFLOAT, frame.render_extent);
  frame.depth = MakeImage(2, VK_FORMAT_R32_SFLOAT, frame.render_extent);
  frame.motion = MakeImage(3, VK_FORMAT_R16G16_SFLOAT, frame.render_extent);
  frame.reactive = MakeImage(4, VK_FORMAT_R8_UNORM, frame.render_extent);
  frame.composition = MakeImage(5, VK_FORMAT_R8_UNORM, frame.render_extent);
  frame.exposure = MakeImage(6, VK_FORMAT_R32_SFLOAT, {1, 1});
  frame.output = MakeImage(7, VK_FORMAT_R16G16B16A16_SFLOAT, frame.output_extent, true);
  frame.sequence = 1;
  frame.epoch = 1;
  frame.time_ns = 1;
  frame.frame_time_ms = 16.0f;
  frame.camera_near = 0.1f;
  frame.camera_far = 1000.0f;
  frame.vertical_fov_radians = 1.0f;
  frame.jitter_x = 0.25f;
  frame.jitter_y = -0.25f;
  frame.pre_exposure = 2.0f;
  frame.motion_complete = true;
  frame.scene_without_ui = true;
  frame.reset = false;
  return frame;
}
void CheckRead(const FfxApiResource& image, uintptr_t id, uint32_t format) {
  assert(image.resource == reinterpret_cast<void*>(id));
  assert(image.state == FFX_API_RESOURCE_STATE_GENERIC_READ);
  assert(image.description.format == format);
}
}  // namespace

extern "C" ffxReturnCode_t ffxCreateContext(ffxContext* context,
                                            ffxCreateContextDescHeader* description,
                                            const ffxAllocationCallbacks*) {
  ++create_count;
  if (fail_create) return FFX_API_RETURN_ERROR_MEMORY;
  const auto* backend = reinterpret_cast<ffxCreateBackendVKDesc*>(description->pNext);
  assert(backend->header.type == FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK);
  assert(backend->vkDeviceProcAddr(backend->vkDevice, "vkGetBufferMemoryRequirements2KHR") == DummyFunction);
  *context = new MockContext{description};
  return FFX_API_RETURN_OK;
}
extern "C" ffxReturnCode_t ffxDestroyContext(ffxContext* context, const ffxAllocationCallbacks*) {
  delete reinterpret_cast<MockContext*>(*context);
  *context = nullptr;
  ++destroy_count;
  return FFX_API_RETURN_OK;
}
extern "C" ffxReturnCode_t ffxQuery(ffxContext*, ffxQueryDescHeader* description) {
  if (description->type == FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION) {
    reinterpret_cast<ffxQueryGetProviderVersion*>(description)->versionName = "contract-test-provider";
  } else if (description->type == FFX_API_QUERY_DESC_TYPE_UPSCALE_GETRENDERRESOLUTIONFROMQUALITYMODE) {
    auto* query = reinterpret_cast<ffxQueryDescUpscaleGetRenderResolutionFromQualityMode*>(description);
    assert(query->qualityMode == FFX_UPSCALE_QUALITY_MODE_QUALITY);
    *query->pOutRenderWidth = 1280;
    *query->pOutRenderHeight = 720;
  } else if (description->type == FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT) {
    *reinterpret_cast<ffxQueryDescUpscaleGetJitterPhaseCount*>(description)->pOutPhaseCount = 18;
  } else if (description->type == FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET) {
    auto* query = reinterpret_cast<ffxQueryDescUpscaleGetJitterOffset*>(description);
    assert(query->index == 15);
    *query->pOutX = 0.25f;
    *query->pOutY = -0.25f;
  } else {
    assert(false);
  }
  return FFX_API_RETURN_OK;
}
extern "C" ffxReturnCode_t ffxConfigure(ffxContext*, const ffxConfigureDescHeader* description) {
  assert(description->type == FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION);
  const auto* config = reinterpret_cast<const ffxConfigureDescFrameGeneration*>(description);
  assert(config->flags == FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY);
  assert(config->frameGenerationEnabled && !config->allowAsyncWorkloads);
  assert(!config->swapChain && !config->presentCallback && !config->frameGenerationCallback);
  assert(!config->HUDLessColor.resource);  // UI is composed after interpolation.
  return FFX_API_RETURN_OK;
}
extern "C" ffxReturnCode_t ffxDispatch(ffxContext* context, const ffxDispatchDescHeader* description) {
  if (fail_dispatch) return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
  const auto* object = reinterpret_cast<MockContext*>(*context);
  assert(object->create->pNext->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_VK);
  if (description->type == FFX_API_DISPATCH_DESC_TYPE_UPSCALE) {
    const auto* dispatch = reinterpret_cast<const ffxDispatchDescUpscale*>(description);
    upscale_resets.push_back(dispatch->reset);
    CheckRead(dispatch->color, 1, FFX_API_SURFACE_FORMAT_R16G16B16A16_FLOAT);
    CheckRead(dispatch->depth, 2, FFX_API_SURFACE_FORMAT_R32_FLOAT);
    CheckRead(dispatch->motionVectors, 3, FFX_API_SURFACE_FORMAT_R16G16_FLOAT);
    CheckRead(dispatch->reactive, 4, FFX_API_SURFACE_FORMAT_R8_UNORM);
    CheckRead(dispatch->transparencyAndComposition, 5, FFX_API_SURFACE_FORMAT_R8_UNORM);
    CheckRead(dispatch->exposure, 6, FFX_API_SURFACE_FORMAT_R32_FLOAT);
    assert(dispatch->output.state == FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(dispatch->motionVectorScale.x == 1.0f && dispatch->motionVectorScale.y == 1.0f);
    assert(dispatch->jitterOffset.x == 0.25f && dispatch->jitterOffset.y == -0.25f);
    assert(dispatch->frameTimeDelta == 16.0f && dispatch->preExposure == 2.0f);
    assert(dispatch->cameraFovAngleVertical == 1.0f);
  } else if (description->type == FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE) {
    const auto* dispatch = reinterpret_cast<const ffxDispatchDescFrameGenerationPrepare*>(description);
    prepared_sequence = dispatch->frameID;
    prepared_camera = description->pNext &&
        description->pNext->type == FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO;
    CheckRead(dispatch->depth, 2, FFX_API_SURFACE_FORMAT_R32_FLOAT);
    CheckRead(dispatch->motionVectors, 3, FFX_API_SURFACE_FORMAT_R16G16_FLOAT);
  } else if (description->type == FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION) {
    const auto* dispatch = reinterpret_cast<const ffxDispatchDescFrameGeneration*>(description);
    assert(prepared_camera && prepared_sequence == dispatch->frameID);
    CheckRead(dispatch->presentColor, 8, FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM);
    generation_resets.push_back(dispatch->reset);
    assert(dispatch->numGeneratedFrames == 1);
    assert(dispatch->outputs[0].resource == reinterpret_cast<void*>(9));
    assert(dispatch->outputs[0].state == FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
    assert(dispatch->backbufferTransferFunction == FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB);
  } else {
    assert(false);
  }
  return FFX_API_RETURN_OK;
}

int main() {
  auto physical = reinterpret_cast<VkPhysicalDevice>(1);
  auto device = reinterpret_cast<VkDevice>(1);
  auto command = reinterpret_cast<VkCommandBuffer>(1);
  auto frame = MakeFrame();
  Configuration configuration{frame.render_extent, frame.output_extent, Quality::kQuality, true};
  FsrUpscalerVulkan sr;
  assert(!sr.available());
  assert(sr.Initialize(physical, device, Resolver));
  Extent render;
  assert(sr.QueryRenderExtent(frame.output_extent, Quality::kQuality, render));
  assert(render == frame.render_extent);
  float x, y;
  assert(sr.QueryJitter(std::numeric_limits<uint64_t>::max(), render, frame.output_extent, x, y));
  assert(x == frame.jitter_x && y == frame.jitter_y);
  assert(sr.Configure(configuration) && sr.available());
  assert(sr.Encode(command, frame));
  assert(!sr.Encode(command, frame));  // Pending submit is not overwritten.
  sr.OnSubmitted();
  assert(!sr.Encode(command, frame));  // Duplicate scene is not accumulated.
  frame.sequence = 2; frame.time_ns = 2;
  assert(sr.Encode(command, frame));
  sr.OnSubmitted();
  frame.sequence = 3; frame.time_ns = 3;
  assert(sr.Encode(command, frame));
  sr.OnDiscarded();
  assert(!sr.Encode(command, frame));
  assert(sr.Configure(configuration));
  assert(sr.Encode(command, frame));
  sr.OnSubmitted();
  frame.sequence = 5; frame.time_ns = 5;
  assert(sr.Encode(command, frame));
  sr.OnSubmitted();
  assert((upscale_resets == std::vector<bool>{true, false, false, true, true}));
  frame.sequence = 6; frame.time_ns = 6; frame.motion_complete = false;
  assert(!sr.Encode(command, frame));
  frame.motion_complete = true;
  fail_dispatch = true;
  assert(!sr.Encode(command, frame));
  fail_dispatch = false;
  assert(sr.Configure(configuration));
  assert(sr.Encode(command, frame) && upscale_resets.back());
  sr.OnSubmitted();
  sr.Shutdown();
  assert(!sr.available());
  assert(sr.Initialize(physical, device, Resolver));
  fail_create = true;
  assert(!sr.Configure(configuration));
  const auto failed_attempts = create_count;
  assert(!sr.Configure(configuration) && create_count == failed_attempts);
  fail_create = false;

  FsrFrameGenerationVulkan fg;
  assert(fg.Initialize(physical, device, Resolver));
  configuration.hdr = false;
  assert(fg.Configure(configuration, VK_FORMAT_R8G8B8A8_UNORM));
  FsrFrameGenerationInput presentation;
  presentation.present_color = MakeImage(8, VK_FORMAT_R8G8B8A8_UNORM, frame.output_extent);
  presentation.generated_output = MakeImage(9, VK_FORMAT_R8G8B8A8_UNORM, frame.output_extent, true);
  presentation.maximum_luminance = 100.0f;
  presentation.camera_up[1] = 1.0f;
  presentation.camera_right[0] = 1.0f;
  presentation.camera_forward[2] = -1.0f;
  frame.sequence = 1; frame.time_ns = 1;
  assert(fg.Encode(command, frame, presentation) && !fg.has_generated_frame());
  fg.OnSubmitted();
  frame.sequence = 2; frame.time_ns = 2;
  assert(fg.Encode(command, frame, presentation) && fg.has_generated_frame());
  fg.OnDiscarded();
  assert(!fg.has_generated_frame());
  assert(!fg.Encode(command, frame, presentation));
  assert(fg.Configure(configuration, VK_FORMAT_R8G8B8A8_UNORM));
  assert(fg.Encode(command, frame, presentation) && !fg.has_generated_frame());
  fg.OnSubmitted();
  assert((generation_resets == std::vector<bool>{true, false, true}));
  presentation.camera_forward[2] = -2.0f;
  frame.sequence = 3; frame.time_ns = 3;
  assert(!fg.Encode(command, frame, presentation));
  fg.Shutdown();
  sr.Shutdown();
  assert(destroy_count == 5);
}
