// TEMP: per-frame Vulkan command counters (perf investigation). Included inside
// namespace rex::ui::vulkan by vulkan_device.cpp.
namespace command_counters {
std::atomic<uint64_t> barriers{0}, barrier_images{0}, renderings{0}, render_passes{0}, draws{0},
    dispatches{0}, copies{0}, clears{0}, binds{0};
PFN_vkCmdPipelineBarrier barrier_fn;
PFN_vkCmdBeginRendering rendering_fn;
PFN_vkCmdBeginRenderPass render_pass_fn;
PFN_vkCmdDraw draw_fn;
PFN_vkCmdDrawIndexed draw_indexed_fn;
PFN_vkCmdDispatch dispatch_fn;
PFN_vkCmdCopyImage copy_image_fn;
PFN_vkCmdBlitImage blit_image_fn;
PFN_vkCmdCopyBufferToImage copy_buffer_to_image_fn;
PFN_vkCmdClearAttachments clear_attachments_fn;
PFN_vkCmdBindPipeline bind_pipeline_fn;
VKAPI_ATTR void VKAPI_CALL Barrier(VkCommandBuffer cb, VkPipelineStageFlags a, VkPipelineStageFlags b,
                                   VkDependencyFlags d, uint32_t mc, const VkMemoryBarrier* m,
                                   uint32_t bc, const VkBufferMemoryBarrier* bm, uint32_t ic,
                                   const VkImageMemoryBarrier* im) {
  ++barriers;
  barrier_images += ic;
  barrier_fn(cb, a, b, d, mc, m, bc, bm, ic, im);
}
VKAPI_ATTR void VKAPI_CALL Rendering(VkCommandBuffer cb, const VkRenderingInfo* info) {
  ++renderings;
  rendering_fn(cb, info);
}
VKAPI_ATTR void VKAPI_CALL RenderPass(VkCommandBuffer cb, const VkRenderPassBeginInfo* info,
                                      VkSubpassContents c) {
  ++render_passes;
  render_pass_fn(cb, info, c);
}
VKAPI_ATTR void VKAPI_CALL Draw(VkCommandBuffer cb, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
  ++draws;
  draw_fn(cb, a, b, c, d);
}
VKAPI_ATTR void VKAPI_CALL DrawIndexed(VkCommandBuffer cb, uint32_t a, uint32_t b, uint32_t c,
                                       int32_t d, uint32_t e) {
  ++draws;
  draw_indexed_fn(cb, a, b, c, d, e);
}
VKAPI_ATTR void VKAPI_CALL Dispatch(VkCommandBuffer cb, uint32_t a, uint32_t b, uint32_t c) {
  ++dispatches;
  dispatch_fn(cb, a, b, c);
}
VKAPI_ATTR void VKAPI_CALL CopyImage(VkCommandBuffer cb, VkImage a, VkImageLayout b, VkImage c,
                                     VkImageLayout d, uint32_t e, const VkImageCopy* f) {
  ++copies;
  copy_image_fn(cb, a, b, c, d, e, f);
}
VKAPI_ATTR void VKAPI_CALL BlitImage(VkCommandBuffer cb, VkImage a, VkImageLayout b, VkImage c,
                                     VkImageLayout d, uint32_t e, const VkImageBlit* f, VkFilter g) {
  ++copies;
  blit_image_fn(cb, a, b, c, d, e, f, g);
}
VKAPI_ATTR void VKAPI_CALL CopyBufferToImage(VkCommandBuffer cb, VkBuffer a, VkImage b,
                                             VkImageLayout c, uint32_t d,
                                             const VkBufferImageCopy* e) {
  ++copies;
  copy_buffer_to_image_fn(cb, a, b, c, d, e);
}
VKAPI_ATTR void VKAPI_CALL ClearAttachments(VkCommandBuffer cb, uint32_t a, const VkClearAttachment* b,
                                            uint32_t c, const VkClearRect* d) {
  ++clears;
  clear_attachments_fn(cb, a, b, c, d);
}
VKAPI_ATTR void VKAPI_CALL BindPipeline(VkCommandBuffer cb, VkPipelineBindPoint a, VkPipeline b) {
  ++binds;
  bind_pipeline_fn(cb, a, b);
}
}  // namespace command_counters

std::string ConsumeCommandCounters(uint32_t frames) {
  using namespace command_counters;
  if (!frames) frames = 1;
  auto take = [&](std::atomic<uint64_t>& value) { return double(value.exchange(0)) / frames; };
  const double b = take(barriers), bi = take(barrier_images), r = take(renderings),
               rp = take(render_passes), d = take(draws), di = take(dispatches), c = take(copies),
               cl = take(clears), p = take(binds);
  return fmt::format(
      "barriers={:.0f} (images {:.0f}) renderings={:.0f} renderpasses={:.0f} draws={:.0f} "
      "pipeline-binds={:.0f} dispatches={:.0f} copies={:.0f} clears={:.0f}",
      b, bi, r, rp, d, p, di, c, cl);
}

static void InstallCommandCounters(VulkanDevice::Functions& dfn) {
#if defined(__ANDROID__)
  using namespace command_counters;
#define REX_WRAP(field, saved, wrapper) \
  if (dfn.field) {                      \
    saved = dfn.field;                  \
    dfn.field = wrapper;                \
  }
  REX_WRAP(vkCmdPipelineBarrier, barrier_fn, Barrier)
  REX_WRAP(vkCmdBeginRendering, rendering_fn, Rendering)
  REX_WRAP(vkCmdBeginRenderPass, render_pass_fn, RenderPass)
  REX_WRAP(vkCmdDraw, draw_fn, Draw)
  REX_WRAP(vkCmdDrawIndexed, draw_indexed_fn, DrawIndexed)
  REX_WRAP(vkCmdDispatch, dispatch_fn, Dispatch)
  REX_WRAP(vkCmdCopyImage, copy_image_fn, CopyImage)
  REX_WRAP(vkCmdBlitImage, blit_image_fn, BlitImage)
  REX_WRAP(vkCmdCopyBufferToImage, copy_buffer_to_image_fn, CopyBufferToImage)
  REX_WRAP(vkCmdClearAttachments, clear_attachments_fn, ClearAttachments)
  REX_WRAP(vkCmdBindPipeline, bind_pipeline_fn, BindPipeline)
#undef REX_WRAP
#else
  (void)dfn;
#endif
}
