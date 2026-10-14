// Sampling through the production texture-view path. The oracle is the
// identity VkComponentMapping used by the macOS native Vulkan backend.
// Test-only MSL compilation; the application still uses offline libraries.
static void ExerciseTextureChannels(
    const std::shared_ptr<rex::ui::metal::MetalContext>& context,
    rex::graphics::gta4_metal::ResourceStore& resources) {
  using namespace rex::graphics;
  using namespace rex::graphics::gta4_metal;
  NSString* source = @R"(
#include <metal_stdlib>
using namespace metal;
kernel void sample_pair(texture2d<float> original [[texture(0)]],
                        texture2d<float> selected [[texture(1)]],
                        device float4* output [[buffer(0)]]) {
  constexpr sampler point(coord::normalized, filter::nearest, address::clamp_to_edge);
  output[0] = original.sample(point, float2(0.5f));
  output[1] = selected.sample(point, float2(0.5f));
}
)";
  NSError* native_error = nil;
  auto library = [context->device newLibraryWithSource:source options:nil error:&native_error];
  Check(library != nil, rex::ui::metal::MetalError(native_error, "Sampling fixture compilation failed"));
  auto pipeline = [context->device newComputePipelineStateWithFunction:[library newFunctionWithName:@"sample_pair"] error:&native_error];
  Check(pipeline != nil, "Sampling fixture pipeline failed");
  struct FormatCase { xenos::TextureFormat guest; MTLPixelFormat host; size_t block_bytes;
    std::array<uint8_t, 16> bytes; std::array<float, 4> expected; };
  const FormatCase cases[] = {
    {xenos::TextureFormat::k_8_8_8_8, MTLPixelFormatRGBA8Unorm, 4, {192,64,16,255}, {192.f/255,64.f/255,16.f/255,1}},
    {xenos::TextureFormat::k_8, MTLPixelFormatR8Unorm, 1, {192}, {192.f/255,0,0,1}},
    {xenos::TextureFormat::k_DXN, MTLPixelFormatRG8Unorm, 2, {192,64}, {192.f/255,64.f/255,0,1}},
    {xenos::TextureFormat::k_CTX1, MTLPixelFormatRG8Unorm, 2, {192,64}, {192.f/255,64.f/255,0,1}},
    {xenos::TextureFormat::k_DXT5A, MTLPixelFormatR8Unorm, 1, {192}, {192.f/255,0,0,1}},
    {xenos::TextureFormat::k_DXT1, MTLPixelFormatBC1_RGBA, 8, {0,248,31,0,0,0,0,0}, {1,0,0,1}},
    {xenos::TextureFormat::k_DXT2_3, MTLPixelFormatBC2_RGBA, 16,
      {255,255,255,255,255,255,255,255,0,248,31,0,0,0,0,0}, {1,0,0,1}},
    {xenos::TextureFormat::k_DXT4_5, MTLPixelFormatBC3_RGBA, 16,
      {255,128,0,0,0,0,0,0,0,248,31,0,0,0,0,0}, {1,0,0,1}},
  };
  size_t checked = 0;
  for (const auto& item : cases) {
    auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:item.host width:4 height:4 mipmapped:NO];
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView;
    auto resource = std::make_shared<TextureResource>();
    resource->image = [context->device newTextureWithDescriptor:descriptor];
    resource->info.format = item.guest;
    Check(resource->image != nil, "Sampling fixture texture allocation failed");
    auto staging = [context->device newBufferWithLength:1024 options:MTLResourceStorageModeShared];
    Check(staging != nil, "Sampling fixture upload allocation failed");
    std::memset(staging.contents, 0, staging.length);
    const bool compressed = item.block_bytes >= 8;
    for (size_t y = 0; y < (compressed ? 1 : 4); ++y)
      for (size_t x = 0; x < (compressed ? 1 : 4); ++x)
        std::memcpy(static_cast<uint8_t*>(staging.contents) + y*256 + x*item.block_bytes,
                    item.bytes.data(), item.block_bytes);
    auto upload = [context->queue commandBuffer]; auto blit = [upload blitCommandEncoder];
    [blit copyFromBuffer:staging sourceOffset:0 sourceBytesPerRow:256 sourceBytesPerImage:1024
        sourceSize:MTLSizeMake(4,4,1) toTexture:resource->image destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
    [blit endEncoding]; [upload commit]; [upload waitUntilCompleted];
    Check(upload.status == MTLCommandBufferStatusCompleted, "Sampling fixture upload failed");
    for (uint32_t swizzle : {uint32_t(xenos::XE_GPU_TEXTURE_SWIZZLE_RGBA),
         uint32_t(XE_GPU_MAKE_TEXTURE_SWIZZLE(B,G,R,A)), uint32_t(XE_GPU_MAKE_TEXTURE_SWIZZLE(R,R,R,R))}) {
      xenos::xe_gpu_texture_fetch_t fetch{}; fetch.swizzle = swizzle; fetch.format = item.guest;
      std::string error; auto selected = resources.View(resource, fetch, error); Check(selected != nil, error);
      auto output = [context->device newBufferWithLength:sizeof(float)*8 options:MTLResourceStorageModeShared];
      auto commands = [context->queue commandBuffer]; auto encoder = [commands computeCommandEncoder];
      [encoder setComputePipelineState:pipeline]; [encoder setTexture:resource->image atIndex:0];
      [encoder setTexture:selected atIndex:1]; [encoder setBuffer:output offset:0 atIndex:0];
      [encoder dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
      [encoder endEncoding]; [commands commit]; [commands waitUntilCompleted];
      Check(commands.status == MTLCommandBufferStatusCompleted, "Sampling fixture GPU execution failed");
      const auto* pixels = static_cast<const float*>(output.contents);
      for (size_t c = 0; c < 4; ++c) {
        Check(std::abs(pixels[c]-item.expected[c]) < 0.001f, "Raw texture does not match known source channels");
        if (std::abs(pixels[c+4]-item.expected[c]) >= 0.001f)
          throw std::runtime_error("Native macOS sampling mismatch: format=" + std::to_string(uint32_t(item.guest)) +
              " swizzle=" + std::to_string(swizzle) + " channel=" + std::to_string(c) +
              " expected=" + std::to_string(item.expected[c]) + " actual=" + std::to_string(pixels[c+4]));
      }
      ++checked;
    }
  }
  std::printf("metal_texture_channel_cases=%zu passed=true\n", checked);
}
