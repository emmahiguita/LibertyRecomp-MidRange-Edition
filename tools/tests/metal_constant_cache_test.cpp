#include "graphics/gta4_metal/constant_upload_cache.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>

using namespace rex::graphics::gta4_metal;
struct Upload {
  std::shared_ptr<std::vector<uint8_t>> storage;
  const void* data;
};
static Upload Make(std::span<const uint8_t> bytes, bool swap) {
  auto data = std::make_shared<std::vector<uint8_t>>(bytes.begin(), bytes.end());
  if (swap) for (size_t i = 0; i < data->size(); i += 4)
    std::reverse(data->begin() + i, data->begin() + i + 4);
  return {data, data->data()};
}
int main() {
  // Deliberate hash collisions: equality must include size, order and every
  // byte, including NaN payloads and the sign of zero. Exercise both source
  // shadowing and the budget-exhausted path (whole blocks and word tails).
  for (size_t budget : {size_t(0), size_t(128), size_t(8192)}) {
    ConstantUploadCache<Upload> cache(budget);
    std::vector<std::vector<uint8_t>> sources;
    std::vector<Upload> uploads;
    for (size_t size : {size_t(4), size_t(60), size_t(64), size_t(68), size_t(4096)}) {
      std::vector<uint8_t> bytes(size);
      constexpr uint8_t pattern[]{0x7f,0xc0,0x12,0x34,0x80,0x00,0x00,0x00};
      for (size_t i = 0; i < size; ++i) bytes[i] = pattern[i % sizeof(pattern)];
      sources.push_back(bytes);
      uploads.push_back(Make(bytes, true));
      cache.Insert(17, bytes, true, uploads.back(), true);
      auto host = Make(bytes, false);
      cache.Insert(17, bytes, false, host, true);
      assert(cache.Find(17, bytes, false)->storage == host.storage);
      assert(cache.Find(17, bytes, true)->storage == uploads.back().storage);
      for (size_t i = 0; i < size; ++i) {
        bytes[i] ^= 1;
        assert(!cache.Find(17, bytes, true));
        assert(!cache.Find(17, bytes, false));
        bytes[i] ^= 1;
      }
      assert(!cache.Find(18, bytes, true));
      assert(cache.source_bytes() <= budget);
    }
    for (size_t i = 0; i < sources.size(); ++i)
      assert(cache.Find(17, sources[i], true)->storage == uploads[i].storage);
    std::weak_ptr<std::vector<uint8_t>> life = uploads.front().storage;
    uploads.clear();
    assert(!life.expired());
    cache.Clear();
    assert(life.expired() && cache.size() == 0 && cache.source_bytes() == 0);
    for (auto& bytes : sources) assert(!cache.Find(17, bytes, true));
  }
  ConstantUploadCache<Upload> cache;
  // Grow, collide in buckets, clear and grow again. Handles from the old
  // submission must not survive, even if the hash and input address repeat.
  for (unsigned frame = 0; frame < 3; ++frame) {
    for (uint32_t i = 0; i < 10000; ++i) {
      const auto bytes = std::span(reinterpret_cast<const uint8_t*>(&i), sizeof(i));
      assert(!cache.Find(i, bytes, false));
      cache.Insert(i, bytes, false, Make(bytes, false), false);
    }
    for (uint32_t i = 0; i < 10000; ++i) {
      const auto bytes = std::span(reinterpret_cast<const uint8_t*>(&i), sizeof(i));
      const auto* found = cache.Find(i, bytes, false);
      assert(found && !std::memcmp(found->data, bytes.data(), bytes.size()));
    }
    cache.Clear();
  }
  std::puts("constant-cache: collision, growth, lifetime, endian, budget and exact-bit tests passed");
}
