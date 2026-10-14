// Menu CVar -> frame snapshot -> production sampler/texture upload -> GPU pixels.
// A separately constructed Metal sampler and host-uploaded mip chain are the
// reference; neither expected settings nor expected images call our policies.
#include <rex/cvar.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/pipeline/texture/conversion.h>

static void ExerciseSamplerFeatures(const std::shared_ptr<rex::ui::metal::MetalContext>& context,
                                    rex::memory::Memory& memory) {
  using namespace rex; using namespace rex::graphics; using namespace gta4_metal;
  const auto saved_filter=cvar::GetFlagByName("gta4_texture_filtering");
  const auto saved_aniso=cvar::GetFlagByName("gta4_anisotropic_filtering");
  constexpr uint32_t size=256, levels=9, handle=0x71400000;
  auto* heap=memory.GetPhysicalHeap();uint32_t address=0;
  Check(heap->Alloc(4u*1024u*1024u,4096,memory::kMemoryAllocationReserve|memory::kMemoryAllocationCommit,
      memory::kMemoryProtectRead|memory::kMemoryProtectWrite,false,&address),"Sampler fixture allocation");
  GuestMemory guest(&memory);ResourceStore resources(context,guest);
  xenos::xe_gpu_texture_fetch_t fetch{};
  fetch.type=xenos::FetchConstantType::kTexture;fetch.format=xenos::TextureFormat::k_8_8_8_8;
  fetch.dimension=xenos::DataDimension::k2DOrStacked;fetch.base_address=address>>12;
  fetch.mip_address=(address+size*size*4)>>12;fetch.pitch=size/32;
  fetch.size_2d.width=fetch.size_2d.height=size-1;fetch.mip_max_level=levels-1;
  fetch.swizzle=xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;fetch.endianness=xenos::Endian::k8in32;
  fetch.min_filter=fetch.mag_filter=fetch.mip_filter=xenos::TextureFilter::kPoint;
  fetch.clamp_x=fetch.clamp_y=xenos::ClampMode::kClampToEdge;
  TextureInfo info{};Check(TextureInfo::Prepare(fetch,&info),"Sampler fixture fetch");
  auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:size height:size mipmapped:YES];
  descriptor.storageMode=MTLStorageModeShared;descriptor.usage=MTLTextureUsageShaderRead;
  auto reference=[context->device newTextureWithDescriptor:descriptor];Check(reference!=nil,"Reference image allocation");
  for(uint32_t level=0;level<levels;++level) {
    const uint32_t width=std::max(1u,size>>level);
    std::vector<std::array<uint8_t,4>> pixels(size_t(width)*width);
    for(uint32_t y=0;y<width;++y) for(uint32_t x=0;x<width;++x) {
      const uint8_t stripe=(x&1)?255:0;
      pixels[size_t(y)*width+x]={stripe,uint8_t(level*27),uint8_t((y*17+level*13)&255),255};
    }
    [reference replaceRegion:MTLRegionMake2D(0,0,width,width) mipmapLevel:level withBytes:pixels.data() bytesPerRow:width*4];
    uint32_t px=0,py=0;const uint32_t mip_address=info.GetMipLocation(level,&px,&py,true);
    const auto extent=info.GetMipExtent(level,true);
    for(uint32_t y=0;y<width;++y) for(uint32_t x=0;x<width;++x) {
      auto bytes=guest.Writable(mip_address+((y+py)*extent.block_pitch_h+x+px)*4,4,true);
      Check(bytes.size()==4,"Guest mip exceeds allocated span");
      for(size_t c=0;c<4;++c) bytes[c]=pixels[size_t(y)*width+x][c^3u];
    }
  }
  NSString* shader=@R"MSL(#include <metal_stdlib>
