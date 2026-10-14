// Headless execution of the exact production scene/background/coverage shader.
// Uses no title assets, vendor SDK, saved settings, or running game process.
#include "graphics/gta4_native/temporal/vulkan_scene_spv.h"
#include <vulkan/vulkan.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::atomic<unsigned> validation_errors{};
unsigned checks{}, cases{};
void Check(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
void V(VkResult result) { if (result != VK_SUCCESS) throw std::runtime_error("Vulkan result " + std::to_string(result)); }
VKAPI_ATTR VkBool32 VKAPI_CALL Debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    ++validation_errors; std::fprintf(stderr, "VALIDATION: %s\n", data->pMessage);
  }
  return VK_FALSE;
}
constexpr uint32_t kWidth=13, kHeight=9, kPixels=117;  // Python: 13*9.
struct Parameters {
  std::array<float,16> reprojection{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  std::array<float,2> jitter{},half_pixel{};
  std::array<uint32_t,2> extent{kWidth,kHeight};
  uint32_t reversed{},history{};
};
static_assert(sizeof(Parameters)==96 && offsetof(Parameters,extent)==80);
struct Buffer { VkBuffer buffer{}; VkDeviceMemory memory{}; void* mapping{}; };
struct Image { VkImage image{}; VkDeviceMemory memory{}; VkImageView view{}; VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED; };
struct Fixture {
  VkInstance instance{}; VkDebugUtilsMessengerEXT messenger{}; VkDevice device{};
  VkPhysicalDevice physical{}; VkPhysicalDeviceMemoryProperties memory{};
  VkQueue queue{}; uint32_t family{}; VkCommandPool commands{};
  VkDescriptorSetLayout set_layout{}; VkPipelineLayout pipeline_layout{}; VkPipeline pipeline{};
  VkDescriptorPool pool{}; VkDescriptorSet set{},audit_set{}; VkSampler sampler{};
  std::array<Image,4> images{}; std::array<Buffer,4> uploads{}; Buffer audit{},readback{};
  Fixture() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion=VK_API_VERSION_1_2;
    app.pApplicationName="Liberty temporal scene verification";
    uint32_t count{}; V(vkEnumerateInstanceExtensionProperties(nullptr,&count,nullptr));
    std::vector<VkExtensionProperties> extensions(count); V(vkEnumerateInstanceExtensionProperties(nullptr,&count,extensions.data()));
    std::vector<const char*> enabled{VK_EXT_DEBUG_UTILS_EXTENSION_NAME}; bool portability=false;
    for(const auto& e:extensions)if(!std::strcmp(e.extensionName,VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
      enabled.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME); portability=true;
    }
    const char* validation="VK_LAYER_KHRONOS_validation";
    VkValidationFeatureEnableEXT sync=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
    VkValidationFeaturesEXT features{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};
    features.enabledValidationFeatureCount=1; features.pEnabledValidationFeatures=&sync;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; create.pApplicationInfo=&app;
    create.flags=portability?VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR:0;
    create.enabledExtensionCount=uint32_t(enabled.size()); create.ppEnabledExtensionNames=enabled.data();
    create.enabledLayerCount=1; create.ppEnabledLayerNames=&validation; create.pNext=&features;
    V(vkCreateInstance(&create,nullptr,&instance));
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
    debug.pfnUserCallback=Debug;
    auto create_debug=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT"));
    Check(create_debug!=nullptr,"debug messenger unavailable"); V(create_debug(instance,&debug,nullptr,&messenger));
    V(vkEnumeratePhysicalDevices(instance,&count,nullptr)); Check(count!=0,"no physical GPU");
    std::vector<VkPhysicalDevice> physicals(count); V(vkEnumeratePhysicalDevices(instance,&count,physicals.data())); physical=physicals[0];
    VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(physical,&properties);
    std::printf("GPU %s\n",properties.deviceName);
    VkPhysicalDeviceFeatures available{}; vkGetPhysicalDeviceFeatures(physical,&available);
    Check(available.shaderStorageImageExtendedFormats,"RG16F storage image feature unavailable");
    vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);
    std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
    for(family=0;family<count;++family)if(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT)break;
    Check(family<count,"compute queue unavailable"); float priority=1;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
    V(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr)); extensions.resize(count);
    V(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,extensions.data())); enabled.clear();
    for(const auto& e:extensions)if(!std::strcmp(e.extensionName,"VK_KHR_portability_subset"))enabled.push_back("VK_KHR_portability_subset");
    VkPhysicalDeviceFeatures base{};base.shaderStorageImageExtendedFormats=VK_TRUE;
    VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.pEnabledFeatures=&base;dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&qi;
    dc.enabledExtensionCount=uint32_t(enabled.size());dc.ppEnabledExtensionNames=enabled.data();V(vkCreateDevice(physical,&dc,nullptr,&device));
    vkGetDeviceQueue(device,family,0,&queue);vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pc.queueFamilyIndex=family;pc.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    V(vkCreateCommandPool(device,&pc,nullptr,&commands));
    for(size_t i=0;i<images.size();++i) {
      images[i]=NewImage(i==2?VK_FORMAT_R16G16_SFLOAT:VK_FORMAT_R32_SFLOAT);
      uploads[i]=NewBuffer(kPixels*sizeof(float),VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    }
    audit=NewBuffer(4*sizeof(uint32_t),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    readback=NewBuffer(kPixels*sizeof(float),VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    VkSamplerCreateInfo sc{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};sc.magFilter=sc.minFilter=VK_FILTER_NEAREST;
    sc.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;sc.addressModeU=sc.addressModeV=sc.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    V(vkCreateSampler(device,&sc,nullptr,&sampler));
    std::array<VkDescriptorSetLayoutBinding,5> bindings{};
    for(uint32_t i=0;i<bindings.size();++i) {bindings[i].binding=i;bindings[i].descriptorCount=1;bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
      bindings[i].descriptorType=i==4?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:i?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;}
    bindings[0].pImmutableSamplers=&sampler;
    VkDescriptorSetLayoutCreateInfo lc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};lc.bindingCount=uint32_t(bindings.size());lc.pBindings=bindings.data();
    V(vkCreateDescriptorSetLayout(device,&lc,nullptr,&set_layout));
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Parameters)};
    VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.setLayoutCount=1;pl.pSetLayouts=&set_layout;pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&push;
    V(vkCreatePipelineLayout(device,&pl,nullptr,&pipeline_layout));
    using namespace rex::graphics::gta4_native::temporal;
    VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=sizeof(kVulkanSceneSpirv);sm.pCode=kVulkanSceneSpirv;
    VkShaderModule module{};V(vkCreateShaderModule(device,&sm,nullptr,&module));
    VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};cp.layout=pipeline_layout;
    cp.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};cp.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;cp.stage.module=module;cp.stage.pName="main";
    V(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&cp,nullptr,&pipeline));vkDestroyShaderModule(device,module,nullptr);
    std::array<VkDescriptorPoolSize,3> sizes{{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2},{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,6},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2}}};
    VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};dp.maxSets=2;dp.poolSizeCount=uint32_t(sizes.size());dp.pPoolSizes=sizes.data();
    V(vkCreateDescriptorPool(device,&dp,nullptr,&pool));
    VkDescriptorSetAllocateInfo sa{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};sa.descriptorPool=pool;sa.descriptorSetCount=1;sa.pSetLayouts=&set_layout;V(vkAllocateDescriptorSets(device,&sa,&set));
    V(vkAllocateDescriptorSets(device,&sa,&audit_set));
    std::array<VkDescriptorImageInfo,4> image_info{};std::array<VkWriteDescriptorSet,5> writes{};
    for(uint32_t i=0;i<4;++i) {image_info[i]={i?VK_NULL_HANDLE:sampler,images[i].view,VK_IMAGE_LAYOUT_GENERAL};
      writes[i]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[i].dstSet=set;writes[i].dstBinding=i;writes[i].descriptorCount=1;writes[i].descriptorType=bindings[i].descriptorType;writes[i].pImageInfo=&image_info[i];}
    VkDescriptorBufferInfo buffer_info{audit.buffer,0,4*sizeof(uint32_t)};writes[4]={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};writes[4].dstSet=set;writes[4].dstBinding=4;
    writes[4].descriptorCount=1;writes[4].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[4].pBufferInfo=&buffer_info;
    vkUpdateDescriptorSets(device,uint32_t(writes.size()),writes.data(),0,nullptr);
    // RecordAudit samples the captured R32 depth while that same image remains
    // bound at the unused write-only depth output. Match that production alias.
    image_info[0].imageView=images[1].view;
    for(auto& write:writes)write.dstSet=audit_set;
    vkUpdateDescriptorSets(device,uint32_t(writes.size()),writes.data(),0,nullptr);
  }
  uint32_t Memory(uint32_t bits,VkMemoryPropertyFlags flags) {
    for(uint32_t i=0;i<memory.memoryTypeCount;++i)if((bits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&flags)==flags)return i;
    throw std::runtime_error("compatible memory type unavailable");
  }
  Buffer NewBuffer(VkDeviceSize bytes,VkBufferUsageFlags usage) {
    Buffer out;VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=bytes;ci.usage=usage;V(vkCreateBuffer(device,&ci,nullptr,&out.buffer));
    VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,out.buffer,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;
    ai.memoryTypeIndex=Memory(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    V(vkAllocateMemory(device,&ai,nullptr,&out.memory));V(vkBindBufferMemory(device,out.buffer,out.memory,0));V(vkMapMemory(device,out.memory,0,VK_WHOLE_SIZE,0,&out.mapping));return out;
  }
  Image NewImage(VkFormat format) {
    Image out;VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=format;ci.extent={kWidth,kHeight,1};
    ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;
    ci.usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    V(vkCreateImage(device,&ci,nullptr,&out.image));VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,out.image,&req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=Memory(req.memoryTypeBits,0);
    V(vkAllocateMemory(device,&ai,nullptr,&out.memory));V(vkBindImageMemory(device,out.image,out.memory,0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=out.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    V(vkCreateImageView(device,&vi,nullptr,&out.view));return out;
  }
  void ImageBarrier(VkCommandBuffer cb,Image& image,VkImageLayout next) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcAccessMask=image.layout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.oldLayout=image.layout;b.newLayout=next;b.image=image.image;
    b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);image.layout=next;
  }
  void Run(const char* label,float depth,float previous,uint32_t history,std::array<uint32_t,4> expected,
           int poison=0,bool rotate=false,bool reversed=false) {
    ++cases;Parameters p;p.history=history;p.reversed=reversed;
    if(rotate)p.reprojection={0.984807753012208f,0,0,-0.17364817766693033f, 0,1,0,0, 0,0,1,0, 0.17364817766693033f,0,0,0.984807753012208f};
    std::vector<float> depths(kPixels,depth),prior(kPixels,previous);
    std::vector<std::array<uint16_t,2>> motion(kPixels,{0x4800,0x4400});  // Python binary16: {8,4}.
    if(poison==1)prior.back()=-1;
    if(poison==2)depths.back()=std::numeric_limits<float>::quiet_NaN();
    if(poison==3)motion.back()[0]=0x7e00;  // Python binary16 quiet NaN.
    if(poison==4)prior.back()=std::numeric_limits<float>::infinity();
    if(poison==5)depths.back()=-0.1f;
    std::memcpy(uploads[0].mapping,depths.data(),depths.size()*sizeof(float));
    std::memcpy(uploads[2].mapping,motion.data(),motion.size()*sizeof(motion[0]));
    std::memcpy(uploads[3].mapping,prior.data(),prior.size()*sizeof(float));
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=commands;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
    VkCommandBuffer cb{};V(vkAllocateCommandBuffers(device,&ai,&cb));VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};V(vkBeginCommandBuffer(cb,&bi));
    VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={kWidth,kHeight,1};
    for(uint32_t i:{0u,2u,3u}) {ImageBarrier(cb,images[i],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      vkCmdCopyBufferToImage(cb,uploads[i].buffer,images[i].image,images[i].layout,1,&copy);ImageBarrier(cb,images[i],VK_IMAGE_LAYOUT_GENERAL);}
    ImageBarrier(cb,images[1],VK_IMAGE_LAYOUT_GENERAL);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout,0,1,&set,0,nullptr);
    vkCmdPushConstants(cb,pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(p),&p);
    vkCmdDispatch(cb,2,2,1);  // Python ceil(13/8),ceil(9/8).
    for(auto& image:images)ImageBarrier(cb,image,VK_IMAGE_LAYOUT_GENERAL);
    VkBufferMemoryBarrier bb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};bb.buffer=audit.buffer;bb.size=4*sizeof(uint32_t);
    bb.srcQueueFamilyIndex=bb.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    bb.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;bb.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&bb,0,nullptr);
    vkCmdFillBuffer(cb,audit.buffer,0,4*sizeof(uint32_t),0);
    bb.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;bb.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,1,&bb,0,nullptr);
    p.history=2;vkCmdBindDescriptorSets(cb,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline_layout,0,1,&audit_set,0,nullptr);
    vkCmdPushConstants(cb,pipeline_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(p),&p);vkCmdDispatch(cb,2,2,1);
    bb.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;bb.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&bb,0,nullptr);
    ImageBarrier(cb,images[2],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkCmdCopyImageToBuffer(cb,images[2].image,images[2].layout,readback.buffer,1,&copy);
    bb.buffer=readback.buffer;bb.size=kPixels*sizeof(float);bb.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&bb,0,nullptr);
    V(vkEndCommandBuffer(cb));VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence{};V(vkCreateFence(device,&fc,nullptr,&fence));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cb;V(vkQueueSubmit(queue,1,&submit,fence));
    V(vkWaitForFences(device,1,&fence,VK_TRUE,5'000'000'000));
    std::array<uint32_t,4> observed{};std::memcpy(observed.data(),audit.mapping,sizeof(observed));
    if(observed!=expected)std::fprintf(stderr,"%s observed %u,%u,%u,%u expected %u,%u,%u,%u\n",label,
      observed[0],observed[1],observed[2],observed[3],expected[0],expected[1],expected[2],expected[3]);
    Check(observed==expected,"coverage counters disagree with fixture");
    const auto* vectors=static_cast<const std::array<_Float16,2>*>(readback.mapping);
    for(size_t i=0;i<kPixels;++i) {
      if(history && previous<0 && depth==(reversed?0.0f:1.0f)) {
        const double x=(double(i%kWidth)+0.5)/kWidth*2-1;
        const double y=1-(double(i/kWidth)+0.5)/kHeight*2;
        const double den=-0.17364817766693033*x+0.984807753012208;
        const double expected_x=rotate?((0.984807753012208*x+0.17364817766693033)/den-x)*6.5:0;
        const double expected_y=rotate?-(y/den-y)*4.5:0;
        Check(std::abs(float(vectors[i][0])-expected_x)<0.01&&std::abs(float(vectors[i][1])-expected_y)<0.01,"background motion disagrees with independent camera-ray calculation");
      } else if(!(poison==3&&i==kPixels-1))Check(float(vectors[i][0])==8&&float(vectors[i][1])==4,"scene preparation changed known object motion");
    }
    Check(validation_errors==0,"Vulkan validation error");vkDestroyFence(device,fence,nullptr);vkFreeCommandBuffers(device,commands,1,&cb);
    std::printf("scene_case=%s passed\n",label);
  }
  ~Fixture() {
    if(device) {
      vkDeviceWaitIdle(device);if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);if(pipeline_layout)vkDestroyPipelineLayout(device,pipeline_layout,nullptr);
      if(pool)vkDestroyDescriptorPool(device,pool,nullptr);if(set_layout)vkDestroyDescriptorSetLayout(device,set_layout,nullptr);if(sampler)vkDestroySampler(device,sampler,nullptr);
      for(auto& image:images){if(image.view)vkDestroyImageView(device,image.view,nullptr);if(image.image)vkDestroyImage(device,image.image,nullptr);if(image.memory)vkFreeMemory(device,image.memory,nullptr);}
      for(auto* b:{&uploads[0],&uploads[1],&uploads[2],&uploads[3],&audit,&readback}) {if(b->mapping)vkUnmapMemory(device,b->memory);if(b->buffer)vkDestroyBuffer(device,b->buffer,nullptr);if(b->memory)vkFreeMemory(device,b->memory,nullptr);}
      if(commands)vkDestroyCommandPool(device,commands,nullptr);vkDestroyDevice(device,nullptr);
    }
    if(messenger)reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"))(instance,messenger,nullptr);
    if(instance)vkDestroyInstance(instance,nullptr);
  }
};
}
int main() {
  try {
    Fixture f;
    f.Run("known_geometry",0.5f,0.3f,1,{kPixels,0,0,0});
    f.Run("new_foreground",0.5f,-1,1,{kPixels,0,0,kPixels});
    f.Run("visible_unknown_last_partial_group",0.5f,0.3f,1,{kPixels,0,0,1},1);
    f.Run("nonfinite_depth",0.5f,0.3f,1,{kPixels,0,1,0},2);
    f.Run("nonfinite_motion",0.5f,0.3f,1,{kPixels,1,0,0},3);
    f.Run("nonfinite_previous_depth",0.5f,0.3f,1,{kPixels,0,0,1},4);
    f.Run("out_of_range_depth",0.5f,0.3f,1,{kPixels,0,1,0},5);
    f.Run("camera_cut_background",1,-1,0,{kPixels,0,0,kPixels});
    f.Run("static_background",1,-1,1,{kPixels,0,0,0});
    f.Run("rotating_background",1,-1,1,{kPixels,0,0,0},0,true);
    f.Run("reversed_background",0,-1,1,{kPixels,0,0,0},0,false,true);
    f.Run("known_environment_preserved",1,0.4f,1,{kPixels,0,0,0},0,true);
    f.Run("valid_frame_after_rejection",0.5f,0.3f,1,{kPixels,0,0,0});
    std::printf("vulkan_temporal_scene_gpu=passed cases=%u checks=%u validation_errors=%u\n",cases,checks,unsigned(validation_errors));
    return 0;
  } catch(const std::exception& error) {std::fprintf(stderr,"scene GPU test: %s\n",error.what());return 1;}
}
