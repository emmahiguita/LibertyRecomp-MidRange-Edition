#pragma once

#include <chrono>
#include <cstdint>

namespace rex::input {

inline uint64_t PointerMonotonicNanoseconds() noexcept {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

// SDL event timestamps use time since SDL initialization, not the host steady
// clock epoch. Preserve event age at the producer boundary, including queue
// delays, before a host-side gesture compares timestamps for expiry.
constexpr uint64_t RebasePointerTimestamp(uint64_t source_timestamp,
                                          uint64_t source_now,
                                          uint64_t host_now) noexcept {
  if (!source_timestamp || source_timestamp >= source_now) return host_now;
  const uint64_t age = source_now - source_timestamp;
  return age <= host_now ? host_now - age : 0;
}

}  // namespace rex::input
