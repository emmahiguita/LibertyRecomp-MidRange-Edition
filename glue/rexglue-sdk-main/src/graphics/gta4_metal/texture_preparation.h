#pragma once
#include "resources.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace rex::graphics::gta4_metal {
// Jobs own every source byte. Only the renderer owner publishes completed jobs
// or touches Metal encoders; workers never retain guest memory or ResourceStore.
class TexturePreparation {
 public:
  struct Slice {
    uint32_t mip = 0, layer = 0, width = 0, height = 0, depth = 0;
    size_t row_pitch = 0, image_pitch = 0;
    std::vector<uint8_t> bytes;
  };
  struct Mip {
    uint32_t level = 0, width = 0, height = 0, depth = 0, x = 0, y = 0,address=0;
    uint32_t pitch_h = 0, pitch_v = 0;
    size_t layer_stride = 0;
    std::vector<uint8_t> source;
  };
  struct Job {
    TextureInfo info{};
    xenos::xe_gpu_texture_fetch_t fetch{};
    std::vector<Mip> mips;
    std::vector<Slice> slices;
    std::string error;
    size_t reserved = 0;
    uint64_t frame=0;
    std::mutex mutex;
    std::condition_variable wake;
    bool complete = false;
    std::atomic<bool> cancelled{false};
  };
  struct Statistics {
    uint64_t queued = 0, ready = 0, waited = 0, inline_decodes = 0;
    uint64_t staging_allocations = 0, staging_reuses = 0;
    size_t pending_bytes = 0, staging_bytes = 0;
    uint64_t staging_slices=0,stale_jobs=0,unchanged_writes=0;
    size_t published_source_bytes=0;
  };
  TexturePreparation();
  ~TexturePreparation();
  // A rejected speculative request is decoded by the demand path instead.
  void Prefetch(uint32_t handle, const xenos::xe_gpu_texture_fetch_t&, GuestMemory);
  std::shared_ptr<Job> Take(uint32_t handle, const xenos::xe_gpu_texture_fetch_t&,GuestMemory);
  void BeginFrame();
  bool MatchesPublished(uint32_t,const xenos::xe_gpu_texture_fetch_t&,GuestMemory);
  void RememberPublished(uint32_t,const std::shared_ptr<Job>&);
  void ForgetPublished(uint32_t);
  bool Wait(const std::shared_ptr<Job>&, std::string& error);
  void Invalidate(uint32_t handle);
  void Clear();
  struct StagingSlice {
    id<MTLBuffer> buffer=nil;size_t offset=0;void* data=nullptr;
    explicit operator bool()const{return buffer&&data;}
  };
  StagingSlice Staging(id<MTLDevice>,id<MTLCommandBuffer>,size_t bytes);
  Statistics statistics() const;

 private:
  static void Decode(Job&);
  static bool SourcesMatch(const std::vector<Mip>&,GuestMemory);
  void Worker();
  std::unordered_map<uint32_t, std::shared_ptr<Job>> pending_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::shared_ptr<Job>> queue_;
  size_t pending_bytes_ = 0;
  uint64_t frame_=0;
  struct Published {xenos::xe_gpu_texture_fetch_t fetch{};std::vector<Mip> mips;size_t bytes=0;uint64_t frame=0;};
  std::unordered_map<uint32_t,Published> published_;
  bool stopping_ = false;
  std::array<std::thread, 2> workers_;
  struct Upload {
    id<MTLBuffer> buffer = nil;
    id<MTLCommandBuffer> owner = nil;
    size_t capacity=0,offset=0;uint8_t* data=nullptr;
  };
  std::vector<Upload> staging_;
  size_t current_upload_=SIZE_MAX;
  Statistics statistics_;
};
}  // namespace rex::graphics::gta4_metal
