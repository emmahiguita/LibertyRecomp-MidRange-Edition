#pragma once
#include <atomic>
#include <cstdint>
#include <limits>

namespace rex::memory {
// A validation result can be reused only while this token remains unchanged
// and nonzero. Mutation scopes run under the heap's existing recursive mutex.
// Zero invalidates old readers BEFORE any host protection or page-table write.
// A token never pins memory: callers retain the same lifetime obligations as a
// fresh QueryRangeAccess call. Nested scopes publish only at the outer boundary.
class AccessEpoch {
 public:
  AccessEpoch() : value_(Next()) {}
  uint64_t Read() const { return value_.load(std::memory_order_acquire); }
  class Change {
   public:
    explicit Change(AccessEpoch& owner) : owner_(owner) {
      if (!owner_.depth_++) owner_.value_.store(0, std::memory_order_release);
    }
    ~Change() {
      if (!--owner_.depth_) owner_.value_.store(Next(), std::memory_order_release);
    }
    Change(const Change&) = delete;
    Change& operator=(const Change&) = delete;
   private:
    AccessEpoch& owner_;
  };
 private:
  static uint64_t Next() {
    // Unique across heap destruction/recreation, including pointer reuse.
    // Exhaustion disables reuse rather than wrapping onto an old token.
    static std::atomic<uint64_t> next{1};
    auto value = next.load(std::memory_order_relaxed);
    while (value != std::numeric_limits<uint64_t>::max()) {
      if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) return value;
    }
    return 0;
  }
  std::atomic<uint64_t> value_;
  unsigned depth_ = 0;
};
}  // namespace rex::memory
