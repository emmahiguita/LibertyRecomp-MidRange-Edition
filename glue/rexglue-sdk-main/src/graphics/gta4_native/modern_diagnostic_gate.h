#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rex::graphics::gta4_native {
// Render-owner only. Each distinct failure gets its own counter, so a broken
// resolve cannot exhaust the allowance for a later missing sky pipeline.
class ModernDiagnosticGate {
 public:
  struct Entry { std::string key; uint64_t count = 0; };
  struct Result { bool report; uint64_t count; bool overflow; };
  Result Observe(std::string_view key) {
    ++total_;
    for (auto& entry : entries_) {
      if (entry.key != key) continue;
      const auto count = ++entry.count;
      return {Milestone(count), count, false};
    }
    if (entries_.size() == 256) {
      ++overflow_;
      return {Milestone(overflow_), overflow_, true};
    }
    entries_.push_back({std::string(key), 1});
    return {true, 1, false};
  }
  void Reset() { entries_.clear(); total_ = overflow_ = 0; }
  const auto& entries() const { return entries_; }
  uint64_t total() const { return total_; }
  uint64_t overflow() const { return overflow_; }
 private:
  static bool Milestone(uint64_t count) { return (count & (count - 1)) == 0; }
  std::vector<Entry> entries_;
  uint64_t total_ = 0, overflow_ = 0;
};
}  // namespace rex::graphics::gta4_native
