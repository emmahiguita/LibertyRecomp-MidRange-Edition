// Included by metal_output_gpu_test.mm after its Fixture and presentation
// constant adapters. Include presentation_image_cache.h, immediate_drawer.h
// and ui_draw_context.h from ui/metal at file scope.
void TestPresentationCachePolicy() {
  using Action = PresentationCachePolicy::Action;
  PresentationImageKey key{};
  key.publication = 1; key.source = 1; key.texture_version = 1;
  key.source_width = key.source_height = key.output_width = key.output_height = 17;
  key.frontbuffer_width = key.frontbuffer_height = 17;
  key.aspect_x = key.aspect_y = 1; key.active = true;
  struct Flow {
    size_t effect_count = 6;
    std::array<uint32_t, 6> effects{1, 1, 1, 1, 1, 1};
    std::array<std::pair<uint32_t, uint32_t>, 6> effect_output_sizes;
    int32_t output_x = 3, output_y = 2;
  } flow;
  flow.effect_output_sizes.fill({17, 17}); key.SetFlow(flow);
  PresentationCachePolicy policy;
  Check(policy.Inspect(key) == Action::kDirect, "First presentation added a cache pass");
  policy.Submitted(key, Action::kDirect);
  Check(policy.Inspect(key) == Action::kPopulate, "First repeat did not populate the cache");
  Check(policy.Inspect(key) == Action::kPopulate, "Unsubmitted population became reusable");
  policy.Submitted(key, Action::kPopulate);
  Check(policy.Inspect(key) == Action::kReuse, "Submitted pixels were not reusable");
  const auto changed = [&](auto modify) {
    auto candidate = key; modify(candidate);
    Check(policy.Inspect(candidate) == Action::kDirect, "Changed pixel identity reused stale output");
  };
#define CACHE_CHANGE(member, value) changed([](auto& k) { k.member = value; })
  CACHE_CHANGE(publication, 2); CACHE_CHANGE(surface_epoch, 1); CACHE_CHANGE(texture_version, 2);
  CACHE_CHANGE(source, 2); CACHE_CHANGE(source_format, 1); CACHE_CHANGE(output_format, 1);
  CACHE_CHANGE(source_width, 23); CACHE_CHANGE(source_height, 23);
  CACHE_CHANGE(output_width, 23); CACHE_CHANGE(output_height, 23);
  CACHE_CHANGE(frontbuffer_width, 23); CACHE_CHANGE(frontbuffer_height, 23);
  CACHE_CHANGE(aspect_x, 4); CACHE_CHANGE(aspect_y, 3);
  CACHE_CHANGE(effect, 1); CACHE_CHANGE(fsr_quality, 1); CACHE_CHANGE(fsr_passes, 1);
  CACHE_CHANGE(flow_effect_count, 1); CACHE_CHANGE(flow_x, 4); CACHE_CHANGE(flow_y, 3);
  CACHE_CHANGE(cas_sharpness, 0.5f); CACHE_CHANGE(fsr_sharpness, 0.5f);
  CACHE_CHANGE(hdr_mode, 1); CACHE_CHANGE(output_mode, 1);
  CACHE_CHANGE(active, false); CACHE_CHANGE(is_8bpc, true);
  CACHE_CHANGE(overscan, true); CACHE_CHANGE(dither, true);
#undef CACHE_CHANGE
  for (size_t i = 0; i < key.hdr.size(); ++i)
    changed([i](auto& k) { k.hdr[i] = 4; });
  for (size_t i = 0; i < flow.effects.size(); ++i) {
    changed([&](auto& k) { auto f = flow; f.effects[i] = 2; k.SetFlow(f); });
    changed([&](auto& k) { auto f = flow; f.effect_output_sizes[i].first = 23; k.SetFlow(f); });
    changed([&](auto& k) { auto f = flow; f.effect_output_sizes[i].second = 23; k.SetFlow(f); });
  }
  auto inactive = key; flow.effect_count = 0; inactive.SetFlow(flow);
  Check(inactive.flow_x == 0 && inactive.flow_y == 0 &&
        inactive.flow_effects == decltype(inactive.flow_effects){}, "Inactive layout retained unused geometry");
  auto next = key; next.publication = 2;
  policy.Submitted(next, Action::kDirect);
  Check(policy.Inspect(key) == Action::kDirect, "New publication left an older image reusable");
  Check(policy.Inspect(next) == Action::kPopulate, "New publication could not populate on repeat");
  policy.Submitted(next, Action::kPopulate);
  policy.InvalidatePixels();
  Check(policy.Inspect(next) == Action::kPopulate, "Replaced texture retained cached pixels");
  policy.Reset();
  Check(policy.Inspect(next) == Action::kDirect, "Reconnect reused prior surface history");
  next.publication = 0; policy.Submitted(next, Action::kDirect);
  Check(policy.Inspect(next) == Action::kDirect, "Anonymous image was cached across updates");
}

