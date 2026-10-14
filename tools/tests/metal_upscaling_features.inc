// Uses the common production flow planner and the real Metal AA/EASU/RCAS/EDR
// implementations. The inert subclass exposes planning, not a substitute renderer.
struct UpscalingFlowProbe final : rex::ui::Presenter {
  using Presenter::GuestOutputProperties; using Presenter::GuestOutputPaintFlow;
  using Presenter::GuestOutputPaintEffect; using Presenter::FsrEasuConstants;
  using Presenter::FsrRcasConstants; using Presenter::BilinearConstants;
  using Presenter::GetGuestOutputPaintFlow;
  UpscalingFlowProbe():Presenter({}) {InitializeCommonSurfaceIndependent();}
  rex::ui::Surface::TypeFlags GetSupportedSurfaceTypes() const override {return rex::ui::Surface::kTypeFlag_CAMetalLayer;}
  bool CaptureGuestOutput(rex::ui::RawImage&) override {return false;}
  SurfacePaintConnectResult ConnectOrReconnectPaintingToSurfaceFromUIThread(rex::ui::Surface&,uint32_t,uint32_t,bool,bool& sync) override {sync=true;return SurfacePaintConnectResult::kSuccess;}
  void DisconnectPaintingFromSurfaceFromUIThreadImpl() override {}
  bool RefreshGuestOutputImpl(uint32_t,uint32_t,uint32_t,std::function<bool(GuestOutputRefreshContext&)>,bool&) override {return false;}
  PaintResult PaintAndPresentImpl(bool) override {return PaintResult::kNotPresented;}
};

void TestUpscalingPolicy() {
  struct Expected {const char* quality;uint32_t width,height;};
  // Python fractions/rounding reference for a 3840x2160 output.
  const Expected presets[]={{"ultra_quality",2954,1662},{"quality",2560,1440},{"balanced",2259,1271},{"performance",1920,1080}};
  for(const auto& p:presets) {
    for(bool hdr:{false,true}) {
      auto metal=SelectFsr1RenderExtent(3840,2160,true,p.quality,false,hdr,true);
      Check(metal.active()&&metal.width==p.width&&metal.height==p.height,"FSR quality did not select expected render size");
      auto legacy=SelectFsr1RenderExtent(3840,2160,true,p.quality,false,hdr,false);
      Check(legacy.active()==!hdr,"Legacy pre-upscaling HDR safety changed");
      if(hdr)Check(legacy.width==3840&&legacy.height==2160,"Unsafe backend unexpectedly downscaled");
      auto native=SelectFsr1RenderExtent(3840,2160,false,p.quality,false,hdr,true);
      Check(!native.active()&&native.width==3840&&native.height==2160,"Quality affected Native resolution");
      auto ssaa=SelectFsr1RenderExtent(3840,2160,true,p.quality,true,hdr,true);
      Check(!ssaa.active()&&ssaa.selection==Fsr1Selection::kSupersampling,"FSR silently combined with SSAA");
    }
  }
  Check(!SelectFsr1RenderExtent(640,360,true,"performance",false,false,true).active(),"FSR minimum scene size ignored");
  Check(!SelectFsr1RenderExtent(0,0,true,"quality",false,false,true).active(),"Zero output size accepted");
  Check(!SelectFsr1RenderExtent(1920,1080,true,"unknown",false,false,true).active(),"Unknown FSR quality accepted");
  std::puts("fsr1_resolution_policy=passed presets=4 native_hdr_ssaa_floor=verified");
}

