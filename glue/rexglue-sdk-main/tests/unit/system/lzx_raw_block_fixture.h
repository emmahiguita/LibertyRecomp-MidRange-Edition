#pragma once
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace lzx_test {
struct RawFrames {
  std::vector<uint8_t> compressed;
  std::vector<uint8_t> expected;
};

// Emit synthetic, non-game LZX bytes. Each block has a 3-bit raw type and
// 24-bit length; only the first stream header includes the Intel-transform bit.
// Header fields are MSB-first inside little-endian 16-bit words.
inline RawFrames RawBlocks(std::span<const uint32_t> lengths, bool cab_padding,
                            bool first_stream_frame = true) {
  RawFrames result;
  bool first = first_stream_frame;
  for (size_t block = 0; block < lengths.size(); ++block) {
    const uint32_t length = lengths[block];
    if (!length || length > 0xFFFFFFu) throw std::invalid_argument("raw LZX block size");
    const uint32_t header = ((3u << 24) | length) << (first ? 4u : 5u);
    for (unsigned shift : {16u, 24u, 0u, 8u})
      result.compressed.push_back(static_cast<uint8_t>(header >> shift));
    // Repeated match offsets R0, R1, R2; all are little-endian one.
    for (unsigned offset = 0; offset < 3; ++offset)
      result.compressed.insert(result.compressed.end(), {1, 0, 0, 0});
    for (uint32_t index = 0; index < length; ++index) {
      const uint8_t value = static_cast<uint8_t>(index * 29u + block * 53u + 7u);
      result.compressed.push_back(value);
      result.expected.push_back(value);
    }
    if (cab_padding && (length & 1u) && block + 1 < lengths.size())
      result.compressed.push_back(0);
    first = false;
  }
  // Actual readable look-ahead, not invented by the decoder.
  result.compressed.insert(result.compressed.end(), {0xA5, 0x5A, 0xC3, 0x3C});
  return result;
}
}  // namespace lzx_test
