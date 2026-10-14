#include <cassert>
#include <cstdio>
#include <thread>
#include <rex/memory/access_epoch.h>
#include "graphics/gta4_metal/guest_access_cache.h"
#include "graphics/gta4_metal/guest_constant_tracking.h"

static void CheckStridedPermissionReuse() {
  using rex::graphics::gta4_metal::GuestAccessCache;
  using rex::memory::PageAccess;
  constexpr uint32_t count = 16;
  constexpr uint32_t queries = 160;
  const int heap = 0;
  // The old low-bit-only hash evicted every entry in a ring of buffers
  // separated by 256 pages (1 MiB with 4 KiB pages). Also exercise other
  // power-of-two strides, range lengths, base pages, and epoch values.
  for (uint32_t stride : {1u, 16u, 256u, 4096u, 65536u}) {
    for (uint32_t base : {0u, 1u, 31u, 255u, 4096u, 32768u}) {
      for (uint64_t epoch : {1ull, 17ull, 255ull, 256ull, 4095ull, 65536ull}) {
        for (uint32_t width : {0u, 1u, 15u, 255u}) {
          GuestAccessCache cache;
          uint32_t hits = 0;
          for (uint32_t i = 0; i < queries + count; ++i) {
            const auto index = i % count;
            const auto first = base + index * stride;
            const auto last = first + width;
            const auto expected = index % 2 ? PageAccess::kReadOnly : PageAccess::kReadWrite;
            PageAccess access{};
            if (cache.Find(&heap, epoch, first, last, access)) {
              assert(access == expected);
              if (i >= count) ++hits;
            } else {
              cache.Remember(&heap, epoch, epoch, first, last, expected);
            }
          }
          // Bounded caches may collide, but small aligned working sets must
          // retain useful reuse instead of deterministically missing every time.
          assert(hits >= queries / 2);
          if (stride <= 16) assert(hits == queries);
        }
      }
    }
  }
}

int main() {
  CheckStridedPermissionReuse();
  using namespace rex::graphics::gta4_metal;
  using rex::memory::AccessEpoch;
  using rex::memory::PageAccess;
  AccessEpoch epoch, other;
  GuestAccessCache cache;
  PageAccess access{};
  const auto initial = epoch.Read();
  assert(initial && initial != other.Read());
  cache.Remember(&epoch, initial, initial, 1, 3, PageAccess::kReadWrite);
  assert(cache.Find(&epoch, initial, 1, 3, access) && access == PageAccess::kReadWrite);
  cache.Remember(&epoch, initial, initial, 257, 259, PageAccess::kReadOnly);
  assert(cache.Find(&epoch, initial, 1, 3, access) && access == PageAccess::kReadWrite);
  assert(cache.Find(&epoch, initial, 257, 259, access) && access == PageAccess::kReadOnly);
  assert(!cache.Find(&other, initial, 1, 3, access));
  assert(!cache.Find(&other, initial, 257, 259, access));
  assert(!cache.Find(&epoch, initial, 1, 4, access));
  assert(!cache.Find(&epoch, initial, 257, 260, access));
  {
    AccessEpoch::Change mutation(epoch);
    assert(!epoch.Read());
    assert(!cache.Find(&epoch, epoch.Read(), 1, 3, access));
    assert(!cache.Find(&epoch, epoch.Read(), 257, 259, access));
    { AccessEpoch::Change nested(epoch); assert(!epoch.Read()); }
    assert(!epoch.Read());
    cache.Remember(&epoch, initial, epoch.Read(), 7, 9, PageAccess::kReadWrite);
    assert(!cache.Find(&epoch, initial, 7, 9, access));
  }
  assert(epoch.Read() && epoch.Read() != initial);
  assert(!cache.Find(&epoch, epoch.Read(), 1, 3, access));
  assert(!cache.Find(&epoch, epoch.Read(), 257, 259, access));
  const auto before = epoch.Read();
  std::thread writer([&] { AccessEpoch::Change change(epoch); });
  writer.join();
  cache.Remember(&epoch, before, epoch.Read(), 1, 3, PageAccess::kReadWrite);
  assert(!cache.Find(&epoch, epoch.Read(), 1, 3, access));
  // Collision replacement must never turn a different page range into a hit.
  for (uint32_t page = 0; page < 4096; ++page) {
    cache.Remember(&epoch, epoch.Read(), epoch.Read(), page, page, PageAccess::kReadOnly);
    assert(cache.Find(&epoch, epoch.Read(), page, page, access));
    assert(access == PageAccess::kReadOnly);
  }
  for (uint32_t page = 4096; page < 8192; ++page)
    assert(!cache.Find(&epoch, epoch.Read(), page, page, access));

  GuestConstantTracking banks;
  banks.Observe(1, 0, 0, false);
  assert(!banks.Clean(0) && !banks.Clean(1));
  banks.Copied(0); banks.Copied(1);
  banks.Observe(1, 0, 0, false);
  assert(banks.Clean(0) && banks.Clean(1));
  banks.Observe(1, 0, 1, false);  // skipped draw consumes the guest's dirty bits
  banks.Observe(1, 0, 0, false);  // later draw must still refresh the pixel bank
  assert(banks.Clean(0) && !banks.Clean(1));
  banks.Copied(1);
  banks.Observe(1, UINT64_MAX, UINT64_MAX, false);  // cached-list replay
  assert(!banks.Clean(0) && !banks.Clean(1));
  banks.Copied(0); banks.Copied(1);
  banks.Observe(1, 0, 0, true);  // deferred light constants
  assert(!banks.Clean(0) && !banks.Clean(1));
  banks.Copied(0); banks.Copied(1);
  banks.Observe(2, 0, 0, false);
  assert(!banks.Clean(0) && !banks.Clean(1));
  banks.Copied(0); banks.Copied(1); banks.Reset();
  assert(!banks.Clean(0) && !banks.Clean(1));
  std::puts("metal_state_tracking=passed");
}
