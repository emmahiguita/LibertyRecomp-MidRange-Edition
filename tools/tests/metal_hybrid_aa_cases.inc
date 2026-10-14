// Title drawing -> original resolves -> production Present -> SMAA -> mailbox.
// Only the absent OS window is replaced by a captured Metal mailbox.
class HybridMailbox final : public ui::Presenter {
 public:
  explicit HybridMailbox(std::shared_ptr<ui::metal::MetalContext> context)
      : Presenter([](bool, bool) { throw std::runtime_error("Hybrid mailbox GPU loss"); }),
        context_(std::move(context)) { Check(InitializeCommonSurfaceIndependent(), "Mailbox setup"); }
  ui::Surface::TypeFlags GetSupportedSurfaceTypes() const override { return 0; }
  bool CaptureGuestOutput(ui::RawImage&) override { return false; }
  id<MTLTexture> last = nil;
 private:
  SurfacePaintConnectResult ConnectOrReconnectPaintingToSurfaceFromUIThread(
      ui::Surface&, uint32_t, uint32_t, bool, bool&) override { return SurfacePaintConnectResult::kFailure; }
  void DisconnectPaintingFromSurfaceFromUIThreadImpl() override {}
  PaintResult PaintAndPresentImpl(bool) override { return PaintResult::kNotPresented; }
  bool RefreshGuestOutputImpl(uint32_t index, uint32_t width, uint32_t height,
      std::function<bool(GuestOutputRefreshContext&)> writer, bool& is_8bpc) override {
    Check(index < images_.size(), "Hybrid mailbox index");
    auto& image = images_[index];
    if (!image || image.width != width || image.height != height) {
      auto d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
          width:width height:height mipmapped:NO];
      d.storageMode = MTLStorageModePrivate;
      d.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
      image = [context_->device newTextureWithDescriptor:d];
    }
    Check(image != nil && image.sampleCount == 1, "Mailbox must be single-sampled");
    ui::metal::MetalGuestOutputRefreshContext output(is_8bpc, image, ++version_);
    if (!writer(output)) return false;
    last = image; return true;
  }
  std::shared_ptr<ui::metal::MetalContext> context_;
  std::array<id<MTLTexture>, 3> images_{};
  uint64_t version_ = 0;
};

static std::vector<std::array<float,4>> ReadHybridImage(
    const std::shared_ptr<ui::metal::MetalContext>& context, id<MTLTexture> image) {
  Check(image && image.pixelFormat == MTLPixelFormatRGBA16Float && image.width == 16 && image.height == 16,
        "Hybrid output changed extent or format");
  constexpr size_t pitch = 256;
  auto buffer = [context->device newBufferWithLength:pitch * 16 options:MTLResourceStorageModeShared];
  auto commands = [context->queue commandBuffer]; auto copy = [commands blitCommandEncoder];
  Check(buffer && commands && copy, "Hybrid readback allocation");
  [copy copyFromTexture:image sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
       sourceSize:MTLSizeMake(16,16,1) toBuffer:buffer destinationOffset:0
       destinationBytesPerRow:pitch destinationBytesPerImage:pitch * 16];
  [copy endEncoding]; [commands commit]; [commands waitUntilCompleted];
  Check(commands.status == MTLCommandBufferStatusCompleted,
        ui::metal::MetalError(commands.error, "Hybrid output readback failed"));
  std::vector<std::array<float,4>> values(16*16);
  for (size_t y=0;y<16;++y) for (size_t x=0;x<16;++x) for (size_t c=0;c<4;++c) {
    _Float16 value;
    std::memcpy(&value, static_cast<const uint8_t*>(buffer.contents)+y*pitch+x*8+c*2, sizeof(value));
    values[y*16+x][c]=float(value); Check(std::isfinite(float(value)), "Nonfinite hybrid output");
  }
  return values;
}

