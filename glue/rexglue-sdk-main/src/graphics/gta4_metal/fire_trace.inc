// Opt-in observation only. Readbacks split encoders, retain the original target
// formats, and never replace guest state, shader outputs, or attachment contents.
namespace {
bool FireFiniteEnabled() {
  static const bool enabled = gta4_native::FireTraceUnsigned("REX_GTA4_FIRE_FINITE_SCAN", 0, 1) != 0;
  return enabled;
}
template<class Range> std::string FireWords(const Range& values) {
  std::string result;
  for (const auto value : values) result += fmt::format("{}{:08X}", result.empty() ? "" : ",", value);
  return result;
}
}

void Renderer::State::FireAdvance(uint32_t guest_frame) {
  using namespace gta4_native;
  if (!FireTraceConfig().enabled) return;
  if (fire_active) FireTraceLog("metal-frame-end", fmt::format(
      "capture={} guest-frame={} draws={} material-draws={} snapshots={} bytes={}",
      fire_capture, guest_frame, fire_draw, fire_material_draws, fire_snapshot, fire_bytes));
  fire_active = false;
  const auto& config = FireTraceConfig();
  if (config.request_file.empty() || config.directory.empty()) return;
  std::ifstream request(config.request_file);
  std::string token;
  for (char ch; token.size() < 128 && request.get(ch) && ch != '\n';) token.push_back(ch);
  if (token.empty() || token == fire_request) return;
  fire_request = token;
  std::error_code failure;
  std::filesystem::create_directories(config.directory, failure);
  if (failure) { FireTraceLog("metal-capture-error", failure.message()); return; }
  fire_active = true; fire_hud_seen = false;
  fire_scan = 0;
  fire_detail_first = fire_detail_last = 0;
  fire_detail_pixel_shader = 0;
  fire_detail_shader_match = false;
  // Optional subsequent lines select a draw interval for raw target snapshots.
  // The first line remains the unique trigger token, so another capture needs
  // no restart when the first finite-to-NaN transition has been located.
  request >> fire_detail_first >> fire_detail_last;
  // An optional hexadecimal pixel-shader hash selects full-size before/input/
  // after images independently of draw numbering and the regular crop.
  request >> std::hex >> fire_detail_pixel_shader;
  ++fire_capture; fire_draw = fire_snapshot = fire_bytes = fire_draw_bytes = fire_material_draws = 0;
  fire_last_targets = {};
  // The existing optional Xcode capture also records the next submission.
  if (!capture_attempted) rex_gta4_metal_capture_start();
  FireTraceLog("metal-frame-armed", fmt::format(
      "capture={} after-guest-frame={} next-frame=true directory={} crop={},{},{},{} detail-ps={:016X}",
      fire_capture, guest_frame, config.directory, config.crop_x, config.crop_y,
      config.crop_width, config.crop_height, fire_detail_pixel_shader));
}

bool Renderer::State::FireDetailed() const {
  return fire_detail_shader_match ||
      (fire_detail_first && fire_draw >= fire_detail_first && fire_draw <= fire_detail_last);
}