id<MTLTexture> CacheTestTarget(Fixture& f, NSUInteger width, MTLPixelFormat format) {
  auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
      width:width height:width mipmapped:NO];
  descriptor.storageMode = MTLStorageModePrivate;
  descriptor.hazardTrackingMode = MTLHazardTrackingModeTracked;
  descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
  auto image = [f.context->device newTextureWithDescriptor:descriptor];
  Check(image != nil, "Cache test image allocation failed"); return image;
}

id<MTLRenderCommandEncoder> CacheTestEncoder(id<MTLCommandBuffer> commands,
    id<MTLTexture> image, MTLLoadAction load = MTLLoadActionClear,
    MTLClearColor color = MTLClearColorMake(0, 0, 0, 1)) {
  auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
  pass.colorAttachments[0].texture = image;
  pass.colorAttachments[0].loadAction = load;
  pass.colorAttachments[0].storeAction = MTLStoreActionStore;
  pass.colorAttachments[0].clearColor = color;
  auto encoder = [commands renderCommandEncoderWithDescriptor:pass];
  Check(encoder != nil, "Cache test encoder allocation failed"); return encoder;
}

struct CacheReadback {
  id<MTLBuffer> buffer = nil;
  size_t width = 0, height = 0, pixel_size = 0, pitch = 0;
  std::vector<uint8_t> Pixels() const {
    std::vector<uint8_t> pixels(width * height * pixel_size);
    for (size_t y = 0; y < height; ++y)
      std::memcpy(pixels.data() + y * width * pixel_size,
          static_cast<const uint8_t*>(buffer.contents) + y * pitch, width * pixel_size);
    return pixels;
  }
};
CacheReadback CacheRead(Fixture& f, id<MTLCommandBuffer> commands, id<MTLTexture> image) {
  CacheReadback result{};
  result.width = image.width; result.height = image.height;
  result.pixel_size = image.pixelFormat == MTLPixelFormatRGBA16Float ? 8 : 4;
  result.pitch = (result.width * result.pixel_size + 255) & ~size_t(255);
  result.buffer = [f.context->device newBufferWithLength:result.pitch * result.height
      options:MTLResourceStorageModeShared];
  Check(result.buffer != nil, "Cache test readback allocation failed");
  auto blit = [commands blitCommandEncoder]; Check(blit != nil, "Cache test readback encoder failed");
  [blit copyFromTexture:image sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
      sourceSize:MTLSizeMake(result.width, result.height, 1) toBuffer:result.buffer destinationOffset:0
      destinationBytesPerRow:result.pitch destinationBytesPerImage:result.pitch * result.height];
  [blit endEncoding]; return result;
}
void CacheFinish(id<MTLCommandBuffer> commands, const std::string& context = {}) {
  [commands commit]; [commands waitUntilCompleted];
  Check(commands.status == MTLCommandBufferStatusCompleted,
        context + ": " + MetalError(commands.error, "Cache test GPU failure"));
  ++cases;
}