void TestUpscalingChain(Fixture& f) {
  PresentationEffects effects;Check(effects.Initialize(f.context,f.error),f.error);
  rex::ui::SDLWindowedAppContext app; Check(app.Initialize(),"FSR flow test context");
  UpscalingFlowProbe planner;
  auto window=rex::ui::Window::Create(app,"FSR flow validation",640,400);
  Check(window&&window->Open(),"FSR flow test surface");
  window->SetPresenter(&planner); planner.PaintFromUIThread(true);
  size_t chains=0;float sharpen_difference=0;
  auto image=[&](uint32_t width,uint32_t height,MTLPixelFormat format,MTLStorageMode storage) {
    auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
    d.storageMode=storage;d.usage=MTLTextureUsageShaderRead|(storage==MTLStorageModePrivate?MTLTextureUsageRenderTarget:MTLTextureUsageUnknown);
    auto value=[f.context->device newTextureWithDescriptor:d];Check(value!=nil,"FSR image allocation");return value;
  };
  const uint32_t output_width=160,output_height=100;
  for(const char* quality:{"ultra_quality","quality","balanced","performance"}) {
    const auto scale=*Fsr1QualityScale(quality);
    const uint32_t width=(output_width*scale.denominator+scale.numerator/2)/scale.numerator;
    const uint32_t height=(output_height*scale.denominator+scale.numerator/2)/scale.numerator;
    for(bool pattern:{false,true}) {
      std::vector<Pixel> pixels(size_t(width)*height);
      for(uint32_t y=0;y<height;++y) for(uint32_t x=0;x<width;++x) {
        const float v=x>y/2+width/3?0.85f:0.15f;
        pixels[size_t(y)*width+x]=pattern?Pixel{v,float(x)/float(width-1),float(y)/float(height-1),1}:Pixel{-0.5f,0.5f,2.0f,1};
      }
      auto input=image(width,height,MTLPixelFormatRGBA32Float,MTLStorageModeShared);
      [input replaceRegion:MTLRegionMake2D(0,0,width,height) mipmapLevel:0 withBytes:pixels.data() bytesPerRow:width*sizeof(Pixel)];
      for(auto aa:{AntiAliasingMode::kOff,AntiAliasingMode::kFxaa,AntiAliasingMode::kSmaa,AntiAliasingMode::kMsaa4xSmaa}) {
        std::vector<Pixel> sharp;
        for(float reduction:{0.0f,2.0f}) for(uint32_t hdr:{0u,1u,2u}) {
          auto normalized=image(width,height,MTLPixelFormatRGBA16Float,MTLStorageModePrivate);
          auto expanded=image(output_width,output_height,MTLPixelFormatRGBA16Float,MTLStorageModePrivate);
          auto sharpened=image(output_width,output_height,MTLPixelFormatRGBA16Float,MTLStorageModePrivate);
          auto final=image(output_width,output_height,MTLPixelFormatRGBA16Float,MTLStorageModePrivate);
          auto commands=[f.context->queue commandBuffer];
          Check(f.post.Record(commands,input,normalized,aa,"high",false,f.error),f.error);
          auto make_encoder=[&](id<MTLTexture> target) {
            auto pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=target;
            pass.colorAttachments[0].loadAction=MTLLoadActionClear;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
            auto encoder=[commands renderCommandEncoderWithDescriptor:pass];Check(encoder!=nil,"FSR chain encoder");return encoder;
          };
          UpscalingFlowProbe::GuestOutputProperties p;p.frontbuffer_width=width;p.frontbuffer_height=height;
          p.display_aspect_ratio_x=output_width;p.display_aspect_ratio_y=output_height;p.is_8bpc=false;
          UpscalingFlowProbe::GuestOutputPaintConfig config;config.SetEffect(UpscalingFlowProbe::GuestOutputPaintConfig::Effect::kFsr);
          config.SetFsrSharpnessReduction(reduction);config.SetDither(false);
          const auto flow=planner.GetGuestOutputPaintFlow(p,output_width,output_height,16384,16384,config);
          if(flow.effect_count!=2)std::fprintf(stderr,"FSR flow quality=%s reduction=%g input=%ux%u output=%ux%u count=%zu effects=%u,%u\n",
              quality,reduction,width,height,output_width,output_height,size_t(flow.effect_count),unsigned(flow.effects[0]),unsigned(flow.effects[1]));
          Check(flow.effect_count==2&&flow.effects[0]==UpscalingFlowProbe::GuestOutputPaintEffect::kFsrEasu&&
              flow.effects[1]==UpscalingFlowProbe::GuestOutputPaintEffect::kFsrRcas,"FSR chain is not EASU followed by RCAS");
          id<MTLTexture> source=normalized;
          for(size_t stage=0;stage<flow.effect_count;++stage) {
            auto target=stage?sharpened:expanded;auto encoder=make_encoder(target);
            int32_t x,y;flow.GetEffectOutputOffset(stage,x,y);const auto size=flow.effect_output_sizes[stage];
            const MTLViewport viewport{double(x),double(y),double(size.first),double(size.second),0,1};
            if(stage==0) {UpscalingFlowProbe::FsrEasuConstants c{};c.Initialize(flow,stage);
              Check(effects.Draw(encoder,PresentationEffect::kFsrEasu,source,target,viewport,std::as_bytes(std::span(&c,1)),f.error),f.error);
            } else {UpscalingFlowProbe::FsrRcasConstants c{};c.Initialize(flow,stage,config);
              Check(effects.Draw(encoder,PresentationEffect::kFsrRcas,source,target,viewport,std::as_bytes(std::span(&c,1)),f.error),f.error);}
            [encoder endEncoding];source=target;
          }
          auto encoder=make_encoder(final);OutputTransferConstants c{};c.hdr_mode=hdr;c.hdr_headroom=4;
          Check(f.transfer.Draw(encoder,source,final,c,f.error),f.error);[encoder endEncoding];
          const auto output=f.Read(commands,final);++chains;
          for(const auto& pixel:output)for(size_t channel=0;channel<3;++channel)
            Check(pixel[channel]>=0&&pixel[channel]<=(hdr?4.0f:1.0f),"FSR/HDR output out of range");
          if(!pattern) {
            const double linear=std::pow((0.5+0.055)/1.055,2.4);
            double green=hdr?linear*203.0/80.0:0.5;
            // Auto HDR is hue preserving and depends on full-color luminance;
            // its exact transfer is covered by TestHdr. Here check normalization.
            for(const auto& pixel:output) {
              Check(std::abs(pixel[0])<0.005f,"Negative FSR input escaped normalization");
              if(hdr!=2)Check(std::abs(pixel[1]-green)<0.007,"FSR altered a uniform perceptual color");
              Check(pixel[2]>0.99f,"FSR lost the clamped bright input");
            }
          } else if(hdr==0) {
            if(reduction==0)sharp=output;
            else for(size_t i=0;i<output.size();++i)sharpen_difference=std::max(sharpen_difference,std::abs(output[i][0]-sharp[i][0]));
            Check(output.front()[1]<output.back()[1]&&output.front()[2]<output.back()[2],"Upscaled image orientation or extent is wrong");
          }
        }
      }
    }
  }
  window->SetPresenter(nullptr);window->RequestClose();
  Check(sharpen_difference>0.001f,"RCAS sharpness setting has no pixel effect");
  std::printf("metal_upscaling_chain=passed cases=%zu qualities=4 aa_modes=4 hdr_modes=3 rcas_difference=%g\n",chains,sharpen_difference);
}
