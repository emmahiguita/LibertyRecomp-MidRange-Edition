#include "texture_preparation.h"
#include "../gta4_native/native_texture_image_identity.h"
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/pipeline/texture/conversion.h>
#include <algorithm>
#include <bit>
#include <cstring>
#include <pthread.h>

namespace rex::graphics::gta4_metal {
namespace {
constexpr size_t kPreparationBudget = 128u * 1024u * 1024u;
constexpr size_t kStagingBudget = 64u * 1024u * 1024u;
const FormatInfo* HostFormat(const TextureInfo& info) {
  const auto base = GetBaseFormat(info.format);
  return FormatInfo::Get(base == xenos::TextureFormat::k_DXT3A ? xenos::TextureFormat::k_DXT2_3 :
      (base == xenos::TextureFormat::k_DXN || base == xenos::TextureFormat::k_CTX1) ? xenos::TextureFormat::k_8_8 :
      base == xenos::TextureFormat::k_DXT5A ? xenos::TextureFormat::k_8 : base);
}
bool Expanded(const TextureInfo& info) {
  const auto base = GetBaseFormat(info.format);
  return base == xenos::TextureFormat::k_CTX1 || base == xenos::TextureFormat::k_DXN || base == xenos::TextureFormat::k_DXT5A;
}
size_t SliceSize(const TextureInfo& info, const TexturePreparation::Mip& mip,
    size_t& row_pitch, size_t& image_pitch) {
  const auto* guest = info.format_info(); const auto* host = HostFormat(info);
  if (!guest || !host || !std::has_single_bit(guest->bytes_per_block()) || !host->bytes_per_block()) return 0;
  const size_t blocks_x = (size_t(mip.width) + guest->block_width - 1) / guest->block_width;
  const size_t blocks_y = (size_t(mip.height) + guest->block_height - 1) / guest->block_height;
  const size_t width = Expanded(info) ? blocks_x * guest->block_width : mip.width;
  const size_t height = Expanded(info) ? blocks_y * guest->block_height : mip.height;
  row_pitch = (((width + host->block_width - 1) / host->block_width) * host->bytes_per_block() + 255) & ~size_t(255);
  image_pitch = row_pitch * ((height + host->block_height - 1) / host->block_height);
  return image_pitch * mip.depth;
}
}

TexturePreparation::TexturePreparation() {
  for (auto& worker : workers_) worker = std::thread([this] { Worker(); });
}
TexturePreparation::~TexturePreparation() {
  Clear();
  { std::lock_guard lock(mutex_); stopping_ = true; }
  wake_.notify_all();
  for (auto& worker : workers_) if (worker.joinable()) worker.join();
}
void TexturePreparation::Prefetch(uint32_t handle, const xenos::xe_gpu_texture_fetch_t& fetch, GuestMemory memory) {
  if (auto found = pending_.find(handle); found != pending_.end()) {
    if (gta4_native::NativeTextureImageFetchEqual(found->second->fetch, fetch)) return;
    Invalidate(handle);
  }
  if (!handle || pending_.size() >= 32) return;
  auto job = std::make_shared<Job>(); job->fetch = fetch;job->frame=frame_;
  auto& info = job->info;
  if (!TextureInfo::Prepare(fetch, &info) || info.mip_min_level > info.mip_max_level ||
      info.mip_max_level >= xenos::kTextureMaxMips || info.width >= 16384 || info.height >= 16384 ||
      ResourceStore::TextureFormat(info.format) == MTLPixelFormatInvalid ||
      ResourceStore::TextureFormat(info.format) == MTLPixelFormatDepth32Float_Stencil8) return;
  const bool volume = info.dimension == xenos::DataDimension::k3D;
  const uint32_t layers = volume ? 1 : info.dimension == xenos::DataDimension::kCube ? 6 : info.is_stacked ? info.depth + 1 : 1;
  const auto layout = texture_util::GetGuestTextureLayout(info.dimension, info.pitch >> 5,
      info.width + 1, info.height + 1, info.depth + 1, info.is_tiled, info.format,
      info.has_packed_mips, info.memory.base_address != 0, info.mip_max_level);
  // Compute and reserve the total before copying. The budget includes queued,
  // running and completed-but-unconsumed jobs, including cancelled work.
  std::vector<std::span<const uint8_t>> sources;
  for (uint32_t level = info.mip_min_level; level <= info.mip_max_level; ++level) {
    Mip mip; mip.level = level; info.GetMipSize(level, &mip.width, &mip.height);
    mip.depth = volume ? std::max(1u, (info.depth + 1) >> level) : 1;
    const auto extent = info.GetMipExtent(level, true);
    mip.pitch_h = extent.block_pitch_h; mip.pitch_v = extent.block_pitch_v;
    const auto& guest_level = level == 0 ? layout.base : layout.mips[level];
    mip.layer_stride = guest_level.array_slice_stride_bytes;
    const auto address = info.GetMipLocation(level, &mip.x, &mip.y, true);mip.address=address;
    auto source = memory.Read(address, guest_level.level_data_extent_bytes, true);
    size_t row = 0, image = 0;
    const size_t size = SliceSize(info, mip, row, image);
    if (source.empty() || !size || size > kStagingBudget || layers > kPreparationBudget / size) return;
    const size_t decoded = size * layers;
    if (source.size() > kPreparationBudget - decoded ||
        job->reserved > kPreparationBudget - decoded - source.size()) return;
    job->reserved += decoded + source.size();
    job->mips.push_back(std::move(mip)); sources.push_back(source);
  }
  {
    std::lock_guard lock(mutex_);
    if (job->reserved > kPreparationBudget - pending_bytes_) return;
    pending_bytes_ += job->reserved;
  }
  // Copy on the serialized command owner. No worker reads these guest spans.
  for (size_t i = 0; i < sources.size(); ++i)
    job->mips[i].source.assign(sources[i].begin(), sources[i].end());
  pending_.emplace(handle, job);
  { std::lock_guard lock(mutex_); queue_.push_back(job); }
  ++statistics_.queued; wake_.notify_one();
}
std::shared_ptr<TexturePreparation::Job> TexturePreparation::Take(uint32_t handle,
    const xenos::xe_gpu_texture_fetch_t& fetch,GuestMemory memory) {
  auto found = pending_.find(handle);
  if (found == pending_.end()) { ++statistics_.inline_decodes; return {}; }
  if (!gta4_native::NativeTextureImageFetchEqual(found->second->fetch, fetch)) {
    Invalidate(handle); ++statistics_.inline_decodes; return {};
  }
  auto job = found->second;
  if(job->frame!=frame_&&!SourcesMatch(job->mips,memory)){
    ++statistics_.stale_jobs;Invalidate(handle);++statistics_.inline_decodes;return {};
  }
  { std::unique_lock lock(job->mutex);
    if (job->complete) ++statistics_.ready;
    else { ++statistics_.waited; job->wake.wait(lock, [&] { return job->complete; }); }
  }
  pending_.erase(found);
  { std::lock_guard lock(mutex_); pending_bytes_ -= job->reserved; job->reserved = 0; }
  return job;
}
bool TexturePreparation::Wait(const std::shared_ptr<Job>& job, std::string& error) {
  error = job->error; return error.empty() && !job->cancelled;
}
void TexturePreparation::Invalidate(uint32_t handle) {
  auto found = pending_.find(handle); if (found == pending_.end()) return;
  const auto job = found->second;
  { std::lock_guard lock(job->mutex);
    job->cancelled = true;
    if (job->complete) {
      std::lock_guard budget_lock(mutex_); pending_bytes_ -= job->reserved; job->reserved = 0;
    }
  }
  pending_.erase(found);
}
void TexturePreparation::Clear() {
  while (!pending_.empty()) Invalidate(pending_.begin()->first);
  published_.clear();statistics_.published_source_bytes=0;
}
void TexturePreparation::BeginFrame(){
  ++frame_;
  for(auto i=pending_.begin();i!=pending_.end();){
    const auto handle=i->first;const auto age=frame_-i->second->frame;++i;
    if(age>2)Invalidate(handle);
  }
}
bool TexturePreparation::SourcesMatch(const std::vector<Mip>& mips,GuestMemory memory){
  if(mips.empty())return false;
  for(const auto& mip:mips){
    const auto live=memory.Read(mip.address,mip.source.size(),true);
    if(live.size()!=mip.source.size()||std::memcmp(live.data(),mip.source.data(),live.size()))return false;
  }
  return true;
}
bool TexturePreparation::MatchesPublished(uint32_t handle,const xenos::xe_gpu_texture_fetch_t& fetch,GuestMemory memory){
  const auto old=published_.find(handle);
  if(old==published_.end()||!gta4_native::NativeTextureImageFetchEqual(old->second.fetch,fetch)||!SourcesMatch(old->second.mips,memory))return false;
  old->second.frame=frame_;++statistics_.unchanged_writes;return true;
}
void TexturePreparation::ForgetPublished(uint32_t handle){
  if(const auto old=published_.find(handle);old!=published_.end()){
    statistics_.published_source_bytes-=old->second.bytes;published_.erase(old);
  }
}
void TexturePreparation::RememberPublished(uint32_t handle,const std::shared_ptr<Job>& job){
  constexpr size_t budget=16u*1024u*1024u;
  ForgetPublished(handle);size_t size=0;
  for(const auto& m:job->mips){if(m.source.size()>budget-size)return;size+=m.source.size();}
  if(!size)return;
  while(published_.size()>=32||statistics_.published_source_bytes>budget-size){
    const auto victim=std::min_element(published_.begin(),published_.end(),[](const auto& a,const auto& b){return a.second.frame<b.second.frame;});
    if(victim==published_.end())return;ForgetPublished(victim->first);
  }
  published_.emplace(handle,Published{job->fetch,std::move(job->mips),size,frame_});statistics_.published_source_bytes+=size;
}
void TexturePreparation::Worker() {
  pthread_setname_np("Liberty texture decode");
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
  for (;;) {
    std::shared_ptr<Job> job;
    { std::unique_lock lock(mutex_); wake_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
      if (stopping_ && queue_.empty()) return;
      job = queue_.front(); queue_.pop_front();
    }
    try { if (!job->cancelled) Decode(*job); }
    catch (const std::exception& error) { job->error = error.what(); }
    { std::lock_guard lock(job->mutex);
      if (job->cancelled) {
        std::lock_guard budget_lock(mutex_); pending_bytes_ -= job->reserved; job->reserved = 0;
      }
      job->complete = true;
    }
    job->wake.notify_all();
  }
}
void TexturePreparation::Decode(Job& job) {
  const auto& info = job.info; const auto base = GetBaseFormat(info.format);
  const auto* guest = info.format_info(); const auto* host = HostFormat(info);
  const uint32_t guest_block = guest->bytes_per_block(), host_block = host->bytes_per_block();
  const bool volume = info.dimension == xenos::DataDimension::k3D, expand = Expanded(info);
  const uint32_t layers = volume ? 1 : info.dimension == xenos::DataDimension::kCube ? 6 : info.is_stacked ? info.depth + 1 : 1;
  for (const auto& mip : job.mips) {
    for (uint32_t layer = 0; layer < layers; ++layer) {
      if (job.cancelled) return;
      Slice slice; slice.mip = mip.level; slice.layer = layer;
      slice.width = mip.width; slice.height = mip.height; slice.depth = mip.depth;
      slice.bytes.resize(SliceSize(info, mip, slice.row_pitch, slice.image_pitch));
      const uint32_t blocks_x = (mip.width + guest->block_width - 1) / guest->block_width;
      const uint32_t blocks_y = (mip.height + guest->block_height - 1) / guest->block_height;
      for (uint32_t z = 0; z < mip.depth; ++z) for (uint32_t y = 0; y < blocks_y; ++y) {
        if (job.cancelled) return;
        for (uint32_t x = 0; x < blocks_x; ++x) {
          const uint32_t sx = mip.x + x, sy = mip.y + y;
          const int64_t offset = info.is_tiled ? (volume ? texture_util::GetTiledOffset3D(sx, sy, z,
              mip.pitch_h, mip.pitch_v, std::countr_zero(guest_block)) :
              texture_util::GetTiledOffset2D(sx, sy, mip.pitch_h, std::countr_zero(guest_block))) :
              int64_t(((uint64_t(z) * mip.pitch_v + sy) * mip.pitch_h + sx) * guest_block);
          const uint64_t source_offset = uint64_t(layer) * mip.layer_stride + uint64_t(offset);
          if (offset < 0 || source_offset > mip.source.size() || guest_block > mip.source.size() - source_offset) {
            job.error = "Guest texture block is outside its captured mip span"; return;
          }
          const auto* input = mip.source.data() + source_offset;
          auto* output = slice.bytes.data() + size_t(z) * slice.image_pitch +
              (expand ? size_t(y) * guest->block_height * slice.row_pitch + size_t(x) * guest->block_width * host_block :
                        size_t(y) * slice.row_pitch + size_t(x) * host_block);
          switch (base) {
            case xenos::TextureFormat::k_CTX1: texture_conversion::ConvertTexelCTX1ToR8G8(info.endianness, output, input, slice.row_pitch); break;
            case xenos::TextureFormat::k_DXN: texture_conversion::ConvertTexelDXNToR8G8(info.endianness, output, input, slice.row_pitch); break;
            case xenos::TextureFormat::k_DXT5A: texture_conversion::ConvertTexelDXT5AToR8(info.endianness, output, input, slice.row_pitch); break;
            case xenos::TextureFormat::k_DXT3A: texture_conversion::ConvertTexelDXT3AToDXT3(info.endianness, output, input, host_block); break;
            default: texture_conversion::CopySwapBlock(info.endianness, output, input, host_block); break;
          }
        }
      }
      job.slices.push_back(std::move(slice));
    }
  }
  // Captured bytes remain immutable for cross-frame freshness checks and
  // exact redundant-write detection after publication.
}
TexturePreparation::StagingSlice TexturePreparation::Staging(id<MTLDevice> device,id<MTLCommandBuffer> commands,size_t bytes){
  if(!commands||!bytes||bytes>kStagingBudget||commands.status!=MTLCommandBufferStatusNotEnqueued)return {};
  const auto take=[&](Upload& upload)->StagingSlice{
    const size_t offset=(upload.offset+255)&~size_t(255);
    if(offset>upload.capacity||bytes>upload.capacity-offset)return {};
    upload.offset=offset+bytes;++statistics_.staging_slices;
    return {upload.buffer,offset,upload.data+offset};
  };
  if(current_upload_<staging_.size()&&staging_[current_upload_].owner==commands)
    if(auto slice=take(staging_[current_upload_])){++statistics_.staging_reuses;return slice;}
  for(size_t i=0;i<staging_.size();++i){
    auto& upload=staging_[i];
    if(upload.owner!=commands){
      if(upload.owner&&upload.owner.status<MTLCommandBufferStatusCompleted)continue;
      upload.offset=0;upload.owner=nil;
    }
    if(auto slice=take(upload)){upload.owner=commands;current_upload_=i;++statistics_.staging_reuses;return slice;}
  }
  constexpr size_t slab=2u*1024u*1024u;
  size_t capacity=std::max(slab,(bytes+255)&~size_t(255));
  const bool retain=capacity<=kStagingBudget-statistics_.staging_bytes;
  if(!retain)capacity=bytes;
  auto buffer=[device newBufferWithLength:capacity options:MTLResourceStorageModeShared|MTLResourceCPUCacheModeWriteCombined];
  if(!buffer)return {};
  auto* data=static_cast<uint8_t*>(buffer.contents);if(!data)return {};
  ++statistics_.staging_allocations;++statistics_.staging_slices;
  if(retain){staging_.push_back({buffer,commands,capacity,bytes,data});current_upload_=staging_.size()-1;statistics_.staging_bytes+=capacity;}
  return {buffer,0,data};
}
TexturePreparation::Statistics TexturePreparation::statistics() const {
  auto result = statistics_;
  // Read only by the owner; pending byte accounting is shared with workers.
  auto& self = const_cast<TexturePreparation&>(*this);
  std::lock_guard lock(self.mutex_); result.pending_bytes = pending_bytes_; return result;
}
}  // namespace rex::graphics::gta4_metal