void TestPresentationCachePixels(Fixture& f) {
  PresentationEffects effects; Check(effects.Initialize(f.context, f.error), f.error);
  PresentationImageCache cache; Check(cache.Initialize(f.context, f.error), f.error);
  auto drawer = MetalImmediateDrawer::Create(f.context, f.error); Check(bool(drawer), f.error);
  const std::array<uint8_t, 4> ui_texel{128, 224, 64, 128};
  auto ui_texture = drawer->CreateTexture(1, 1, rex::ui::ImmediateTextureFilter::kNearest, false, ui_texel.data());
  Check(bool(ui_texture), "Production UI test texture allocation failed");
  for (NSUInteger width : {17u, 32u}) for (uint32_t mode : {0u, 1u, 2u})
      for (bool letterbox : {false, true}) for (float headroom : {1.0f, 4.0f}) {
    const std::string case_name = "presentation_cache width=" + std::to_string(width) +
        " format=" + (mode ? "RGBA16Float" : "BGRA8Unorm") +
        " hdr_mode=" + std::to_string(mode) + " letterbox=" + std::to_string(letterbox) +
        " headroom=" + std::to_string(headroom);
    const auto check = [&](bool value, const std::string& message) { Check(value, case_name + ": " + message); };
    const auto equal_pixels = [&](const std::vector<uint8_t>& expected,
                                  const std::vector<uint8_t>& actual, const char* message) {
      check(expected.size() == actual.size(), "Pixel readback sizes differ");
      size_t first = expected.size(), changed = 0;
      for (size_t i = 0; i < expected.size(); ++i) if (expected[i] != actual[i]) {
        if (first == expected.size()) first = i;
        ++changed;
      }
      std::string detail = message;
      if (changed) detail += " changed_bytes=" + std::to_string(changed) +
          " first_byte=" + std::to_string(first) + " expected=" + std::to_string(expected[first]) +
          " actual=" + std::to_string(actual[first]);
      check(changed == 0, detail);
    };
    std::vector<Pixel> pixels(width * width);
    for (size_t y = 0; y < width; ++y) for (size_t x = 0; x < width; ++x)
      pixels[y * width + x] = {float(x) / float(width - 1), float(y) / float(width - 1),
          float((x + y) % 3) / 2.0f, 0.75f};
    auto source = f.Source(width, pixels);
    UploadArena ui_uploads;
    const auto format = mode ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm;
    auto reference = CacheTestTarget(f, width, format);
    auto display = CacheTestTarget(f, width, format);
    auto perceptual = f.Target(width);
    auto cached = cache.Prepare(uint32_t(width), uint32_t(width), format, f.error);
    check(cached != nil, f.error);
    check(cache.Prepare(uint32_t(width), uint32_t(width), format, f.error) == cached,
          "Unchanged cache extent allocated another image");
    PresentationConstants::GuestOutputPaintFlow flow{};
    flow.effect_count = 1; flow.properties.frontbuffer_width = flow.properties.frontbuffer_height = uint32_t(width);
    flow.output_x = flow.output_y = letterbox ? 3 : 0;
    const uint32_t extent = uint32_t(width) - (letterbox ? 6 : 0);
    flow.effect_output_sizes[0] = {extent, extent};
    PresentationConstants::BilinearConstants constants{}; constants.Initialize(flow, 0);
    const auto game = [&](id<MTLCommandBuffer> commands, id<MTLTexture> destination) {
      auto input = source;
      id<MTLRenderCommandEncoder> encoder = nil;
      if (!mode || letterbox) {
        auto target = mode ? perceptual : destination;
        encoder = CacheTestEncoder(commands, target);
        check(effects.Draw(encoder, mode ? PresentationEffect::kBilinear : PresentationEffect::kBilinearDither,
            source, target, MTLViewport{double(flow.output_x), double(flow.output_y), double(extent), double(extent), 0, 1},
            std::as_bytes(std::span(&constants, 1)), f.error), f.error);
        if (mode) [encoder endEncoding];
        input = target;
      }
      if (mode) {
        encoder = CacheTestEncoder(commands, destination);
        OutputTransferConstants hdr{}; hdr.hdr_mode = mode; hdr.hdr_headroom = headroom;
        check(f.transfer.Draw(encoder, input, destination, hdr, f.error), f.error);
      }
      // Keep the final game pass open for the production UI blend, matching
      // the presenter's direct path rather than adding a store/load boundary.
      return encoder;
    };
    const auto ui = [&](id<MTLRenderCommandEncoder> encoder, uint32_t offset) {
      MetalUIDrawContext context{uint32_t(width), uint32_t(width), encoder, ui_uploads};
      context.target_format = format; context.linear_output = mode != 0;
      const float a = float(offset), b = float(offset + 3);
      constexpr uint32_t color = 0xC0A0C040;  // RGBA bytes 64,192,160,192.
      const std::array<rex::ui::ImmediateVertex, 6> vertices{{
          {a, a, 0, 0, color}, {b, a, 1, 0, color}, {a, b, 0, 1, color},
          {a, b, 0, 1, color}, {b, a, 1, 0, color}, {b, b, 1, 1, color}}};
      drawer->Begin(context, float(width), float(width));
      rex::ui::ImmediateDrawBatch batch{};
      batch.vertices = vertices.data(); batch.vertex_count = int(vertices.size());
      drawer->BeginDrawBatch(batch);
      // The drawer dynamically checks this private context type. The test
      // target must compile the production drawer locally, as the application's
      // presenter and drawer also share a module. A rejected cross-DSO context
      // silently skips BeginDrawBatch and otherwise looks like a GPU failure.
      check(ui_uploads.reserved_bytes() != 0,
            "Production drawer rejected the UI context or failed its vertex upload; offset=" + std::to_string(offset));
      rex::ui::ImmediateDraw draw{}; draw.count = int(vertices.size()); draw.texture = ui_texture.get();
      drawer->Draw(draw); drawer->End();
    };
    auto commands = [f.context->queue commandBuffer];
    commands.label = [NSString stringWithUTF8String:case_name.c_str()];
    auto encoder = game(commands, reference); [encoder endEncoding];
    auto original = CacheRead(f, commands, reference);
    encoder = game(commands, cached); [encoder endEncoding];
    encoder = CacheTestEncoder(commands, display, MTLLoadActionDontCare);
    check(cache.Draw(encoder, display, f.error), f.error); [encoder endEncoding];
    auto copy = CacheRead(f, commands, display);
    encoder = game(commands, reference); ui(encoder, 2); [encoder endEncoding];
    encoder = CacheTestEncoder(commands, display, MTLLoadActionDontCare);
    check(cache.Draw(encoder, display, f.error), f.error); ui(encoder, 2); [encoder endEncoding];
    auto reference_ui = CacheRead(f, commands, reference), copied_ui = CacheRead(f, commands, display);
    CacheFinish(commands, case_name);
    const auto original_pixels = original.Pixels(), copied_pixels = copy.Pixels();
    const auto reference_ui_pixels = reference_ui.Pixels(), copied_ui_pixels = copied_ui.Pixels();
    equal_pixels(original_pixels, copied_pixels, "Cached transfer changed SDR/dither/HDR/letterbox pixels");
    equal_pixels(reference_ui_pixels, copied_ui_pixels, "Cached game image changed production alpha/HDR UI composition");
    check(reference_ui_pixels != original_pixels, "Production UI draw did not cover the game image; offset=2 upload_bytes=" +
          std::to_string(ui_uploads.reserved_bytes()));
    ui_uploads.Reset();
    commands = [f.context->queue commandBuffer];
    commands.label = [NSString stringWithUTF8String:(case_name + " moving_ui").c_str()];
    encoder = CacheTestEncoder(commands, display, MTLLoadActionDontCare);
    check(cache.Draw(encoder, display, f.error), f.error); ui(encoder, 7); [encoder endEncoding];
    auto moved_ui = CacheRead(f, commands, display);
    auto unchanged_game = CacheRead(f, commands, cache.image()); CacheFinish(commands, case_name + " moving_ui");
    equal_pixels(original_pixels, unchanged_game.Pixels(), "UI pixels contaminated the persistent game cache");
    check(moved_ui.Pixels() != copied_ui_pixels, "UI stopped updating on a repeated game publication; offset=7");
  }
}

