#include "native_vulkan_test_context.h"
using namespace liberty::test;
namespace {
void BlockedQueue(Context& c){
 auto gate=c.Semaphore();auto fence=c.Fence();uint64_t one=1;VkTimelineSemaphoreSubmitInfo ti{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};ti.waitSemaphoreValueCount=1;ti.pWaitSemaphoreValues=&one;VkPipelineStageFlags stage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.pNext=&ti;si.waitSemaphoreCount=1;si.pWaitSemaphores=&gate;si.pWaitDstStageMask=&stage;V(vkQueueSubmit(c.queues[0],1,&si,fence));
 auto offscreen=c.NewBuffer(4096,false);auto cb=c.Begin(1);vkCmdFillBuffer(cb,offscreen.buffer,0,4096,0x12345678);auto independent=c.Submit(1,cb);
 auto same_buffer=c.NewBuffer(4096,false);auto same_cb=c.Begin(0);vkCmdFillBuffer(same_cb,same_buffer.buffer,0,4096,0xabcdef12);auto same_fence=c.Submit(0,same_cb);
 const VkResult independent_result=vkWaitForFences(c.device,1,&independent,VK_TRUE,2'000'000'000);
 const VkResult same_result=vkGetFenceStatus(c.device,same_fence);const VkResult gate_result=vkGetFenceStatus(c.device,fence);
 c.HostSignal(gate,1);c.Wait(same_fence);c.Wait(independent);c.Wait(fence);
 Check(independent_result==VK_SUCCESS,"offscreen work blocked behind other queue");Check(same_result==VK_NOT_READY,"negative control unexpectedly passed blocked queue");Check(gate_result==VK_NOT_READY,"gate completed before signal");
 std::cout<<"BLOCKED_QUEUE independent_progress=true same_queue_control_blocked=true\n";
}
void ImageExchange(Context& c){
 constexpr size_t bytes=16*16*4;std::array<Image,3> images{c.NewImage(),c.NewImage(),c.NewImage()};std::array<ImageAccessTimeline,3> access;
 for(auto& a:access)a.Initialize(c.Semaphore());
 auto output=c.NewBuffer(128*(bytes+32),false);std::memset(output.data,0xCD,128*(bytes+32));
 auto binary=c.Semaphore(false);auto seed=c.Begin(0);c.Wait(c.Submit(0,seed,nullptr,VK_NULL_HANDLE,binary));
 for(uint32_t frame=0;frame<128;++frame){auto index=frame%3;auto& im=images[index];auto& timeline=access[index];
  {auto abandoned=timeline.Acquire();Check(abandoned.ticket().valid(),"abandoned reservation");}
  auto lease=timeline.Acquire();auto write=c.Begin(1);c.Transition(write,im,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);VkClearColorValue color{};color.uint32[0]=frame;color.uint32[1]=255-frame;color.uint32[2]=index;color.uint32[3]=255;VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};vkCmdClearColorImage(write,im.image,im.layout,&color,1,&range);c.Transition(write,im,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);auto write_done=c.Submit(1,write,&lease);
  auto read_lease=timeline.Acquire();auto read=c.Begin(0);VkBufferImageCopy copy{};copy.bufferOffset=frame*(bytes+32)+16;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={16,16,1};vkCmdCopyImageToBuffer(read,im.image,im.layout,output.buffer,1,&copy);VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=output.buffer;host.offset=copy.bufferOffset;host.size=bytes;vkCmdPipelineBarrier(read,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);auto read_done=c.Submit(0,read,&read_lease,frame==0?binary:VK_NULL_HANDLE);
  if(frame%8==7){c.Wait(read_done);c.Wait(write_done);}
 }
 for(auto& timeline:access){auto last=timeline.Acquire();auto ticket=last.ticket();last=ImageAccessTimeline::Lease{};c.WaitTimeline(ticket.semaphore,ticket.wait_value);}
 auto* pixels=(uint8_t*)output.data;
 for(uint32_t frame=0;frame<128;++frame){const auto offset=frame*(bytes+32);for(size_t j=0;j<16;++j){Check(pixels[offset+j]==0xCD,"leading guard");Check(pixels[offset+16+bytes+j]==0xCD,"trailing guard");}for(size_t j=0;j<16*16;++j){auto* p=pixels+offset+16+j*4;Check(p[0]==frame&&p[1]==255-frame&&p[2]==frame%3&&p[3]==255,"frame identity overwritten before reader");}}
 std::cout<<"IMAGE_EXCHANGE frames=128 mailbox_images=3 byte_exact=true guarded_readbacks=true abandoned_accesses=128\n";
}
}
int main(int argc,char**argv){try{{Context context(argc>1&&std::string(argv[1])=="--validation");BlockedQueue(context);ImageExchange(context);}Check(errors==0,"validation errors");std::cout<<"PASS checks="<<checks<<" validation_errors="<<errors.load()<<'\n';return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