using namespace metal;
kernel void samples(texture2d<float> tex [[texture(0)]],sampler filter [[sampler(0)]],
                    device float4* values [[buffer(0)]],uint i [[thread_position_in_grid]]) {
  float2 uv=float2((float(i%16)+0.37f)/16.0f,(float(i/16)+0.61f)/16.0f);
  values[i]=tex.sample(filter,uv,level(0.35f));
  values[256+i]=tex.sample(filter,uv,gradient2d(float2(16.0f/256.0f,0),float2(0,1.0f/256.0f)));
})MSL";
  NSError* native_error=nil;auto library=[context->device newLibraryWithSource:shader options:nil error:&native_error];
  Check(library!=nil,ui::metal::MetalError(native_error,"Sampler test shader"));
  auto pipeline=[context->device newComputePipelineStateWithFunction:[library newFunctionWithName:@"samples"] error:&native_error];
  Check(pipeline!=nil,ui::metal::MetalError(native_error,"Sampler test pipeline"));
  std::string error;size_t cases=0,pixel_checks=0;float mip_difference=0,anisotropy_difference=0;
  std::vector<std::array<float,4>> bilinear_one,trilinear_one;
  for(const char* filtering:{"bilinear","trilinear"}) for(uint32_t anisotropy:{1u,2u,4u,8u,16u}) {
    Check(cvar::SetFlagByName("gta4_texture_filtering",filtering),"Filter setting rejected");
    Check(cvar::SetFlagByName("gta4_anisotropic_filtering",std::to_string(anisotropy)+"x"),"Anisotropy setting rejected");
    resources.BeginFrame();auto commands=[context->queue commandBuffer];
    auto texture=resources.Texture(handle,fetch,commands,[]{},error);Check(bool(texture),error);
    Check(texture->image.mipmapLevelCount==levels,"Guest mip chain was truncated");
    auto selected=resources.Sampler(fetch,error,texture.get());Check(selected!=nil,error);
    Check(resources.Sampler(fetch,error,texture.get())==selected,"Same sampler was not reused");
    auto expected=[MTLSamplerDescriptor new];
    expected.minFilter=expected.magFilter=MTLSamplerMinMagFilterLinear;
    expected.mipFilter=anisotropy>1 || std::string_view(filtering)=="trilinear"?MTLSamplerMipFilterLinear:MTLSamplerMipFilterNearest;
    expected.maxAnisotropy=anisotropy;expected.lodMinClamp=0;expected.lodMaxClamp=levels-1;
    expected.sAddressMode=expected.tAddressMode=MTLSamplerAddressModeClampToEdge;
    auto reference_sampler=[context->device newSamplerStateWithDescriptor:expected];Check(reference_sampler!=nil,"Reference sampler");
    std::array<id<MTLBuffer>,2> buffers{};
    for(size_t path=0;path<2;++path) {
      buffers[path]=[context->device newBufferWithLength:512*sizeof(float)*4 options:MTLResourceStorageModeShared];
      auto encoder=[commands computeCommandEncoder];Check(encoder!=nil,"Sampler encoder");
      [encoder setComputePipelineState:pipeline];
      [encoder setTexture:path?reference:texture->image atIndex:0];
      [encoder setSamplerState:path?reference_sampler:selected atIndex:0];
      [encoder setBuffer:buffers[path] offset:0 atIndex:0];
      [encoder dispatchThreads:MTLSizeMake(256,1,1) threadsPerThreadgroup:MTLSizeMake(32,1,1)];[encoder endEncoding];
    }
    [commands commit];[commands waitUntilCompleted];
    Check(commands.status==MTLCommandBufferStatusCompleted,ui::metal::MetalError(commands.error,"Sampler GPU failure"));
    const auto* actual=static_cast<const std::array<float,4>*>(buffers[0].contents);
    const auto* wanted=static_cast<const std::array<float,4>*>(buffers[1].contents);
    for(size_t i=0;i<512;++i) for(size_t c=0;c<4;++c) {
      if(!std::isfinite(actual[i][c]) || std::abs(actual[i][c]-wanted[i][c])>0.002f) {
        std::fprintf(stderr,"Sampler mismatch filter=%s anisotropy=%u pixel=%zu channel=%zu actual=%.6f expected=%.6f\n",filtering,anisotropy,i,c,actual[i][c],wanted[i][c]);
        Check(false,"Production texture/sampler differs from independent reference");
      }
      ++pixel_checks;
    }
    if(anisotropy==1 && std::string_view(filtering)=="bilinear")bilinear_one.assign(actual,actual+512);
    if(anisotropy==1 && std::string_view(filtering)=="trilinear") {
      trilinear_one.assign(actual,actual+512);
      for(size_t i=0;i<256;++i)mip_difference=std::max(mip_difference,std::abs(actual[i][1]-bilinear_one[i][1]));
    }
    if(anisotropy==16 && std::string_view(filtering)=="trilinear")
      for(size_t i=256;i<512;++i)anisotropy_difference=std::max(anisotropy_difference,std::abs(actual[i][1]-trilinear_one[i][1]));
    ++cases;
  }
  Check(mip_difference>0.01f,"Bilinear/trilinear selection did not change fractional mip sampling");
  Check(anisotropy_difference>0.01f,"Anisotropy did not change the grazing-footprint samples");
  Check(cvar::SetFlagByName("gta4_texture_filtering",saved_filter),"Restore filtering");
  Check(cvar::SetFlagByName("gta4_anisotropic_filtering",saved_aniso),"Restore anisotropy");
  resources.Clear();heap->Release(address);
  std::printf("metal_sampler_features=passed cases=%zu pixel_checks=%zu mip_difference=%g anisotropy_difference=%g\n",cases,pixel_checks,mip_difference,anisotropy_difference);
}