void TestPresentationCacheQueuedLifetime(Fixture& f) {
  PresentationImageCache cache; Check(cache.Initialize(f.context, f.error), f.error);
  auto image = cache.Prepare(17, 17, MTLPixelFormatRGBA16Float, f.error); Check(image != nil, f.error);
  auto display = CacheTestTarget(f, 17, MTLPixelFormatRGBA16Float);
  struct ReleaseQueue {
    id<MTLSharedEvent> event;
    ~ReleaseQueue() { event.signaledValue = 1; }
  } release{[f.context->device newSharedEvent]};
  Check(release.event != nil, "Cache lifetime event unavailable");
  auto first = [f.context->queue commandBuffer];
  [first encodeWaitForEvent:release.event value:1];
  auto encoder = CacheTestEncoder(first, image, MTLLoadActionClear, MTLClearColorMake(0.125, 0.25, 0.5, 0.75));
  [encoder endEncoding];
  encoder = CacheTestEncoder(first, display, MTLLoadActionDontCare);
  Check(cache.Draw(encoder, display, f.error), f.error); [encoder endEncoding];
  auto original = CacheRead(f, first, image), first_copy = CacheRead(f, first, display); [first commit];
  auto repeat = [f.context->queue commandBuffer];
  encoder = CacheTestEncoder(repeat, display, MTLLoadActionDontCare);
  Check(cache.Draw(encoder, display, f.error), f.error); [encoder endEncoding];
  auto repeat_copy = CacheRead(f, repeat, display); [repeat commit];
  auto rewrite = [f.context->queue commandBuffer];
  encoder = CacheTestEncoder(rewrite, image, MTLLoadActionClear, MTLClearColorMake(0.75, 0.5, 0.25, 0.125));
  [encoder endEncoding];
  encoder = CacheTestEncoder(rewrite, display, MTLLoadActionDontCare);
  Check(cache.Draw(encoder, display, f.error), f.error); [encoder endEncoding];
  auto rewritten = CacheRead(f, rewrite, display); [rewrite commit];
  // None of the queued work can complete yet. Replace and release the cache's
  // own reference before allowing those commands to read their retained image.
  Check(cache.Prepare(23, 23, MTLPixelFormatBGRA8Unorm, f.error) != image, "Resize kept incompatible cache storage");
  image = nil; cache.Reset(); release.event.signaledValue = 1;
  [rewrite waitUntilCompleted];
  Check(first.status == MTLCommandBufferStatusCompleted && repeat.status == MTLCommandBufferStatusCompleted &&
        rewrite.status == MTLCommandBufferStatusCompleted, "Queued cache replacement failed");
  Check(original.Pixels() == first_copy.Pixels() && original.Pixels() == repeat_copy.Pixels(),
        "Cache rewrite raced an earlier in-flight read");
  Check(rewritten.Pixels() != original.Pixels(), "Later cache write was not visible in queue order");
  ++cases;
}