void ExerciseHybridAA(memory::Memory& memory, const std::shared_ptr<ui::metal::MetalContext>& context,
                      const std::filesystem::path& shaders) {
  using namespace rex::graphics::gta4_native::core;
  Check([context->device supportsTextureSampleCount:4], "4x MSAA capability unavailable");
  size_t cases=0, changed_pixels=0, quarter_coverage=0;
  for (uint32_t guest_samples : {1u,2u}) for (bool tiled : {false,true}) {
    Check(cvar::SetFlagByName("gta4_native_anti_aliasing","msaa4x_smaa"),"Select hybrid");
    HybridMailbox mailbox(context); Fixture f(memory,context,shaders,&mailbox);
    ConfigureGeometryFixture(f);
    const auto surface=f.Surface(false,guest_samples), depth=f.Surface(true,guest_samples);
    f.Bind(surface,depth);
    f.Word(f.arena+kDepthEnableOffset,1);
    f.Word(f.arena+kDepthStencilPackedOffset,(7u<<4)|4u);
    const auto resolved=f.MakeTexture(false,tiled,xenos::Endian::k8in32);
    const auto resolved_depth=f.MakeTexture(true,tiled,xenos::Endian::k8in32);
    PostProcessing reference; Check(reference.Initialize(context,f.error),f.error);
    auto sd=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:16 height:16 mipmapped:NO];
    sd.storageMode=MTLStorageModeShared; sd.usage=MTLTextureUsageShaderRead;
    auto reference_input=[context->device newTextureWithDescriptor:sd];
    auto td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:16 height:16 mipmapped:NO];
    td.storageMode=MTLStorageModePrivate;td.usage=MTLTextureUsageShaderRead|MTLTextureUsageRenderTarget;
    auto reference_output=[context->device newTextureWithDescriptor:td];
    Check(reference_input && reference_output,"Hybrid reference allocation");
    uint32_t frame=0;
    for (bool edges : {false,true}) {
      std::vector<uint32_t> baseline;
      for (const auto mode : {"msaa4x_smaa","msaa4x","msaa4x_smaa"}) {
        Check(cvar::SetFlagByName("gta4_native_anti_aliasing",mode),"Switch hybrid SMAA live");
        f.Clear(0x31,{0,0,0,1},1,0x53);
        const std::array<GeometryVertex,4> quad{GeometryCorner(-1,1),GeometryCorner(1,1),GeometryCorner(1,-1),GeometryCorner(-1,-1)};
        std::array<GeometryVertex,3> triangle{GeometryCorner(-0.92f,0.96f),GeometryCorner(0.84f,0.70f),GeometryCorner(-0.70f,-0.86f)};
        for (auto& vertex : triangle) vertex.uv = {1,1};
        if(edges) SubmitGeometry(f,triangle,uint32_t(xenos::PrimitiveType::kTriangleList),{1,1,1,1});
        else SubmitGeometry(f,quad,uint32_t(xenos::PrimitiveType::kQuadList),{0,0,0.6f,1});
        f.Submit(f.ResolveRequest(surface,resolved,false));
        f.Submit(f.ResolveRequest(depth,resolved_depth,true));
        const auto pixels=f.Read(resolved);
        const auto depth_pixels=f.Read(resolved_depth);
        for (auto value : depth_pixels) Check((value&255)==0x53,"Hybrid changed stencil contents");
        if (baseline.empty()) baseline=pixels;
        else Check(baseline==pixels,"Adding SMAA changed pre-output MSAA scene pixels");
        for (auto pixel : pixels) {
          const uint32_t blue=(pixel>>16)&255;
          if(edges && (blue==64 || blue==191)) ++quarter_coverage;
        }
        PresentCommand present{};present.device=f.arena;present.frontbuffer_texture=resolved.handle;
        std::memcpy(present.frontbuffer_fetch,&resolved.fetch,sizeof(resolved.fetch));
        present.width=present.height=present.display_width=present.display_height=16;
        present.submitted_frame=++frame;
        f.Submit(present);Check(f.renderer.Finish(f.error),f.error);
        const auto actual=ReadHybridImage(context,mailbox.last);
        [reference_input replaceRegion:MTLRegionMake2D(0,0,16,16) mipmapLevel:0 withBytes:pixels.data() bytesPerRow:16*sizeof(uint32_t)];
        auto cb=[context->queue commandBuffer];
        const bool smaa=std::string_view(mode)=="msaa4x_smaa";
        // Independently apply SMAA to the resolved image, bypassing the hybrid selector.
        Check(reference.Record(cb,reference_input,reference_output,smaa?AntiAliasingMode::kSmaa:AntiAliasingMode::kOff,
                               "high",false,f.error),f.error);
        [cb commit];[cb waitUntilCompleted];
        Check(cb.status==MTLCommandBufferStatusCompleted,"Hybrid reference GPU error");
        const auto expected=ReadHybridImage(context,reference_output);
        for(size_t i=0;i<actual.size();++i) {
          for(size_t c=0;c<4;++c) Check(std::abs(actual[i][c]-expected[i][c])<0.002f,
                                       "Production hybrid output differs from resolved-image SMAA");
          if(edges && smaa && std::abs(actual[i][2]-float((pixels[i]>>16)&255)/255.0f)>0.004f) ++changed_pixels;
        }
        ++cases;
      }
    }
    // Exercise the established depth/stencil handoffs with the hybrid mode active.
    ExerciseInitialization(f,guest_samples,tiled);
    Exercise(f,guest_samples,tiled);
  }
  Check(quarter_coverage>0,"Test did not observe true four-sample geometric coverage");
  Check(changed_pixels>0,"SMAA did not process the already-resolved scene");
  Check(cvar::SetFlagByName("gta4_native_anti_aliasing","off"),"Restore AA fixture setting");
  std::printf("metal_hybrid_aa=passed cases=%zu quarter_coverage=%zu smaa_changed_pixels=%zu live_toggle=true depth_stencil=true\n",
              cases,quarter_coverage,changed_pixels);
}
