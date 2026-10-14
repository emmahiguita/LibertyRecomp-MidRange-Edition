// Differential test: the prior production bilinear -> FP16 -> HDR path is the
// reference. The fused pass must preserve filtering, intermediate rounding,
// alpha, letterboxing, overscan and the authored HDR curve.
void TestFusedBilinearHdr(Fixture& f) {
  PresentationEffects effects;Check(effects.Initialize(f.context,f.error),f.error);
  float maximum_difference=0;size_t compared=0,paths=0;
  struct Shape {uint32_t sw,sh,tw,th;};
  for(const auto shape:{Shape{17,11,29,23},Shape{53,37,23,17},Shape{64,32,64,32}}){
    auto allocate=[&](MTLPixelFormat format,uint32_t w,uint32_t h,MTLStorageMode storage){
      auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:w height:h mipmapped:NO];
      d.storageMode=storage;d.usage=MTLTextureUsageShaderRead|(storage==MTLStorageModePrivate?MTLTextureUsageRenderTarget:MTLTextureUsageUnknown);
      auto t=[f.context->device newTextureWithDescriptor:d];Check(t!=nil,"fused HDR fixture allocation");return t;
    };
    auto source=allocate(MTLPixelFormatRGBA16Float,shape.sw,shape.sh,MTLStorageModeShared);
    auto intermediate=allocate(MTLPixelFormatRGBA16Float,shape.tw,shape.th,MTLStorageModePrivate);
    auto target=allocate(MTLPixelFormatRGBA16Float,shape.tw,shape.th,MTLStorageModePrivate);
    for(bool pattern:{false,true}){
      std::vector<std::array<_Float16,4>> data(size_t(shape.sw)*shape.sh);
      for(uint32_t y=0;y<shape.sh;++y)for(uint32_t x=0;x<shape.sw;++x){
        const float r=float(x)/float(shape.sw-1),g=float(y)/float(shape.sh-1);
        data[size_t(y)*shape.sw+x]={_Float16(pattern?r:0.25f),_Float16(pattern?g:0.5f),_Float16(pattern?((x+y)%2):0.75f),_Float16(pattern?float((x*7+y*3)%17)/16:1)};
      }
      [source replaceRegion:MTLRegionMake2D(0,0,shape.sw,shape.sh) mipmapLevel:0 withBytes:data.data() bytesPerRow:shape.sw*sizeof(data[0])];
      const std::array<MTLViewport,4> views{{
        {0,0,double(shape.tw),double(shape.th),0,1},
        {3,2,double(shape.tw-6),double(shape.th-4),0,1},
        {-4,-3,double(shape.tw+8),double(shape.th+6),0,1},
        {0,4,double(shape.tw),double(shape.th-8),0,1}}};
      for(const auto viewport:views)for(uint32_t mode:{1u,2u})for(float headroom:{1.0f,2.0f,4.0f}){
        OutputTransferConstants hdr{};hdr.hdr_mode=mode;hdr.hdr_headroom=headroom;
        hdr.shoulder_start=pattern?0.25f:0;hdr.shoulder_power=pattern?3.5f:2.5f;
        std::array<std::vector<Pixel>,2> results;
        for(size_t path=0;path<2;++path){
          auto commands=[f.context->queue commandBuffer];
          auto encoder_for=[&](id<MTLTexture> t){auto p=[MTLRenderPassDescriptor renderPassDescriptor];p.colorAttachments[0].texture=t;
            p.colorAttachments[0].loadAction=MTLLoadActionClear;p.colorAttachments[0].storeAction=MTLStoreActionStore;
            p.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,1);
            auto e=[commands renderCommandEncoderWithDescriptor:p];Check(e!=nil,"fused HDR test encoder");return e;};
          if(path==0){
            PresentationConstants::GuestOutputPaintFlow flow{};flow.effect_count=1;
            flow.properties.frontbuffer_width=shape.sw;flow.properties.frontbuffer_height=shape.sh;
            flow.effect_output_sizes[0]={uint32_t(viewport.width),uint32_t(viewport.height)};
            flow.output_x=int32_t(viewport.originX);flow.output_y=int32_t(viewport.originY);
            PresentationConstants::BilinearConstants constants{};constants.Initialize(flow,0);
            auto e=encoder_for(intermediate);
            Check(effects.Draw(e,PresentationEffect::kBilinear,source,intermediate,viewport,std::as_bytes(std::span(&constants,1)),f.error),f.error);
            [e endEncoding];e=encoder_for(target);Check(f.transfer.Draw(e,intermediate,target,hdr,f.error),f.error);[e endEncoding];
          }else{
            auto e=encoder_for(target);Check(f.transfer.DrawBilinearHdr(e,source,target,viewport,hdr,f.error),f.error);[e endEncoding];
          }
          results[path]=f.Read(commands,target);
        }
        Check(results[0].size()==results[1].size(),"fused HDR image extent changed");
        for(size_t i=0;i<results[0].size();++i)for(size_t c=0;c<4;++c){
          const float difference=std::abs(results[0][i][c]-results[1][i][c]);maximum_difference=std::max(maximum_difference,difference);
          if(difference>0.004f){std::fprintf(stderr,"fused HDR difference src=%ux%u dst=%ux%u viewport=%.0f,%.0f,%.0f,%.0f mode=%u pattern=%u pixel=%zu channel=%zu old=%g new=%g\n",shape.sw,shape.sh,shape.tw,shape.th,viewport.originX,viewport.originY,viewport.width,viewport.height,mode,unsigned(pattern),i,c,results[0][i][c],results[1][i][c]);}
          Check(difference<=0.004f,"fused bilinear/HDR changed output beyond the differential tolerance");++compared;
        }
        ++paths;
      }
    }
    auto commands=[f.context->queue commandBuffer];auto p=[MTLRenderPassDescriptor renderPassDescriptor];p.colorAttachments[0].texture=target;
    p.colorAttachments[0].loadAction=MTLLoadActionDontCare;p.colorAttachments[0].storeAction=MTLStoreActionStore;
    auto encoder=[commands renderCommandEncoderWithDescriptor:p];OutputTransferConstants invalid{};
    const MTLViewport v{0,0,double(shape.tw),double(shape.th),0,1};
    Check(!f.transfer.DrawBilinearHdr(encoder,source,target,v,invalid,f.error),"SDR accepted on HDR-only route");
    invalid.hdr_mode=2;invalid.output_mode|=16;
    Check(!f.transfer.DrawBilinearHdr(encoder,source,target,v,invalid,f.error),"FXAA silently omitted from fused route");
    invalid.output_mode=4;auto fractional=v;fractional.originX=0.5;
    Check(!f.transfer.DrawBilinearHdr(encoder,source,target,fractional,invalid,f.error),"fractional viewport accepted by integer flow route");
    [encoder endEncoding];[commands commit];[commands waitUntilCompleted];
    Check(commands.status==MTLCommandBufferStatusCompleted,"fused rejection test command failed");
  }
  std::printf("fused_bilinear_hdr=passed configurations=%zu compared_channels=%zu maximum_difference=%g\n",paths,compared,maximum_difference);
}

