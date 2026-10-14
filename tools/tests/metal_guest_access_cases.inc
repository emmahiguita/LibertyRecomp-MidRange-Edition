// Exercise real guest heap mutations against the renderer's permission cache.
void ExerciseGuestAccess(rex::memory::Memory& memory) {
  using namespace rex::memory;
  using rex::graphics::gta4_metal::GuestMemory;
  GuestMemory guest(&memory);
  constexpr uint32_t size = 65536;
  constexpr uint32_t allocation = kMemoryAllocationReserve | kMemoryAllocationCommit;
  constexpr uint32_t rw = kMemoryProtectRead | kMemoryProtectWrite;
  for (bool physical : {false, true}) {
    auto* heap = physical ? memory.GetPhysicalHeap() : memory.LookupHeap(0x10000000);
    uint32_t address = 0;
    Check(heap->Alloc(size, size, allocation, rw, false, &address), "Permission fixture allocation failed");
    const auto query = [&](uint32_t start, size_t count) {
      const auto access = heap->QueryRangeAccess(start, uint32_t(uint64_t(start) + count - 1));
      Check(!guest.Read(start, count, physical).empty() ==
                (access == PageAccess::kReadOnly || access == PageAccess::kReadWrite),
            "Cached read permission differs from live heap");
      Check(!guest.Writable(start, count, physical).empty() == (access == PageAccess::kReadWrite),
            "Cached write permission differs from live heap");
    };
    query(address, size); query(address + 64, 32);
    auto epoch = heap->access_epoch();
    Check(heap->Protect(address, size, kMemoryProtectRead, nullptr), "Read-only protection failed");
    Check(heap->access_epoch() != epoch, "Protection did not invalidate permission cache");
    query(address, size); query(address + 64, 32);
    Check(guest.Writable(address, size, physical).empty(), "Cached writable permission survived protection");
    std::thread reader([&] { query(address, size); }); reader.join();
    Check(heap->Protect(address, size, 0, nullptr), "No-access protection failed");
    query(address, size); Check(guest.Read(address, size, physical).empty(), "Cached read survived no-access protection");
    Check(heap->Protect(address, size, rw, nullptr), "Writable protection restore failed");
    query(address, size);
    epoch = heap->access_epoch();
    Check(heap->Release(address), "Permission fixture release failed");
    Check(heap->access_epoch() != epoch, "Release did not invalidate permission cache");
    query(address, size); Check(guest.Read(address, size, physical).empty(), "Freed span retained cached access");
    Check(heap->AllocFixed(address, size, size, allocation, kMemoryProtectRead), "Same-address reallocation failed");
    query(address, size);
    Check(guest.Writable(address, size, physical).empty(), "Same-address allocation inherited old write access");
    Check(heap->Release(address), "Reallocated fixture release failed");
  }
  Check(guest.Read(UINT32_MAX, 16).empty(), "Wrapping permission range accepted");
  Check(guest.Read(1, SIZE_MAX).empty(), "Overflowing host-size range accepted");
  Check(guest.Read(0x1FFFFFFF, 16, true).empty(), "Cross-heap physical range accepted");
  std::puts("metal_guest_access=passed");
}

// Reference is the previous production Read path, including heap lookup,
// range validation and translation. Run explicitly to avoid timing normal tests.
__attribute__((noinline)) std::span<const uint8_t> ReadGuestWithoutCache(
    rex::memory::Memory& memory, uint32_t address, size_t size) {
  if (!size || uint64_t(address) + size > uint64_t{0x100000000}) return {};
  auto* heap = memory.LookupHeap(address);
  if (!heap) return {};
  const auto access = heap->QueryRangeAccess(address, uint32_t(uint64_t(address) + size - 1));
  const auto required = rex::memory::PageAccess::kReadOnly;
  if ((uint32_t(access) & uint32_t(required)) != uint32_t(required)) return {};
  return {memory.TranslateVirtual<const uint8_t*>(address), size};
}
void BenchmarkGuestAccess(rex::memory::Memory& memory) {
  if (!std::getenv("REX_METAL_BENCHMARK_ACCESS")) return;
  using namespace rex::memory;
  auto* heap = memory.LookupHeap(0x10000000);
  uint32_t address = 0;
  Check(heap->Alloc(65536, 65536, kMemoryAllocationReserve | kMemoryAllocationCommit,
                   kMemoryProtectRead | kMemoryProtectWrite, false, &address), "Benchmark allocation failed");
  rex::graphics::gta4_metal::GuestMemory guest(&memory);
  constexpr unsigned iterations = 200000;
  uintptr_t checksum = 0;
  for (size_t size : {size_t(32), size_t(65536)}) {
    Check(guest.Read(address, size).size() == size, "Benchmark warmup failed");
    for (unsigned repeat = 0; repeat < 7; ++repeat) for (bool cached : {false, true}) {
      const auto start = std::chrono::steady_clock::now().time_since_epoch().count();
      for (unsigned i = 0; i < iterations; ++i) {
        const auto bytes = cached ? guest.Read(address, size) : ReadGuestWithoutCache(memory, address, size);
        checksum ^= reinterpret_cast<uintptr_t>(bytes.data()) ^ bytes.size();
      }
      const auto end = std::chrono::steady_clock::now().time_since_epoch().count();
      std::printf("access_benchmark size=%zu cached=%u iterations=%u start=%lld end=%lld checksum=%llu\n",
                  size, unsigned(cached), iterations, static_cast<long long>(start),
                  static_cast<long long>(end), static_cast<unsigned long long>(checksum));
    }
  }
  Check(heap->Release(address), "Benchmark release failed");
}
