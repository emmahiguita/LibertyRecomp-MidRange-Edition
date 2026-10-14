// Actual pinned FidelityFX SR/FG adapter execution. No mock vendor functions.
#define VK_ENABLE_BETA_EXTENSIONS
#if defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#endif
#include <vulkan/vulkan.h>
#include "graphics/gta4_native/temporal/fsr_vulkan.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace t=rex::graphics::gta4_native::temporal;
namespace {
unsigned checks{};
std::atomic<unsigned> errors{};
void Check(bool ok,const std::string& message){++checks;if(!ok)throw std::runtime_error(message);}
void V(VkResult v){Check(v==VK_SUCCESS,"Vulkan result "+std::to_string(v));}
VKAPI_ATTR VkBool32 VKAPI_CALL Debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*) {
  if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){++errors;std::fprintf(stderr,"VALIDATION %s\n",data->pMessage);}
  return VK_FALSE;
}
struct Buffer {VkBuffer handle{};VkDeviceMemory memory{};void* mapping{};};
struct Image {t::Image resource{};VkDeviceMemory memory{};};
struct Fixture {
  VkInstance instance{};VkDebugUtilsMessengerEXT messenger{};VkPhysicalDevice physical{};VkDevice device{};
  VkPhysicalDeviceMemoryProperties memory{};VkQueue queue{};uint32_t family{};VkCommandPool pool{};
  std::vector<Buffer> buffers;std::vector<Image> images;
  Fixture() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_3;app.pApplicationName="Liberty actual FidelityFX GPU qualification";
    uint32_t n{};V(vkEnumerateInstanceExtensionProperties(nullptr,&n,nullptr));std::vector<VkExtensionProperties> extensions(n);
    V(vkEnumerateInstanceExtensionProperties(nullptr,&n,extensions.data()));std::vector<const char*> enabled{VK_EXT_DEBUG_UTILS_EXTENSION_NAME};bool portability=false;
    for(const auto& e:extensions)if(!std::strcmp(e.extensionName,VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)){enabled.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);portability=true;}
    const char* validation="VK_LAYER_KHRONOS_validation";VkValidationFeatureEnableEXT sync=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT validation_features{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};validation_features.enabledValidationFeatureCount=1;validation_features.pEnabledValidationFeatures=&sync;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ci.pNext=&validation_features;ci.pApplicationInfo=&app;
    ci.flags=portability?VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR:0;ci.enabledExtensionCount=uint32_t(enabled.size());ci.ppEnabledExtensionNames=enabled.data();
    ci.enabledLayerCount=1;ci.ppEnabledLayerNames=&validation;V(vkCreateInstance(&ci,nullptr,&instance));
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;debug.pfnUserCallback=Debug;
    auto create_debug=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT"));
    Check(bool(create_debug),"debug utilities unavailable");V(create_debug(instance,&debug,nullptr,&messenger));
    V(vkEnumeratePhysicalDevices(instance,&n,nullptr));Check(n>0,"no physical GPU");std::vector<VkPhysicalDevice> physicals(n);V(vkEnumeratePhysicalDevices(instance,&n,physicals.data()));physical=physicals[0];
    VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(physical,&properties);std::printf("GPU %s API=%u\n",properties.deviceName,properties.apiVersion);
    Check(properties.apiVersion>=VK_API_VERSION_1_3,"fixture requires Vulkan 1.3 for explicit promoted feature negotiation");
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};f12.pNext=&f13;
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&f12;vkGetPhysicalDeviceFeatures2(physical,&features);
    VkPhysicalDeviceFeatures base{};base.shaderInt16=features.features.shaderInt16;
    base.shaderStorageImageExtendedFormats=features.features.shaderStorageImageExtendedFormats;
    base.shaderStorageImageReadWithoutFormat=features.features.shaderStorageImageReadWithoutFormat;
    base.shaderStorageImageWriteWithoutFormat=features.features.shaderStorageImageWriteWithoutFormat;
    VkPhysicalDeviceVulkan13Features e13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features e12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};e12.pNext=&e13;
    // Match production's enabled feature set. The pinned Vulkan SDK chooses
    // shader-model 5.1 permutations, which do not require forced wave64.
    e12.shaderFloat16=f12.shaderFloat16;
    std::printf("enabled float16=%u int16=%u imageExtended=%u imageRead=%u imageWrite=%u subgroupExtended=%u subgroupSize=%u\n",
        e12.shaderFloat16,base.shaderInt16,base.shaderStorageImageExtendedFormats,base.shaderStorageImageReadWithoutFormat,
        base.shaderStorageImageWriteWithoutFormat,e12.shaderSubgroupExtendedTypes,e13.subgroupSizeControl);std::fflush(stdout);
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,nullptr);std::vector<VkQueueFamilyProperties> queues(n);vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,queues.data());
    for(family=0;family<n;++family)if(queues[family].queueFlags&VK_QUEUE_COMPUTE_BIT)break;Check(family<n,"no compute queue");
    float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
    V(vkEnumerateDeviceExtensionProperties(physical,nullptr,&n,nullptr));extensions.resize(n);V(vkEnumerateDeviceExtensionProperties(physical,nullptr,&n,extensions.data()));enabled.clear();
    for(const auto& e:extensions)if(!std::strcmp(e.extensionName,"VK_KHR_portability_subset"))enabled.push_back("VK_KHR_portability_subset");
    VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.pNext=&e12;dc.pEnabledFeatures=&base;dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&qi;
    dc.enabledExtensionCount=uint32_t(enabled.size());dc.ppEnabledExtensionNames=enabled.data();V(vkCreateDevice(physical,&dc,nullptr,&device));
    vkGetDeviceQueue(device,family,0,&queue);vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pc.queueFamilyIndex=family;pc.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;V(vkCreateCommandPool(device,&pc,nullptr,&pool));
  }
  uint32_t Memory(uint32_t bits,VkMemoryPropertyFlags required){for(uint32_t i=0;i<memory.memoryTypeCount;++i)if((bits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&required)==required)return i;throw std::runtime_error("memory type unavailable");}
  Buffer NewBuffer(size_t size){Buffer b;VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=size;ci.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    V(vkCreateBuffer(device,&ci,nullptr,&b.handle));VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,b.handle,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;
    ai.memoryTypeIndex=Memory(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);V(vkAllocateMemory(device,&ai,nullptr,&b.memory));V(vkBindBufferMemory(device,b.handle,b.memory,0));V(vkMapMemory(device,b.memory,0,VK_WHOLE_SIZE,0,&b.mapping));buffers.push_back(b);return b;}
  Image NewImage(t::Extent extent,VkFormat format){Image i;i.resource.format=format;i.resource.extent=extent;
    i.resource.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=format;ci.extent={extent.width,extent.height,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=i.resource.usage;
    V(vkCreateImage(device,&ci,nullptr,&i.resource.image));VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,i.resource.image,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=Memory(req.memoryTypeBits,0);
    V(vkAllocateMemory(device,&ai,nullptr,&i.memory));V(vkBindImageMemory(device,i.resource.image,i.memory,0));VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=i.resource.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    V(vkCreateImageView(device,&vi,nullptr,&i.resource.view));images.push_back(i);return i;}
  VkCommandBuffer Begin(){VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;VkCommandBuffer cb{};V(vkAllocateCommandBuffers(device,&ai,&cb));VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};V(vkBeginCommandBuffer(cb,&bi));return cb;}
  void Barrier(VkCommandBuffer cb,Image& image,VkImageLayout next){auto& i=image.resource;VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout=i.layout;b.newLayout=next;b.image=i.image;
    b.srcAccessMask=i.layout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);i.layout=next;}
  void Upload(VkCommandBuffer cb,Image& image,const Buffer& buffer){Barrier(cb,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={image.resource.extent.width,image.resource.extent.height,1};vkCmdCopyBufferToImage(cb,buffer.handle,image.resource.image,image.resource.layout,1,&copy);Barrier(cb,image,VK_IMAGE_LAYOUT_GENERAL);}
  void Read(VkCommandBuffer cb,Image& image,const Buffer& buffer){Barrier(cb,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={image.resource.extent.width,image.resource.extent.height,1};vkCmdCopyImageToBuffer(cb,image.resource.image,image.resource.layout,buffer.handle,1,&copy);
    VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.buffer=buffer.handle;b.size=VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&b,0,nullptr);Barrier(cb,image,VK_IMAGE_LAYOUT_GENERAL);}
  VkFence Submit(VkCommandBuffer cb){V(vkEndCommandBuffer(cb));VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence{};V(vkCreateFence(device,&fi,nullptr,&fence));VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&cb;V(vkQueueSubmit(queue,1,&si,fence));return fence;}
  void Wait(VkCommandBuffer cb,VkFence fence){V(vkWaitForFences(device,1,&fence,VK_TRUE,30'000'000'000));vkDestroyFence(device,fence,nullptr);vkFreeCommandBuffers(device,pool,1,&cb);Check(errors==0,"Vulkan validation errors");}
  ~Fixture(){if(device){vkDeviceWaitIdle(device);for(auto&i:images){vkDestroyImageView(device,i.resource.view,nullptr);vkDestroyImage(device,i.resource.image,nullptr);vkFreeMemory(device,i.memory,nullptr);}for(auto&b:buffers){vkUnmapMemory(device,b.memory);vkDestroyBuffer(device,b.handle,nullptr);vkFreeMemory(device,b.memory,nullptr);}vkDestroyCommandPool(device,pool,nullptr);vkDestroyDevice(device,nullptr);}if(messenger)reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"))(instance,messenger,nullptr);if(instance)vkDestroyInstance(instance,nullptr);}
};
using Pixel=std::array<_Float16,4>;
void CheckPixels(const Buffer& buffer,t::Extent extent,bool moving,float expected_center,const char* phase,unsigned frame) {
  const auto* pixels=static_cast<const Pixel*>(buffer.mapping);double maximum_error=0,weight=0,centroid=0;
  for(size_t y=0;y<extent.height;++y)for(size_t x=0;x<extent.width;++x){const auto& p=pixels[y*extent.width+x];
    for(size_t c=0;c<3;++c)Check(std::isfinite(float(p[c]))&&float(p[c])>=-0.01f&&float(p[c])<=1.1f,"nonfinite/out-of-range FSR output");
    if(!moving){const float expected[3]={0.25f,0.5f,1.0f};for(size_t c=0;c<3;++c)maximum_error=std::max(maximum_error,double(std::abs(float(p[c])-expected[c])));}
    else if(float(p[0])>0.5f){weight+=1;centroid+=double(x)+0.5;}}
  if(moving){Check(weight>0,"FSR lost the moving object");centroid/=weight;Check(std::abs(centroid-expected_center)<3.0,"FSR object centroid does not follow expected current/interpolated scene");}
  else Check(maximum_error<0.02,"FSR changed uniform scene color");
  std::printf("%s frame=%u moving=%d max_error=%.9g centroid=%.9g expected=%.9g\n",phase,frame,moving,maximum_error,centroid,expected_center);std::fflush(stdout);
}
}
int main(int argc,char**argv){try{
#if defined(__APPLE__)
  for(uint32_t i=0;i<_dyld_image_count();++i){const char* path=_dyld_get_image_name(i);
    if(path&&(std::strstr(path,"libamd_fidelityfx")||std::strstr(path,"libvulkan")))std::printf("runtime_library=%s\n",path);}
#endif
  bool run_fg=false,chain_upscale=false;
  for(int arg=1;arg<argc;++arg){
    if(std::strcmp(argv[arg],"--frame-generation")==0)run_fg=true;
    else if(std::strcmp(argv[arg],"--chained-upscale")==0)run_fg=chain_upscale=true;
    else throw std::runtime_error(std::string("unknown argument: ")+argv[arg]);
  }
  std::printf("fixture frame_generation=%d chained_upscale=%d\n",run_fg,chain_upscale);
  Fixture f;t::FsrUpscalerVulkan sr;t::FsrFrameGenerationVulkan fg;
  t::Configuration config{{64,64},{128,128},t::Quality::kPerformance,true,false,false};
  Check(sr.Initialize(f.physical,f.device,vkGetDeviceProcAddr),sr.last_error());
  t::Extent queried{};Check(sr.QueryRenderExtent(config.output_extent,config.quality,queried),sr.last_error());
  Check(queried==config.render_extent,"real FSR performance-mode render extent mismatch");
  std::printf("create_real_fsr_upscaler\n");std::fflush(stdout);Check(sr.Configure(config),sr.last_error());Check(errors==0,"validation during SR creation");
  std::printf("SR provider=%s\n",sr.provider_name().c_str());std::fflush(stdout);
  if(run_fg){Check(fg.Initialize(f.physical,f.device,vkGetDeviceProcAddr),fg.last_error());std::printf("create_real_fsr_frame_generation\n");std::fflush(stdout);Check(fg.Configure(config,VK_FORMAT_R16G16B16A16_SFLOAT),fg.last_error());Check(errors==0,"validation during FG creation");std::printf("FG provider=%s\n",fg.provider_name().c_str());std::fflush(stdout);}
  auto color=f.NewImage(config.render_extent,VK_FORMAT_R16G16B16A16_SFLOAT),depth=f.NewImage(config.render_extent,VK_FORMAT_R32_SFLOAT),motion=f.NewImage(config.render_extent,VK_FORMAT_R16G16_SFLOAT),output=f.NewImage(config.output_extent,VK_FORMAT_R16G16B16A16_SFLOAT);
  auto present=f.NewImage(config.output_extent,VK_FORMAT_R16G16B16A16_SFLOAT),generated=f.NewImage(config.output_extent,VK_FORMAT_R16G16B16A16_SFLOAT);
  std::printf("images color=%p depth=%p motion=%p output=%p present=%p generated=%p\n",
      reinterpret_cast<void*>(color.resource.image),reinterpret_cast<void*>(depth.resource.image),
      reinterpret_cast<void*>(motion.resource.image),reinterpret_cast<void*>(output.resource.image),
      reinterpret_cast<void*>(present.resource.image),reinterpret_cast<void*>(generated.resource.image));std::fflush(stdout);
  const size_t render_pixels=size_t(config.render_extent.width)*config.render_extent.height,output_pixels=size_t(config.output_extent.width)*config.output_extent.height;
  auto color_upload=f.NewBuffer(render_pixels*sizeof(Pixel)),depth_upload=f.NewBuffer(render_pixels*sizeof(float)),motion_upload=f.NewBuffer(render_pixels*sizeof(std::array<_Float16,2>)),present_upload=f.NewBuffer(output_pixels*sizeof(Pixel));
  auto output_read=f.NewBuffer(output_pixels*sizeof(Pixel)),generated_read=f.NewBuffer(output_pixels*sizeof(Pixel));
  for(unsigned frame=0;frame<16;++frame){const bool moving=frame>=8;const bool reset=frame==0||frame==8;const unsigned step=moving?frame-8:0;
    t::FrameInput input;input.render_extent=config.render_extent;input.output_extent=config.output_extent;input.sequence=frame+1;input.epoch=1;input.time_ns=uint64_t(frame+1)*16666667;
    input.frame_time_ms=16.666666666666668f;input.camera_near=0.1f;input.camera_far=1000;input.vertical_fov_radians=1.2f;input.reset=reset;input.motion_complete=input.scene_without_ui=true;
    Check(sr.QueryJitter(frame,config.render_extent,config.output_extent,input.jitter_x,input.jitter_y),sr.last_error());
    auto* c=static_cast<Pixel*>(color_upload.mapping);auto* z=static_cast<float*>(depth_upload.mapping);auto* m=static_cast<std::array<_Float16,2>*>(motion_upload.mapping);
    for(size_t y=0;y<config.render_extent.height;++y)for(size_t x=0;x<config.render_extent.width;++x){const size_t i=y*config.render_extent.width+x;
      const double sx=double(x)+0.5-input.jitter_x,sy=double(y)+0.5-input.jitter_y;
      const bool object=moving&&sx>=16+2*step&&sx<32+2*step&&sy>=20&&sy<44;
      c[i]=moving?(object?Pixel{1,0.2,0.05,1}:Pixel{0.05,0.05,0.2,1}):Pixel{0.25,0.5,1,1};z[i]=object?0.4f:0.8f;m[i]={_Float16(object?-2:0),0};}
    auto* display=static_cast<Pixel*>(present_upload.mapping);
    for(size_t y=0;y<config.output_extent.height;++y)for(size_t x=0;x<config.output_extent.width;++x){const bool object=moving&&x>=32+4*step&&x<64+4*step&&y>=40&&y<88;
      display[y*config.output_extent.width+x]=moving?(object?Pixel{1,0.2,0.05,1}:Pixel{0.05,0.05,0.2,1}):Pixel{0.25,0.5,1,1};}
    auto cb=f.Begin();f.Upload(cb,color,color_upload);f.Upload(cb,depth,depth_upload);f.Upload(cb,motion,motion_upload);f.Barrier(cb,output,VK_IMAGE_LAYOUT_GENERAL);
    input.color=color.resource;input.depth=depth.resource;input.motion=motion.resource;input.output=output.resource;
    Check(sr.Encode(cb,input),sr.last_error());Check(errors==0,"validation during real SR dispatch");
    f.Read(cb,output,output_read);
    bool generated_frame=false;
    if(run_fg){if(!chain_upscale)f.Upload(cb,present,present_upload);f.Barrier(cb,generated,VK_IMAGE_LAYOUT_GENERAL);
      // Analytic color isolates exact interpolation; chained mode also audits
      // the real SR output's visibility and layout at the FG consumer boundary.
      t::FsrFrameGenerationInput presentation;presentation.present_color=chain_upscale?output.resource:present.resource;presentation.generated_output=generated.resource;presentation.transfer_function=t::FsrTransferFunction::kScRgb;
      presentation.minimum_luminance=0;presentation.maximum_luminance=1000;presentation.camera_right[0]=1;presentation.camera_up[1]=1;presentation.camera_forward[2]=1;
      Check(fg.Encode(cb,input,presentation),fg.last_error());Check(errors==0,"validation during real FG dispatch");generated_frame=fg.has_generated_frame();Check(generated_frame==!reset,"FG reset/seed readiness mismatch");f.Read(cb,generated,generated_read);}
    auto fence=f.Submit(cb);sr.OnSubmitted();if(run_fg)fg.OnSubmitted();f.Wait(cb,fence);
    CheckPixels(output_read,config.output_extent,moving,float(48+4*step),"SR",frame);
    if(generated_frame)CheckPixels(generated_read,config.output_extent,moving,float(46+4*step),"FG",frame);
  }
  fg.Shutdown();sr.Shutdown();Check(errors==0,"validation during provider destruction");
  std::printf("actual_fsr_vulkan_gpu=passed frames=16 frame_generation=%d chained_upscale=%d checks=%u validation_errors=%u\n",run_fg,chain_upscale,checks,unsigned(errors));return 0;
}catch(const std::exception& e){std::fprintf(stderr,"actual FSR GPU: %s\n",e.what());return 1;}}