// Explicit opt-in operation benchmark; this is not a game FPS measurement.
// Alternate both paths on identical pixels, never include CPU readback in timing.
void BenchmarkFusedBilinearHdr(Fixture& f) {
  PresentationEffects effects;Check(effects.Initialize(f.context,f.error),f.error);
  struct Shape {uint32_t sw,sh,tw,th;};
  std::puts("fused_hdr_benchmark,path,sw,sh,tw,th,trial,gpu_seconds,encoders,intermediate_bytes");
  for(const auto shape:{Shape{1280,720,1920,1080},Shape{1670,1080,3456,2234}}){
    const auto allocate=[&](uint32_t w,uint32_t h,MTLStorageMode storage){
      auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:w height:h mipmapped:NO];
      d.storageMode=storage;d.usage=MTLTextureUsageShaderRead|(storage==MTLStorageModePrivate?MTLTextureUsageRenderTarget:MTLTextureUsageUnknown);
      auto image=[f.context->device newTextureWithDescriptor:d];Check(image!=nil,"HDR benchmark texture allocation");return image;
    };
    auto source=allocate(shape.sw,shape.sh,MTLStorageModeShared),scratch=allocate(shape.tw,shape.th,MTLStorageModePrivate),output=allocate(shape.tw,shape.th,MTLStorageModePrivate);
    std::vector<std::array<_Float16,4>> pixels(size_t(shape.sw)*shape.sh);
    for(size_t y=0;y<shape.sh;++y)for(size_t x=0;x<shape.sw;++x)
      pixels[y*shape.sw+x]={_Float16(float(x)/shape.sw),_Float16(float(y)/shape.sh),_Float16(float((x/4+y/4)%2)),_Float16(1)};
    [source replaceRegion:MTLRegionMake2D(0,0,shape.sw,shape.sh) mipmapLevel:0 withBytes:pixels.data() bytesPerRow:shape.sw*sizeof(pixels[0])];
    const MTLViewport viewport{0,0,double(shape.tw),double(shape.th),0,1};
    PresentationConstants::GuestOutputPaintFlow flow{};flow.effect_count=1;flow.properties.frontbuffer_width=shape.sw;flow.properties.frontbuffer_height=shape.sh;
    flow.effect_output_sizes[0]={shape.tw,shape.th};PresentationConstants::BilinearConstants bilinear{};bilinear.Initialize(flow,0);
    OutputTransferConstants hdr{};hdr.hdr_mode=2;hdr.hdr_headroom=2;hdr.paper_white_nits=203;hdr.peak_nits=400;
    for(int trial=-5;trial<30;++trial)for(int order=0;order<2;++order){
      const bool fused=((trial+5+order)%2)!=0;
      auto commands=[f.context->queue commandBuffer];
      const auto encoder_for=[&](id<MTLTexture> target){auto pass=[MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture=target;pass.colorAttachments[0].loadAction=MTLLoadActionClear;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
        pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,1);auto e=[commands renderCommandEncoderWithDescriptor:pass];Check(e!=nil,"benchmark encoder");return e;};
      if(fused){auto e=encoder_for(output);Check(f.transfer.DrawBilinearHdr(e,source,output,viewport,hdr,f.error),f.error);[e endEncoding];}
      else {auto e=encoder_for(scratch);Check(effects.Draw(e,PresentationEffect::kBilinear,source,scratch,viewport,std::as_bytes(std::span(&bilinear,1)),f.error),f.error);
        [e endEncoding];e=encoder_for(output);Check(f.transfer.Draw(e,scratch,output,hdr,f.error),f.error);[e endEncoding];}
      [commands commit];[commands waitUntilCompleted];Check(commands.status==MTLCommandBufferStatusCompleted,MetalError(commands.error,"benchmark GPU execution"));
      const double seconds=commands.GPUEndTime-commands.GPUStartTime;Check(seconds>0&&std::isfinite(seconds),"GPU timestamps unavailable");
      if(trial>=0)std::printf("fused_hdr_benchmark,%s,%u,%u,%u,%u,%d,%.12f,%u,%llu\n",fused?"fused":"reference",shape.sw,shape.sh,shape.tw,shape.th,trial,seconds,fused?1:2,(unsigned long long)(fused?0:scratch.allocatedSize));
    }
  }
}