void Renderer::State::FireInspect(id<MTLTexture> image, std::string_view label,
    uint32_t handle, uint64_t draw) {
  using namespace gta4_native;
  if (!fire_active || !FireFiniteEnabled() || !commands || !image) return;
  switch (image.pixelFormat) {
    case MTLPixelFormatR16Float: case MTLPixelFormatRG16Float:
    case MTLPixelFormatRGBA16Float: case MTLPixelFormatR32Float:
    case MTLPixelFormatRG32Float: case MTLPixelFormatRGBA32Float: break;
    default: return; // Integer/normalized color formats cannot store NaN/Inf.
  }
  const auto metadata = fmt::format(
      "capture={} scan={} draw={} label={} handle={:08X} image={} format={} extent={}x{}",
      fire_capture, ++fire_scan, draw, label, handle,
      uint64_t(reinterpret_cast<uintptr_t>((__bridge void*)image)),
      uint32_t(image.pixelFormat), image.width, image.height);
  if (image.textureType != MTLTextureType2D || image.sampleCount != 1) {
    FireTraceLog("metal-finite-skip", metadata + " reason=multisample-or-non-2d"); return;
  }
  if (!fire_scan_pipeline || !fire_sample_pipeline) {
    NSError* failure = nil;
    fire_scan_pipeline = [context->device newComputePipelineStateWithFunction:
        [native_library newFunctionWithName:@"liberty_trace_finite"] error:&failure];
    fire_sample_pipeline = [context->device newComputePipelineStateWithFunction:
        [native_library newFunctionWithName:@"liberty_trace_nonfinite_sample"] error:&failure];
    if (!fire_scan_pipeline || !fire_sample_pipeline) {
      FireTraceLog("metal-finite-error", metadata + " reason=" +
          ui::metal::MetalError(failure, "finite scan pipeline creation failed")); return;
    }
  }
  // Only a compact result is read back. Every texel is scanned on the GPU;
  // counts and locations are determined with integer IEEE bit classification.
  const std::array<uint32_t,12> initial{0,0,UINT32_MAX,UINT32_MAX,0,0,0,0,0,0,0,0};
  auto result = [context->device newBufferWithBytes:initial.data() length:sizeof(initial)
      options:MTLResourceStorageModeShared];
  if (!result) { FireTraceLog("metal-finite-error", metadata + " reason=allocation-failed"); return; }
  EndRender();
  auto encoder = [commands computeCommandEncoder];
  if (!encoder) { FireTraceLog("metal-finite-error", metadata + " reason=encoder-failed"); return; }
  encoder.label = @"Rail diagnostic: locate first nonfinite color";
  [encoder setComputePipelineState:fire_scan_pipeline];
  [encoder setTexture:image atIndex:0]; [encoder setBuffer:result offset:0 atIndex:0];
  [encoder dispatchThreads:MTLSizeMake(32768,1,1) threadsPerThreadgroup:MTLSizeMake(256,1,1)];
  [encoder memoryBarrierWithScope:MTLBarrierScopeBuffers];
  [encoder setComputePipelineState:fire_sample_pipeline];
  [encoder dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
  [encoder endEncoding];
  const auto width = uint32_t(image.width);
  FireTraceLog("metal-finite-queued", metadata);
  [commands addCompletedHandler:^(id<MTLCommandBuffer> completed) {
    if (completed.status != MTLCommandBufferStatusCompleted) {
      FireTraceLog("metal-finite-error", metadata + " reason=gpu-submission-failed"); return;
    }
    const auto* values = static_cast<const uint32_t*>(result.contents);
    FireTraceLog("metal-finite", metadata + fmt::format(
        " nan-pixels={} inf-pixels={} first-nan={},{} first-inf={},{} nan-bits={:08X},{:08X},{:08X},{:08X} inf-bits={:08X},{:08X},{:08X},{:08X}",
        values[0],values[1],values[2]==UINT32_MAX?UINT32_MAX:values[2]%width,
        values[2]==UINT32_MAX?UINT32_MAX:values[2]/width,
        values[3]==UINT32_MAX?UINT32_MAX:values[3]%width,
        values[3]==UINT32_MAX?UINT32_MAX:values[3]/width,
        values[4],values[5],values[6],values[7],values[8],values[9],values[10],values[11]));
  }];
}

void Renderer::State::FireInspectTargets(const Targets& targets, std::string_view label) {
  if (!fire_active || !FireFiniteEnabled()) return;
  for (size_t i=0; i<targets.colors.size(); ++i) {
    const auto& target = targets.colors[i];
    if (target && target->initialized && !target->pending_clear.aspects)
      FireInspect(target->image, fmt::format("{}-rt{}",label,i), target->descriptor.handle,fire_draw);
  }
}

void Renderer::State::FireSnapshot(id<MTLTexture> image, std::string_view label,
    uint32_t handle, bool per_draw, bool full) {
  using namespace gta4_native;
  if (!fire_active || !commands || !image) return;
  if (fire_detail_shader_match && label.starts_with("detail-")) full = true;
  if (image.textureType != MTLTextureType2D || image.sampleCount != 1) {
    FireTraceLog("metal-image-skip", fmt::format("capture={} draw={} label={} handle={:08X} reason=multisample-or-non-2d samples={}",
        fire_capture, fire_draw, label, handle, image.sampleCount)); return;
  }
  const bool depth = image.pixelFormat == MTLPixelFormatDepth32Float_Stencil8;
  size_t pixel_bytes = 0;
  const char* kind = nullptr;
  switch (image.pixelFormat) {
    case MTLPixelFormatRGBA8Unorm: case MTLPixelFormatRGBA8Unorm_sRGB: pixel_bytes = 4; kind = "rgba8"; break;
    case MTLPixelFormatBGRA8Unorm: case MTLPixelFormatBGRA8Unorm_sRGB: pixel_bytes = 4; kind = "bgra8"; break;
    case MTLPixelFormatRGBA16Float: pixel_bytes = 8; kind = "rgba16f"; break;
    case MTLPixelFormatRG16Float: pixel_bytes = 4; kind = "rg16f"; break;
    case MTLPixelFormatR32Float: pixel_bytes = 4; kind = "r32f"; break;
    case MTLPixelFormatDepth32Float_Stencil8: pixel_bytes = 4; kind = "depth32f"; break;
    default: FireTraceLog("metal-image-skip", fmt::format("capture={} draw={} label={} handle={:08X} reason=unsupported-format format={}",
        fire_capture, fire_draw, label, handle, uint32_t(image.pixelFormat))); return;
  }
  const auto& config = FireTraceConfig();
  const size_t x = full ? 0 : std::min<size_t>(config.crop_x, image.width);
  const size_t y = full ? 0 : std::min<size_t>(config.crop_y, image.height);
  const size_t width = full || !config.crop_width ? image.width - x : std::min<size_t>(config.crop_width, image.width - x);
  const size_t height = full || !config.crop_height ? image.height - y : std::min<size_t>(config.crop_height, image.height - y);
  if (!width || !height) return;
  for (unsigned aspect = 0; aspect < (depth ? 2u : 1u); ++aspect) {
    const size_t bpp = aspect ? 1 : pixel_bytes;
    const char* encoding = aspect ? "stencil8" : kind;
    const size_t pitch = (width * bpp + 255) & ~size_t(255);
    const uint64_t bytes = uint64_t(pitch) * height;
    const uint64_t limit = uint64_t(config.image_frame_mib) * 1048576;
    const uint64_t disk_limit = uint64_t(config.disk_mib) * 1048576;
    // Reserve room for full final output; per-material images have their own
    // smaller budget so later lighting and HUD checkpoints remain observable.
    const uint64_t allowed = full ? limit : limit - std::min<uint64_t>(limit / 2, 67108864);
    if (fire_bytes > allowed || bytes > allowed - fire_bytes ||
        fire_disk_bytes > disk_limit || bytes > disk_limit - fire_disk_bytes ||
        (per_draw && (fire_draw_bytes > limit / 3 || bytes > limit / 3 - fire_draw_bytes))) {
      FireTraceLog("metal-image-skip", fmt::format("capture={} draw={} label={} reason=budget bytes={}", fire_capture, fire_draw, label, bytes));
      continue;
    }
    auto buffer = [context->device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    if (!buffer) { FireTraceLog("metal-capture-error", "staging-allocation-failed"); return; }
    EndRender();
    auto blit = [commands blitCommandEncoder];
    if (!blit) { FireTraceLog("metal-capture-error", "blit-encoder-failed"); return; }
    blit.label = @"Rail/HUD diagnostic snapshot";
    const auto option = !depth ? MTLBlitOptionNone : aspect ? MTLBlitOptionStencilFromDepthStencil : MTLBlitOptionDepthFromDepthStencil;
    [blit copyFromTexture:image sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(x,y,0)
        sourceSize:MTLSizeMake(width,height,1) toBuffer:buffer destinationOffset:0
        destinationBytesPerRow:pitch destinationBytesPerImage:bytes options:option];
    [blit endEncoding];
    fire_bytes += bytes; fire_disk_bytes += bytes; if (per_draw) fire_draw_bytes += bytes;
    const auto capture = fire_capture, draw = fire_draw, snapshot = ++fire_snapshot;
    const std::string path = (std::filesystem::path(config.directory) /
        fmt::format("c{}-d{}-s{}-{}-{}.bin", capture, draw, snapshot, label, encoding)).string();
    const auto metadata = fmt::format(
        "capture={} draw={} snapshot={} label={} handle={:08X} format={} encoding={} source={}x{} origin={},{} extent={}x{} pitch={} bytes={} path={}",
        capture, draw, snapshot, label, handle, uint32_t(image.pixelFormat), encoding,
        image.width, image.height, x, y, width, height, pitch, bytes, path);
    FireTraceLog("metal-image-queued", metadata);
    [commands addCompletedHandler:^(id<MTLCommandBuffer> completed) {
      if (completed.status != MTLCommandBufferStatusCompleted) {
        FireTraceLog("metal-image-error", metadata + " reason=gpu-submission-failed"); return;
      }
      std::ofstream output(path, std::ios::binary);
      output.write(static_cast<const char*>(buffer.contents), std::streamsize(bytes));
      output.close();
      FireTraceLog(output ? "metal-image-ready" : "metal-image-error", metadata);
    }];
  }
}

void Renderer::State::FireTargets(const Targets& targets, std::string_view label, bool per_draw) {
  if (!fire_active) return;
  for (size_t i = 0; i < targets.colors.size(); ++i) {
    const auto& surface = targets.colors[i];
    if (surface && surface->initialized && !surface->pending_clear.aspects)
      FireSnapshot(surface->image, fmt::format("{}-rt{}", label, i), surface->descriptor.handle, per_draw);
  }
  if (targets.depth && targets.depth->initialized && !targets.depth->pending_clear.aspects)
    FireSnapshot(targets.depth->image, fmt::format("{}-ds", label), targets.depth->descriptor.handle, per_draw);
}

void Renderer::State::FireBeforeDraw(const Targets& targets, const FixedState& fixed,
    const Pipeline& pipeline, std::span<const uint8_t> guest, uint32_t primitive, uint32_t count, int32_t base_vertex) {
  using namespace gta4_native;
  if (!fire_active) return;
  ++fire_draw;
  fire_detail_shader_match = fire_detail_pixel_shader &&
      pipeline.pixel.hash == fire_detail_pixel_shader;
  const auto& c = fire_context;
  const bool hud = pipeline.pixel.name.find("gta_im_") != std::string::npos;
  if (FireFiniteEnabled()) {
    const auto path = (std::filesystem::path(FireTraceConfig().directory) /
        fmt::format("c{}-d{}-guest-state.bin",fire_capture,fire_draw)).string();
    std::ofstream output(path,std::ios::binary);
    output.write(reinterpret_cast<const char*>(guest.data()),std::streamsize(guest.size()));
    output.close();
    FireTraceLog(output?"metal-guest-state":"metal-capture-error",fmt::format(
        "capture={} draw={} bytes={} endian=big path={}",fire_capture,fire_draw,guest.size(),path));
  }
  FireTraceLog("metal-draw", fmt::format(
      "capture={} draw={} batch={} guest-frame={} event={} occurrence={} phase={} model={:08X} geometry={:08X} material={:08X} ordinal={} bucket={} texture={} technique={:08X} pass={:08X} replay={} list={:08X} replay-ordinal={} vs={:016X}:{} ps={:016X}:{} variant={} hud={} primitive={} count={} base={} declaration={:08X} ib={:08X} extent={}x{} samples={} color-mask={:04X} depth={}:{}:{} stencil={}:{}:{}:{:02X}:{:02X} alpha={}:{}:{} atoc={:03X} blend={}:{:08X},{:08X},{:08X},{:08X} viewport={} scissor={},{},{},{} vs-constants={:016X} ps-constants={:016X} booleans={:08X},{:08X}",
      fire_capture,fire_draw,submitted,c.guest_frame,c.event,c.occurrence,uint32_t(Phase()),c.model,c.geometry,c.material,c.ordinal,c.bucket,c.texture_name.data(),c.technique,c.pass,
      c.replay,c.command_list,c.replay_ordinal,pipeline.vertex.hash,pipeline.vertex.name,pipeline.pixel.hash,pipeline.pixel.name,pipeline.variant,hud,primitive,count,base_vertex,declaration,index_buffer,
      targets.width,targets.height,targets.samples,fixed.color_write_mask,fixed.depth_enable,fixed.depth_write_enable,fixed.depth_function,
      fixed.stencil_enable,fixed.stencil_function,fixed.stencil_reference,fixed.stencil_mask,fixed.stencil_write_mask,
      fixed.alpha_test_enable,fixed.alpha_function,fixed.alpha_reference,fixed.alpha_to_mask,fixed.blend_enable,
      fixed.blend_controls[0],fixed.blend_controls[1],fixed.blend_controls[2],fixed.blend_controls[3],FireWords(fixed.viewport_bits),
      fixed.scissor[0],fixed.scissor[1],fixed.scissor[2],fixed.scissor[3],XXH3_64bits(guest.data()+0x780,0x1000),XXH3_64bits(guest.data()+0x1780,0xE00),GuestWord(guest,0x2780),GuestWord(guest,0x2790)));
  for (size_t i=0;i<targets.colors.size();++i) if (const auto& target=targets.colors[i])
    FireTraceLog("metal-target",fmt::format("capture={} draw={} slot={} handle={:08X} generation={} serial={} guest-info={:08X} format={} initialized={} pending-clear={}",
        fire_capture,fire_draw,i,target->descriptor.handle,target->generation,target->content_serial,target->descriptor.address,uint32_t(target->image.pixelFormat),target->initialized,target->pending_clear.aspects));
  if (targets.depth) FireTraceLog("metal-depth",fmt::format("capture={} draw={} handle={:08X} generation={} serial={} format={}",
      fire_capture,fire_draw,targets.depth->descriptor.handle,targets.depth->generation,targets.depth->content_serial,uint32_t(targets.depth->image.pixelFormat)));
  if (c.replay) {
    for (const auto& [name,state] : {std::pair{"live",&c.replay_live},std::pair{"applied",&c.replay_applied}})
      FireTraceLog("metal-replay-state",fmt::format("capture={} draw={} event={} state={} vs={:016X} ps={:016X} rt={} ds={:08X} viewport={} c8-c11={} c46={}",
          fire_capture,fire_draw,c.event,name,state->vertex_constants,state->pixel_constants,FireWords(state->colors),state->depth,FireWords(state->viewport),FireWords(state->transform),FireWords(state->lighting_c46)));
  }
  FireInspectTargets(targets,"draw-before");
  if (c.occurrence) { ++fire_material_draws; if (!FireFiniteEnabled()) FireTargets(targets,"material-before",true); }
  if (FireDetailed()) FireTargets(targets,"detail-before");
  if (hud && !fire_hud_seen && targets.samples==1 && targets.width>640) {
    fire_hud_seen=true; FireTargets(targets,"hud-before");
  }
  fire_last_targets=targets;
}
void Renderer::State::FireAfterDraw(const Targets& targets) {
  if (fire_active) gta4_native::FireTraceLog("metal-draw-encoded",fmt::format(
      "capture={} draw={} event={}",fire_capture,fire_draw,fire_context.event));
  FireInspectTargets(targets,"draw-after");
  if (fire_active && fire_context.occurrence && !FireFiniteEnabled()) FireTargets(targets,"material-after",true);
  if (fire_active && FireDetailed()) FireTargets(targets,"detail-after");
}
void Renderer::State::FirePhase(const gta4_native::RenderPhaseMarkerCommand& marker) {
  using namespace gta4_native;
  if (!fire_active) return;
  FireTraceLog("metal-phase",fmt::format("capture={} draw={} phase={} event={} object={:08X}",
      fire_capture,fire_draw,uint32_t(marker.phase),uint32_t(marker.event),marker.object));
  if (marker.event==RenderPhaseEvent::kEnd)
    FireTargets(fire_last_targets,fmt::format("phase{}-end",uint32_t(marker.phase)));
}
