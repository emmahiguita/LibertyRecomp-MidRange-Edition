#pragma once
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <iterator>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace rex::graphics::gta4_native {
// Commands keep stable addresses, with only owning handles crossing queues.
// A fixed-size slab free list avoids libc++'s ad-hoc allocation list: small
// largest_required_pool_block settings on affected runtimes route command-sized
// allocations through that list, making FIFO retirement quadratic.
// The resource must outlive every Owner. Constructors and destructors execute
// outside its mutex; only free-list operations and occasional slab growth lock.
template <typename T>
class NativeCommandPool {
 public:
  static constexpr size_t kSlotsPerSlab = 64;
  struct Statistics {
    size_t slabs = 0;
    size_t live = 0;
    size_t free = 0;
    size_t reserved_bytes = 0;
  };
  struct Deleter {
    std::pmr::memory_resource* resource = nullptr;
    void operator()(T* value) const noexcept {
      if (!value) return;
      std::destroy_at(value);
      resource->deallocate(value, sizeof(T), alignof(T));
    }
  };
  using Owner = std::unique_ptr<T, Deleter>;

  // Recycling: retirement re-initializes a command in place (T::RecycleInPlace)
  // instead of destroying it, and Make() hands that object out as is. The
  // producer then neither constructs ~3 KB per command nor allocates buffers
  // the previous occupant already had.
  static constexpr bool kRecyclable = requires(T& value) { value.RecycleInPlace(); };
  void SetRecycling(bool enabled) { recycling_.store(enabled, std::memory_order_relaxed); }

  template <typename... Args>
  Owner Make(Args&&... args) {
    void* storage = resource_.allocate(sizeof(T), alignof(T));
    auto* slot = static_cast<Slot*>(storage);
    if (slot->constructed) {
      // Ordered after the retiring thread's writes by the free-list mutex.
      slot->constructed = false;
      T* recycled = std::launder(reinterpret_cast<T*>(slot->storage));
      if constexpr (sizeof...(Args) == 0) return Owner(recycled, Deleter{&resource_});
      std::destroy_at(recycled);
    }
    try {
      T* value = std::construct_at(static_cast<T*>(storage), std::forward<Args>(args)...);
      return Owner(value, Deleter{&resource_});
    } catch (...) {
      resource_.deallocate(storage, sizeof(T), alignof(T));
      throw;
    }
  }
  Statistics GetStatistics() const { return resource_.GetStatistics(); }

  // Retire one completed batch without taking the shared free-list mutex for
  // each command. Destructors keep their original order and run outside the
  // pool mutex; GPU ownership/retirement remains with the caller.
  template <typename Owners>
  void RetireBatch(Owners& owners) noexcept {
    Slot* first = nullptr;
    Slot* last = nullptr;
    size_t count = 0;
    for (auto& owner : owners) {
      if (!owner) continue;
      if (owner.get_deleter().resource != &resource_) {
        owner.reset();  // Preserve ownership even in a mixed-pool batch.
        continue;
      }
      T* object = owner.release();
      auto* slot = reinterpret_cast<Slot*>(object);
      if constexpr (kRecyclable) {
        if (recycling_.load(std::memory_order_relaxed)) {
          object->RecycleInPlace();
          slot->constructed = true;
        } else {
          std::destroy_at(object);
        }
      } else {
        std::destroy_at(object);
      }
      slot->next = first;
      first = slot;
      if (!last) last = slot;
      ++count;
    }
    if (first) resource_.ReturnList(first, last, count);
    owners.clear();
  }

 private:
  struct Slot {
    alignas(T) std::byte storage[sizeof(T)];
    Slot* next = nullptr;
    bool constructed = false;  // storage holds a recycled, default-state T.
  };
  static_assert(std::is_standard_layout_v<Slot> && offsetof(Slot, storage) == 0);
  struct Slab {
    Slab* next = nullptr;
    Slot slots[kSlotsPerSlab];
  };
  class FixedResource final : public std::pmr::memory_resource {
   public:
    ~FixedResource() override {
      // Recycled objects still own (empty but allocated) buffers.
      for (Slot* slot : local_)
        if (slot->constructed) std::destroy_at(std::launder(reinterpret_cast<T*>(slot->storage)));
      for (Slot* slot : free_)
        if (slot->constructed) std::destroy_at(std::launder(reinterpret_cast<T*>(slot->storage)));
      live_ -= local_.size();  // Taken by the allocator cache, never handed out.
      // Destruction occurs after the render worker has joined and owners drain.
      assert(live_ == 0);
      for (Slab* slab = slabs_; slab;) {
        Slab* next = slab->next;
        delete slab;
        slab = next;
      }
    }
    // The retiring thread just wrote these links, so walking them here is
    // cache-hot; the allocator then never reads cold slot memory.
    void ReturnList(Slot* first, Slot* last, size_t count) {
      (void)last;
      std::lock_guard lock(mutex_);
      assert(count <= live_);
      for (Slot* slot = first; slot; slot = slot->next) free_.push_back(slot);
      live_ -= count;
    }
    Statistics GetStatistics() const {
      std::lock_guard lock(mutex_);
      return {slab_count_, live_, slab_count_ * kSlotsPerSlab - live_,
              slab_count_ * sizeof(Slab)};
    }
   private:
    void* do_allocate(size_t bytes, size_t alignment) override {
      if (bytes != sizeof(T) || alignment != alignof(T)) throw std::bad_alloc();
      // Allocation is single-threaded (the producer holds its capture mutex),
      // while slots return from the retirement thread. The allocator swaps out
      // the whole shared free array under one lock and pops from a private
      // one. Free slots are tracked as a pointer array, not an intrusive list:
      // reading a link from a command freed on another core was a cache miss
      // on every allocation.
      if (local_.empty()) {
        std::lock_guard lock(mutex_);
        if (free_.empty()) {
          // Nothing changes until allocation succeeds. Empty storage is not a T.
          Slab* slab = new Slab;
          slab->next = slabs_;
          slabs_ = slab;
          ++slab_count_;
          free_.reserve(free_.size() + kSlotsPerSlab);
          for (Slot& slot : slab->slots) free_.push_back(&slot);
        }
        local_.swap(free_);
        live_ += local_.size();  // Counted as live while cached; never returned uncached.
      }
      Slot* slot = local_.back();
      local_.pop_back();
      // Construction zero-fills the whole command, so start owning the next
      // free slot's lines now; the following allocation is microseconds away.
      // (Zeroing the slot here instead measured worse: the cost is the line
      // ownership transfer from the recorder's core, not the stale data.)
      if (!local_.empty()) {
        const auto* next = reinterpret_cast<const char*>(local_.back());
        for (size_t offset = 0; offset < sizeof(Slot); offset += 64)
          __builtin_prefetch(next + offset, 1);
      }
      return static_cast<void*>(slot->storage);
    }
    void do_deallocate(void* value, size_t bytes, size_t alignment) override {
      assert(bytes == sizeof(T) && alignment == alignof(T));
      (void)bytes;
      (void)alignment;
      // storage is the first member; retiring a slot requires no owner search.
      auto* slot = reinterpret_cast<Slot*>(value);
      std::lock_guard lock(mutex_);
      assert(live_ != 0);
      free_.push_back(slot);
      --live_;
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
      return this == &other;
    }
    mutable std::mutex mutex_;
    std::vector<Slot*> local_;  // Allocator-private free slots.
    std::vector<Slot*> free_;   // Shared, under mutex_.
    Slab* slabs_ = nullptr;
    size_t slab_count_ = 0;
    size_t live_ = 0;
  };
  FixedResource resource_;
  std::atomic<bool> recycling_{true};
};

// Forward iteration yields commands, never handles. Element addresses stay
// stable as the handle vector grows. Take is used only for queue/batch handoff;
// moved-from batch positions must not be visited again.
template <typename T, bool Queue = false>
class NativeOwnedCommands {
 public:
  using Owner = typename NativeCommandPool<T>::Owner;
 private:
  using Container = std::conditional_t<Queue, std::deque<Owner>, std::vector<Owner>>;
  Container values_;
  template <typename It, typename Value>
  class Iterator {
   public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using reference = Value&;
    using pointer = Value*;
    Iterator() = default;
    explicit Iterator(It it) : it_(it) {}
    reference operator*() const { assert(*it_); return **it_; }
    pointer operator->() const { return &**this; }
    Iterator& operator++() { ++it_; return *this; }
    Iterator operator++(int) { auto old = *this; ++*this; return old; }
    bool operator==(const Iterator&) const = default;
   private:
    It it_{};
  };
 public:
  size_t size() const { return values_.size(); }
  bool empty() const { return values_.empty(); }
  size_t capacity() const requires (!Queue) { return values_.capacity(); }
  size_t handle_bytes() const {
    if constexpr (Queue) return values_.size() * sizeof(Owner);
    else return values_.capacity() * sizeof(Owner);
  }
  void reserve(size_t n) requires (!Queue) { values_.reserve(n); }
  void clear() { values_.clear(); }
  void clear(NativeCommandPool<T>& pool) { pool.RetireBatch(values_); }
  // Hands every owner to the caller, keeping this container's capacity.
  Container TakeAll() requires (!Queue) {
    Container taken;
    taken.swap(values_);
    values_.reserve(taken.size());
    return taken;
  }
  void swap(NativeOwnedCommands& other) noexcept { values_.swap(other.values_); }
  void push_back(Owner value) { assert(value); values_.push_back(std::move(value)); }
  Owner Take(size_t i) { assert(values_[i]); return std::move(values_[i]); }
  Owner TakeFront() requires Queue {
    auto value = Take(0); values_.pop_front(); return value;
  }
  T& operator[](size_t i) { assert(values_[i]); return *values_[i]; }
  const T& operator[](size_t i) const { assert(values_[i]); return *values_[i]; }
  T& front() { return (*this)[0]; }
  const T& front() const { return (*this)[0]; }
  T& back() { return (*this)[size() - 1]; }
  const T& back() const { return (*this)[size() - 1]; }
  auto begin() { return Iterator<typename Container::iterator,T>(values_.begin()); }
  auto end() { return Iterator<typename Container::iterator,T>(values_.end()); }
  auto begin() const { return Iterator<typename Container::const_iterator,const T>(values_.begin()); }
  auto end() const { return Iterator<typename Container::const_iterator,const T>(values_.end()); }
};
// Destroys retired command batches on a background thread. Command teardown
// (dozens of shared_ptr releases and vector frees per draw) was a measurable
// slice of the single render worker on phone CPUs. Commands own no GPU
// objects, and the pool's free list is mutex-protected, so only the timing of
// the release moves; reclamation that needs use_count() == 1 simply waits a
// little longer. Declare after the pool: it drains before the pool dies.
template <typename T>
class NativeCommandRetirer {
 public:
  using Batch = std::vector<typename NativeCommandPool<T>::Owner>;
  explicit NativeCommandRetirer(NativeCommandPool<T>& pool) : pool_(pool) {
    thread_ = std::thread([this] { Run(); });
  }
  ~NativeCommandRetirer() {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
    }
    condition_.notify_one();
    thread_.join();
  }
  NativeCommandRetirer(const NativeCommandRetirer&) = delete;
  NativeCommandRetirer& operator=(const NativeCommandRetirer&) = delete;
  void Retire(Batch&& batch) {
    if (batch.empty()) return;
    {
      std::lock_guard lock(mutex_);
      pending_.push_back(std::move(batch));
    }
    condition_.notify_one();
  }

 private:
  void Run() {
    std::unique_lock lock(mutex_);
    for (;;) {
      condition_.wait(lock, [this] { return stopping_ || !pending_.empty(); });
      if (pending_.empty()) return;  // stopping and drained
      Batch batch = std::move(pending_.front());
      pending_.pop_front();
      lock.unlock();
      pool_.RetireBatch(batch);
      lock.lock();
    }
  }
  NativeCommandPool<T>& pool_;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::deque<Batch> pending_;
  bool stopping_ = false;
  std::thread thread_;
};

}  // namespace rex::graphics::gta4_native
