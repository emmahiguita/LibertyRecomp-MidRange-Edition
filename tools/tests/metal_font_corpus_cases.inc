#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/pipeline/texture/conversion.h>
#include <filesystem>
#include <fstream>

// Optional, user-owned DDS corpus exported from each installed episode. Expected
// episode identities come from the independently exported BC3 payload, not from
// the resource store's selected atlas or a fabricated shader resource.
static void ExerciseFontCorpus(const std::shared_ptr<rex::ui::metal::MetalContext>& context,
                               rex::memory::Memory& memory, const std::filesystem::path& root) {
  using namespace rex; using namespace rex::graphics; using namespace gta4_metal;
  namespace font = gta4_native::core;
  struct Case { const char* path; uint32_t id; font::VectorFontSet set; uint64_t identity; };
  const Case cases[] = {
      {"gta4/fonts/font1.dds",1,font::VectorFontSet::kGta4,0xDFE8FA6D9EFA3B74ull},
      {"gta4/fonts/font3.dds",3,font::VectorFontSet::kGta4,0x7014107751000DA2ull},
      {"gta4/fonts_streamed_1/font2.dds",2,font::VectorFontSet::kGta4,0xC437FD9D511E6140ull},
      {"tbogt/fonts/font1.dds",1,font::VectorFontSet::kGta4,0xDFE8FA6D9EFA3B74ull},
      {"tbogt/fonts/font3.dds",3,font::VectorFontSet::kTbogt,0x6C569517F27F53D2ull},
      {"tbogt/fonts_streamed_1/font2.dds",2,font::VectorFontSet::kTbogt,0xD319ABCEFD23508Dull},
      {"tlad/fonts/font1.dds",1,font::VectorFontSet::kGta4,0x50BA3B10E69EC82Full},
      {"tlad/fonts/font3.dds",3,font::VectorFontSet::kTlad,0x57551F2730323DF3ull},
  };
  auto* heap = memory.GetPhysicalHeap(); uint32_t payload = 0;
  Check(heap->Alloc(font::kFontIdentityBytes,4096,memory::kMemoryAllocationReserve|memory::kMemoryAllocationCommit,
                    memory::kMemoryProtectRead|memory::kMemoryProtectWrite,false,&payload),"Font corpus allocation failed");
  GuestMemory guest(&memory); auto storage=guest.Writable(payload,font::kFontIdentityBytes,true);
  Check(storage.size()==font::kFontIdentityBytes,"Font corpus physical span unavailable");
  NSString* source=@R"MSL(#include <metal_stdlib>
using namespace metal;
kernel void sample_atlas(texture2d<float> image [[texture(0)]], sampler filter [[sampler(0)]],
                         device float4* output [[buffer(0)]],uint i [[thread_position_in_grid]]) {
  uint mip=i/256, cell=i%256;
  uint extent=image.get_width(mip), cell_extent=extent/16;
  uint2 p=uint2(cell%16,cell/16)*cell_extent+uint2(cell_extent/2,cell_extent*3/5);
  output[i]=image.sample(filter,(float2(p)+0.5f)/float(extent),level(float(mip)));
})MSL";
  NSError* native_error=nil;
  auto library=[context->device newLibraryWithSource:source options:nil error:&native_error];
  Check(library!=nil,ui::metal::MetalError(native_error,"Font corpus shader failed"));
  auto pipeline=[context->device newComputePipelineStateWithFunction:[library newFunctionWithName:@"sample_atlas"] error:&native_error];
  Check(pipeline!=nil,ui::metal::MetalError(native_error,"Font corpus pipeline failed"));
  ResourceStore resources(context,guest); resources.BeginFrame();std::string error;size_t sampled=0,uploads=0;
  constexpr uint32_t handle=0x71300000;
  for(const auto& c:cases) {
    std::ifstream input(root/c.path,std::ios::binary);
    std::vector<uint8_t> dds((std::istreambuf_iterator<char>(input)),{});
    Check(dds.size()==128+font::kFontIdentityBytes && std::memcmp(dds.data(),"DDS ",4)==0 &&
          std::memcmp(dds.data()+84,"DXT5",4)==0,"Font corpus is not a complete single-mip BC3 atlas");
    const std::span<const uint8_t> linear(dds.data()+128,font::kFontIdentityBytes);
    Check(font::StockFontIdentity(linear)==c.identity,"Font corpus identity differs from exported reference");
    Check(font::SelectVectorFontSet(c.id-1,c.identity)==c.set,"Wrong font atlas selected for episode payload");
    const auto* expected=font::FindVectorFontAtlas(c.set,c.id-1);Check(expected!=nullptr,"Episode atlas unavailable");
    const auto mips=font::FontCoverageMips(expected->alpha,font::kFontAtlasExtent,font::kFontMipLevels);
    for(bool tiled:{false,true}) for(auto endian:{xenos::Endian::kNone,xenos::Endian::k8in16,xenos::Endian::k8in32,xenos::Endian::k16in32}) {
      for(uint32_t y=0;y<128;++y) for(uint32_t x=0;x<128;++x) {
        const size_t destination=tiled ? size_t(texture_util::GetTiledOffset2D(x,y,128,4)) : size_t(y*128+x)*16;
        Check(destination+16<=storage.size(),"Corpus tiling exceeds physical span");
        // Construct input independently of the converter under test.
        const size_t byte_mask = endian == xenos::Endian::k8in16 ? 1 :
            endian == xenos::Endian::k8in32 ? 3 : endian == xenos::Endian::k16in32 ? 2 : 0;
        for (size_t i = 0; i < 16; ++i)
          storage[destination + i] = linear[size_t(y * 128 + x) * 16 + (i ^ byte_mask)];
      }
      resources.RegisterFont(handle,c.id); resources.Invalidate(handle);
      xenos::xe_gpu_texture_fetch_t fetch{};fetch.type=xenos::FetchConstantType::kTexture;
      fetch.dimension=xenos::DataDimension::k2DOrStacked;fetch.format=xenos::TextureFormat::k_DXT4_5;
      fetch.base_address=payload>>12;fetch.pitch=16;fetch.size_2d.width=fetch.size_2d.height=511;
      fetch.tiled=tiled;fetch.endianness=endian;fetch.swizzle=xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA;
      fetch.mip_filter=xenos::TextureFilter::kBaseMap;
      auto commands=[context->queue commandBuffer];
      auto texture=resources.Texture(handle,fetch,commands,[]{},error);
      Check(texture && texture->font_replacement && texture->image.width==font::kFontAtlasExtent,error);
      auto view=resources.View(texture,fetch,error);auto filter=resources.Sampler(fetch,error,texture.get());
      Check(view && filter,error);
      auto output=[context->device newBufferWithLength:font::kFontMipLevels*256*sizeof(float)*4 options:MTLResourceStorageModeShared];
      auto encoder=[commands computeCommandEncoder];
      [encoder setComputePipelineState:pipeline];[encoder setTexture:view atIndex:0];[encoder setSamplerState:filter atIndex:0];
      [encoder setBuffer:output offset:0 atIndex:0];
      [encoder dispatchThreads:MTLSizeMake(font::kFontMipLevels*256,1,1) threadsPerThreadgroup:MTLSizeMake(32,1,1)];
      [encoder endEncoding];[commands commit];[commands waitUntilCompleted];
      Check(commands.status==MTLCommandBufferStatusCompleted,ui::metal::MetalError(commands.error,"Font corpus GPU execution failed"));
      const auto* values=static_cast<const float*>(output.contents);
      for(size_t level=0;level<font::kFontMipLevels;++level) for(size_t cell=0;cell<256;++cell) {
        const size_t extent=font::kFontAtlasExtent>>level,cell_extent=extent/16;
        const size_t x=(cell%16)*cell_extent+cell_extent/2,y=(cell/16)*cell_extent+cell_extent*3/5;
        const auto* value=values+(level*256+cell)*4;
        Check(value[0]==1 && value[1]==1 && value[2]==1,"Episode atlas tint was changed");
        if (std::abs(value[3]-float(mips[level][y*extent+x])/255.0f)>=0.002f) {
          std::fprintf(stderr,"Corpus mismatch file=%s tiled=%u endian=%u mip=%zu cell=%zu xy=%zu,%zu wanted=%u actual=%.6f image=%s\n",
              c.path,unsigned(tiled),unsigned(endian),level,cell,x,y,unsigned(mips[level][y*extent+x]),value[3],texture->image.label.UTF8String);
          Check(false,std::string("Episode glyph coverage differs: ")+c.path);
        }
        ++sampled;
      }
      ++uploads;
    }
  }
  resources.Clear();heap->Release(payload);
  std::printf("metal_font_corpus=passed files=%zu uploads=%zu sampled_cells=%zu\n",std::size(cases),uploads,sampled);
}
