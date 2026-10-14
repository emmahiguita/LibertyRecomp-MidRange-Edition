#pragma once
// Headless Vulkan integration: the production image-access timeline on real queues.
// No game state or saved graphics settings are changed.
#include <vulkan/vulkan.h>
#include <rex/ui/vulkan/image_access.h>
#include <rex/ui/vulkan/native_queue_policy.h>
#include <array>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace rex::ui::vulkan;
namespace liberty::test {
std::atomic<unsigned> errors{0};
unsigned checks=0;
void Check(bool pass,const char* message){++checks;if(!pass)throw std::runtime_error(message);}
void V(VkResult v){if(v!=VK_SUCCESS)throw std::runtime_error("Vulkan result "+std::to_string(v));}
VKAPI_ATTR VkBool32 VKAPI_CALL Debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT,const VkDebugUtilsMessengerCallbackDataEXT* data,void*){
 if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){++errors;std::cerr<<"VALIDATION "<<data->pMessage<<'\n';}return VK_FALSE;
}
struct Allocation {VkDeviceMemory memory{};};
struct Image {VkImage image{}; VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;};
struct Buffer {VkBuffer buffer{};void* data{};};
struct Context {
 VkInstance instance{};VkDebugUtilsMessengerEXT messenger{};VkDevice device{};VkPhysicalDevice physical{};
 std::array<VkQueue,2> queues{};std::array<uint32_t,2> families{};std::array<VkCommandPool,2> pools{};
 VkPhysicalDeviceMemoryProperties memory{};std::vector<VkDeviceMemory> allocations;std::vector<VkImage> images;
 std::vector<VkBuffer> buffers;std::vector<VkSemaphore> semaphores;std::vector<VkFence> fences;
 explicit Context(bool validate){
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_2;app.pApplicationName="Liberty queue handoff verification";
  uint32_t n=0;V(vkEnumerateInstanceExtensionProperties(nullptr,&n,nullptr));std::vector<VkExtensionProperties> ie(n);V(vkEnumerateInstanceExtensionProperties(nullptr,&n,ie.data()));
  std::vector<const char*> ex;bool portability=false;for(auto&e:ie){if(!std::strcmp(e.extensionName,VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)){ex.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);portability=true;}if(validate&&!std::strcmp(e.extensionName,VK_EXT_DEBUG_UTILS_EXTENSION_NAME))ex.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);}
  const char* layer="VK_LAYER_KHRONOS_validation";VkValidationFeatureEnableEXT vf=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT;
  VkValidationFeaturesEXT vi{VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT};vi.enabledValidationFeatureCount=1;vi.pEnabledValidationFeatures=&vf;
  VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ii.pApplicationInfo=&app;ii.flags=portability?VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR:0;
  ii.enabledExtensionCount=uint32_t(ex.size());ii.ppEnabledExtensionNames=ex.data();if(validate){ii.enabledLayerCount=1;ii.ppEnabledLayerNames=&layer;ii.pNext=&vi;}V(vkCreateInstance(&ii,nullptr,&instance));
  if(validate){VkDebugUtilsMessengerCreateInfoEXT di{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};di.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;di.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;di.pfnUserCallback=Debug;auto create=(PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT");Check(bool(create),"debug utils unavailable");V(create(instance,&di,nullptr,&messenger));}
  V(vkEnumeratePhysicalDevices(instance,&n,nullptr));Check(n>0,"no physical device");std::vector<VkPhysicalDevice> ps(n);V(vkEnumeratePhysicalDevices(instance,&n,ps.data()));physical=ps[0];
  VkPhysicalDeviceVulkan12Properties p12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};p2.pNext=&p12;vkGetPhysicalDeviceProperties2(physical,&p2);Check(p12.driverID==VK_DRIVER_ID_MOLTENVK,"fixture must use project's MoltenVK ICD");
  VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};f2.pNext=&f12;vkGetPhysicalDeviceFeatures2(physical,&f2);Check(f12.timelineSemaphore,"timeline semaphores unavailable");
  vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,nullptr);std::vector<VkQueueFamilyProperties> qs(n);vkGetPhysicalDeviceQueueFamilyProperties(physical,&n,qs.data());families[0]=UINT32_MAX;
  for(uint32_t i=0;i<n;++i)if(qs[i].queueCount&&(qs[i].queueFlags&3)==3){families[0]=i;break;}
  families[1]=SelectNativeOffscreenQueueFamily(qs,families[0],true,true,true);Check(families[1]!=families[0],"no separate queue family");
  float priority=1;std::array<VkDeviceQueueCreateInfo,2> qi{};for(size_t i=0;i<2;++i){qi[i].sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;qi[i].queueFamilyIndex=families[i];qi[i].queueCount=1;qi[i].pQueuePriorities=&priority;}
  V(vkEnumerateDeviceExtensionProperties(physical,nullptr,&n,nullptr));std::vector<VkExtensionProperties> de(n);V(vkEnumerateDeviceExtensionProperties(physical,nullptr,&n,de.data()));std::vector<const char*> dex;for(auto&e:de)if(!std::strcmp(e.extensionName,"VK_KHR_portability_subset"))dex.push_back("VK_KHR_portability_subset");
  VkPhysicalDeviceVulkan12Features enabled{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};enabled.timelineSemaphore=VK_TRUE;VkPhysicalDeviceFeatures enabled_base{};enabled_base.sampleRateShading=f2.features.sampleRateShading;enabled_base.independentBlend=f2.features.independentBlend;VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.pEnabledFeatures=&enabled_base;dc.pNext=&enabled;dc.queueCreateInfoCount=2;dc.pQueueCreateInfos=qi.data();dc.enabledExtensionCount=uint32_t(dex.size());dc.ppEnabledExtensionNames=dex.data();V(vkCreateDevice(physical,&dc,nullptr,&device));
  vkGetPhysicalDeviceMemoryProperties(physical,&memory);for(size_t i=0;i<2;++i){vkGetDeviceQueue(device,families[i],0,&queues[i]);VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};ci.queueFamilyIndex=families[i];ci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;V(vkCreateCommandPool(device,&ci,nullptr,&pools[i]));}
  std::cout<<"DEVICE "<<p2.properties.deviceName<<" paint_family="<<families[0]<<" scene_family="<<families[1]<<" validation="<<validate<<'\n';
 }
 uint32_t Memory(uint32_t bits,VkMemoryPropertyFlags flags){for(uint32_t i=0;i<memory.memoryTypeCount;++i)if((bits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&flags)==flags)return i;throw std::runtime_error("memory type");}
 Buffer NewBuffer(size_t bytes,bool shared){Buffer b;VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=bytes;bi.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT;bi.sharingMode=shared?VK_SHARING_MODE_CONCURRENT:VK_SHARING_MODE_EXCLUSIVE;bi.queueFamilyIndexCount=shared?2:0;bi.pQueueFamilyIndices=shared?families.data():nullptr;V(vkCreateBuffer(device,&bi,nullptr,&b.buffer));buffers.push_back(b.buffer);VkMemoryRequirements req;vkGetBufferMemoryRequirements(device,b.buffer,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=Memory(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);VkDeviceMemory m;V(vkAllocateMemory(device,&ai,nullptr,&m));allocations.push_back(m);V(vkBindBufferMemory(device,b.buffer,m,0));V(vkMapMemory(device,m,0,bytes,0,&b.data));return b;}
 Image NewImage(VkFormat format=VK_FORMAT_R8G8B8A8_UINT,uint32_t width=16,uint32_t height=16,VkImageUsageFlags extra_usage=0){Image im;VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ii.imageType=VK_IMAGE_TYPE_2D;ii.format=format;ii.extent={width,height,1};ii.mipLevels=ii.arrayLayers=1;ii.samples=VK_SAMPLE_COUNT_1_BIT;ii.tiling=VK_IMAGE_TILING_OPTIMAL;ii.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|extra_usage;ii.sharingMode=VK_SHARING_MODE_CONCURRENT;ii.queueFamilyIndexCount=2;ii.pQueueFamilyIndices=families.data();V(vkCreateImage(device,&ii,nullptr,&im.image));images.push_back(im.image);VkMemoryRequirements req;vkGetImageMemoryRequirements(device,im.image,&req);VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=Memory(req.memoryTypeBits,0);VkDeviceMemory m;V(vkAllocateMemory(device,&ai,nullptr,&m));allocations.push_back(m);V(vkBindImageMemory(device,im.image,m,0));return im;}
 VkSemaphore Semaphore(bool timeline=true){VkSemaphore s;VkSemaphoreTypeCreateInfo ti{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};ti.semaphoreType=VK_SEMAPHORE_TYPE_TIMELINE;VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};ci.pNext=timeline?&ti:nullptr;V(vkCreateSemaphore(device,&ci,nullptr,&s));semaphores.push_back(s);return s;}
 VkFence Fence(){VkFence f;VkFenceCreateInfo ci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};V(vkCreateFence(device,&ci,nullptr,&f));fences.push_back(f);return f;}
 VkCommandBuffer Begin(size_t queue){VkCommandBuffer c;VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pools[queue];ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;V(vkAllocateCommandBuffers(device,&ai,&c));VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;V(vkBeginCommandBuffer(c,&bi));return c;}
 void Transition(VkCommandBuffer c,Image& im,VkImageLayout to){VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout=im.layout;b.newLayout=to;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=im.image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};b.srcAccessMask=im.layout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;vkCmdPipelineBarrier(c,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);im.layout=to;}
 VkFence Submit(size_t queue,VkCommandBuffer c,ImageAccessTimeline::Lease* lease=nullptr,VkSemaphore binary_wait=VK_NULL_HANDLE,VkSemaphore binary_signal=VK_NULL_HANDLE){V(vkEndCommandBuffer(c));VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&c;VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;if(binary_wait){si.waitSemaphoreCount=1;si.pWaitSemaphores=&binary_wait;si.pWaitDstStageMask=&stage;}if(binary_signal){si.signalSemaphoreCount=1;si.pSignalSemaphores=&binary_signal;}ImageAccessSubmit access;if(lease)Check(access.Attach(si,lease->ticket()),"attach failed");auto fence=Fence();V(vkQueueSubmit(queues[queue],1,&si,fence));if(lease)Check(lease->Commit(),"commit failed");return fence;}
 void Wait(VkFence f){V(vkWaitForFences(device,1,&f,VK_TRUE,5'000'000'000));}
 void WaitTimeline(VkSemaphore sem,uint64_t value){VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};wi.semaphoreCount=1;wi.pSemaphores=&sem;wi.pValues=&value;V(vkWaitSemaphores(device,&wi,5'000'000'000));}
 void HostSignal(VkSemaphore sem,uint64_t value){VkSemaphoreSignalInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO};si.semaphore=sem;si.value=value;V(vkSignalSemaphore(device,&si));}
 ~Context(){if(device){vkDeviceWaitIdle(device);for(auto p:pools)vkDestroyCommandPool(device,p,nullptr);for(auto f:fences)vkDestroyFence(device,f,nullptr);for(auto s:semaphores)vkDestroySemaphore(device,s,nullptr);for(auto b:buffers)vkDestroyBuffer(device,b,nullptr);for(auto i:images)vkDestroyImage(device,i,nullptr);for(auto m:allocations)vkFreeMemory(device,m,nullptr);vkDestroyDevice(device,nullptr);}if(messenger)((PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"))(instance,messenger,nullptr);if(instance)vkDestroyInstance(instance,nullptr);}
};

} // namespace liberty::test
