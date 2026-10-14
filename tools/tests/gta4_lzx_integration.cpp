// Compiles the production hook alongside exact generated PPC decoder bodies.
// OS protection and guest allocation are fixture boundaries, not codec stubs.
#include "../../glue/rexglue-sdk-main/tests/unit/system/lzx_raw_block_fixture.h"
#include "gta4_init.h"
#undef REXLOG_INFO
#undef REXLOG_WARN
#undef REXLOG_ERROR
#define REXLOG_INFO(...) ((void)0)
#define REXLOG_WARN(...) ((void)0)
#define REXLOG_ERROR(...) ((void)0)
#include "../../glue/rexglue-sdk-main/gta4-recomp/src/gta4_lzx_hooks.cpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <sys/mman.h>
#include <thread>
#include <vector>

using namespace gta4::lzx;  // Test-only access to included production hook
                            // state.

std::atomic<uint64_t> g_lzx_reference_calls{0};
static std::atomic<size_t> checks{0};
#define CHECK(x)                                                                         \
  do {                                                                                   \
    checks.fetch_add(1, std::memory_order_relaxed);                                      \
    if (!(x))                                                                            \
      throw std::runtime_error("check failed at " + std::to_string(__LINE__) + ": " #x); \
  } while (false)
extern "C" void __imp__MmQueryAddressProtect(PPCContext& ctx, uint8_t*) {
  ctx.r3.u64 = 0x204;
}
extern "C" void rexcrt_memset(PPCContext& ctx, uint8_t* base) {
  std::memset(REX_RAW_ADDR(ctx.r3.u32), ctx.r4.u8, ctx.r5.u32);
}
extern "C" void rexcrt_memcpy(PPCContext& ctx, uint8_t* base) {
  std::memmove(REX_RAW_ADDR(ctx.r3.u32), REX_RAW_ADDR(ctx.r4.u32), ctx.r5.u32);
}
extern "C" void __imp__sub_82A15060(PPCContext&, uint8_t*) {}  // Fixture owns the guest arena.
static std::vector<uint8_t> Read(const char* path) {
  std::ifstream f(path, std::ios::binary);
  if (!f)
    throw std::runtime_error(path);
  return {(std::istreambuf_iterator<char>(f)), {}};
}
struct Frame {
  std::vector<uint8_t> input;
  size_t output;
};
static std::vector<Frame> Parse(const std::vector<uint8_t>& data) {
  CHECK(data.size() >= 20 && std::memcmp(data.data(), "RSC\5", 4) == 0);
  std::vector<Frame> frames;
  for (size_t off = 20; off < data.size();) {
    size_t hdr = data[off] == 255 ? 5 : 2;
    CHECK(hdr <= data.size() - off);
    size_t out = hdr == 5 ? (size_t(data[off + 1]) << 8) | data[off + 2] : 32768;
    size_t bytes = hdr == 5 ? (size_t(data[off + 3]) << 8) | data[off + 4]
                            : (size_t(data[off]) << 8) | data[off + 1];
    CHECK(bytes && bytes <= data.size() - off - hdr && out <= 32768);
    Frame f{std::vector<uint8_t>(bytes + 4, 0), out};
    std::copy_n(data.data() + off + hdr, std::min(bytes + 4, data.size() - off - hdr),
                f.input.data());
    frames.push_back(std::move(f));
    off += hdr + bytes;
  }
  return frames;
}
static void Initialize(PPCContext& ctx, uint8_t* base, uint32_t outer, bool host) {
  ctx.r3.u32 = outer;
  ctx.r4.u32 = ctx.r1.u32 - 0x1000;
  ctx.r5.u32 = 0x20000;
  ctx.r6.u32 = 20;
  ctx.r7.u32 = 1;
  if (host)
    sub_82A21F70(ctx, base);
  else
    __imp__sub_82A21F70(ctx, base);
  REX_STORE_U32(outer + 8, 1);
  ctx.r3.u32 = outer;
  __imp__sub_82A21680(ctx, base);
}
static void Decode(PPCContext& ctx, uint8_t* base, uint32_t outer, const Frame& f, uint32_t input,
                   uint32_t output, uint32_t size_addr, bool host) {
  std::memcpy(REX_RAW_ADDR(input), f.input.data(), f.input.size());
  ctx.r3.u32 = outer + 20;
  ctx.r4.u32 = f.output;
  ctx.r5.u32 = input;
  ctx.r6.u32 = f.input.size() - 4;
  ctx.r7.u32 = output;
  ctx.r8.u32 = 0x12345678;
  ctx.r9.u32 = size_addr;
  if (host) {
    const auto reference_before = g_lzx_reference_calls.load();
    sub_82A21BF0(ctx, base);
    CHECK(g_lzx_reference_calls.load() == reference_before);
  } else
    __imp__sub_82A21BF0(ctx, base);
}
int main(int argc, char** argv) try {
  CHECK(argc >= 4);
  const size_t capacity = 0x100001000ULL;
  uint8_t* base = static_cast<uint8_t*>(
      mmap(nullptr, capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
  CHECK(base != MAP_FAILED);
  auto image = Read(argv[1]);
  CHECK(image.size() <= 0x1300000);
  std::memcpy(base + 0x82000000ULL, image.data(), image.size());
  constexpr uint32_t guest = 0x10000, host = 0x80000, input = 0x100000, gout = 0x200000,
                     hout = 0x300000, written = 0x8000;
  PPCContext gc{}, hc{};
  gc.r1.u32 = 0x700000;
  hc.r1.u32 = 0x780000;
  std::vector<Frame> saved;
  {
    auto a = Read(argv[2]);
    auto b = Read(argv[3]);
    saved.push_back({a, b.size()});
  }
  // The fixed reference stream is supplied as input/output pairs after the
  // image.
  std::vector<std::vector<uint8_t>> expected;
  size_t pair_end = argc;
  for (size_t i = 2; i < size_t(argc); ++i)
    if (std::string_view(argv[i]) == "--corpus") {
      pair_end = i;
      break;
    }
  CHECK((pair_end - 2) % 2 == 0);
  saved.clear();
  for (size_t i = 2; i < pair_end; i += 2) {
    expected.push_back(Read(argv[i + 1]));
    saved.push_back({Read(argv[i]), expected.back().size()});
  }
  auto CheckStream = [&](const std::vector<Frame>& frames, bool use_expected) {
    Initialize(gc, base, guest, false);
    Initialize(hc, base, host, true);
    for (size_t i = 0; i < frames.size(); ++i) {
      const auto& f = frames[i];
      Decode(gc, base, guest, f, input, gout, written, false);
      uint32_t gs = gc.r3.u32, gw = REX_LOAD_U32(written);
      Decode(hc, base, host, f, input, hout, written, true);
      CHECK(gs == hc.r3.u32);
      CHECK(gs == 0);
      CHECK(gw == REX_LOAD_U32(written));
      CHECK(gw == f.output);
      CHECK(std::memcmp(base + gout, base + hout, gw) == 0);
      if (use_expected)
        CHECK(std::memcmp(base + hout, expected[i].data(), gw) == 0);
      for (uint32_t off : {kTotalOutputOffset, kDecodeCountOffset})
        CHECK(REX_LOAD_U32(guest + 20 + off) == REX_LOAD_U32(host + 20 + off));
      CHECK(REX_LOAD_U8(host + 20 + kOutputProtectionOffset) == 1);
    }
  };
  for (unsigned n = 0; n < 3; ++n)
    CheckStream(saved, true);
  // Synthetic Xbox frames exercise odd raw-block boundaries absent from the
  // original base-game corpus. Compare production code to compiled retail PPC.
  for (const auto& lengths : std::array<std::vector<uint32_t>, 5>{
           {{3, 7, 9}, {1, 32767}, {9987, 22781}, {2, 4, 7}, {32765, 3}}}) {
    const auto fixture = lzx_test::RawBlocks(lengths, false);
    CheckStream({Frame{fixture.compressed, fixture.expected.size()}}, false);
    CHECK(std::memcmp(base + hout, fixture.expected.data(), fixture.expected.size()) == 0);
  }
  std::vector<Frame> odd_frames;
  for (unsigned frame = 0; frame < 9; ++frame) {
    const std::array<uint32_t, 2> lengths =
        frame == 8 ? std::array<uint32_t, 2>{5, 9} : std::array<uint32_t, 2>{3, 32765};
    const auto fixture = lzx_test::RawBlocks(lengths, false, frame == 0);
    odd_frames.push_back({fixture.compressed, fixture.expected.size()});
  }
  CheckStream(odd_frames, false);

  // Reinitialize the same allocation without calling its destroy hook.
  CHECK(FindEntry(host + 20) != nullptr);
  hc.r3.u32 = host;
  hc.r4.u32 = written;
  hc.r5.u32 = 0x20000;
  hc.r6.u32 = 20;
  hc.r7.u32 = 1;
  sub_82A21F70(hc, base);
  CHECK(FindEntry(host + 20) == nullptr);
  CheckStream(saved, true);
  // A size-only initialization query cannot evict a live decoder.
  auto existing = FindEntry(host + 20);
  hc.r3.u32 = 0;
  hc.r4.u32 = written;
  hc.r5.u32 = 0x20000;
  hc.r6.u32 = host + 20;
  hc.r7.u32 = 1;
  sub_82A21F70(hc, base);
  CHECK(FindEntry(host + 20) == existing);
  // Malformed streams fail closed, remain failed, and recover only on reset.
  Initialize(hc, base, host, true);
  Frame bad{{0xff, 0, 0, 0, 0}, 32};
  Decode(hc, base, host, bad, input, hout, written, true);
  CHECK(hc.r3.u32 == 1);
  CHECK(REX_LOAD_U32(written) == 0);
  Decode(hc, base, host, saved[0], input, hout, written, true);
  CHECK(hc.r3.u32 == 1);
  CHECK(FindEntry(host + 20)->failed);
  hc.r3.u32 = host + 20;
  sub_82A21BA8(hc, base);
  Decode(hc, base, host, saved[0], input, hout, written, true);
  CHECK(hc.r3.u32 == 0);
  CHECK(std::memcmp(base + hout, expected[0].data(), expected[0].size()) == 0);
  // A decoder first observed halfway into a stream cannot use a fresh
  // dictionary.
  Initialize(hc, base, host, true);
  REX_STORE_U32(host + 20 + kTotalOutputOffset, 32768);
  Decode(hc, base, host, saved[0], input, hout, written, true);
  CHECK(hc.r3.u32 == 1);
  // Snapshot makes input/output overlap safe.
  Initialize(hc, base, host, true);
  Decode(hc, base, host, saved[0], input, input, written, true);
  CHECK(hc.r3.u32 == 0);
  CHECK(std::memcmp(base + input, expected[0].data(), expected[0].size()) == 0);
  // Physical aliases must follow REX_RAW_ADDR rather than raw base+address.
  Initialize(hc, base, host, true);
  Decode(hc, base, host, saved[0], 0xe0100000, 0xe0200000, written, true);
  CHECK(hc.r3.u32 == 0);
  CHECK(std::memcmp(REX_RAW_ADDR(0xe0200000), expected[0].data(), expected[0].size()) == 0);
  CHECK(!IsGuestRange(0xdffffff0, 32));
  CHECK(!IsGuestRange(0xfffffff0, 32));
  // No-output calls preserve output totals but still follow the retail call
  // count.
  Initialize(gc, base, guest, false);
  Initialize(hc, base, host, true);
  Frame empty{{0, 0, 0, 0}, 0};
  Decode(gc, base, guest, empty, input, gout, written, false);
  Decode(hc, base, host, empty, input, hout, written, true);
  CHECK(gc.r3.u32 == hc.r3.u32);
  CHECK(hc.r3.u32 == 0);
  CHECK(REX_LOAD_U32(written) == 0);
  CHECK(REX_LOAD_U32(guest + 20 + kDecodeCountOffset) ==
        REX_LOAD_U32(host + 20 + kDecodeCountOffset));
  CHECK(REX_LOAD_U32(host + 20 + kTotalOutputOffset) == 0);
  // Invalid lengths fail before touching a caller output and poison that
  // context.
  for (unsigned field = 0; field < 4; ++field) {
    Initialize(hc, base, host, true);
    std::memset(base + hout, 0xa7, 64);
    DecodeArguments args{host + 20, 32768,  input, uint32_t(saved[0].input.size() - 4),
                         hout,      written};
    if (field == 0)
      args.expected_output = kMaximumFrameSize + 1;
    if (field == 1)
      args.compressed_size = kMaximumCompressedFrameSize + 1;
    if (field == 2)
      args.input = 0xfffffffd;
    if (field == 3)
      args.output = 0xfffffff0;
    DecodeFrame(hc, base, args);
    CHECK(hc.r3.u32 == 1);
    CHECK(REX_LOAD_U32(written) == 0);
    CHECK(FindEntry(host + 20)->failed);
    for (size_t j = 0; j < 64; ++j)
      CHECK(base[hout + j] == 0xa7);
  }
  // An unsupported window never silently invokes the old decoder.
  Initialize(hc, base, host, true);
  REX_STORE_U32(host + 20 + kWindowSizeOffset, 0x10000);
  Decode(hc, base, host, saved[0], input, hout, written, true);
  CHECK(hc.r3.u32 == 1);
  CHECK(REX_LOAD_U32(written) == 0);
  // Repeated truncated-frame failures are bounded and recover after a real
  // reset.
  for (size_t n = 1; n < 64; ++n) {
    Initialize(hc, base, host, true);
    Frame truncated{std::vector<uint8_t>(saved[0].input.begin(), saved[0].input.begin() + n),
                    saved[0].output};
    // Four actual readable padding bytes accompany each truncated compressed
    // span.
    truncated.input.resize(n + 4, 0);
    Decode(hc, base, host, truncated, input, hout, written, true);
    CHECK(hc.r3.u32 == 1);
    CHECK(FindEntry(host + 20)->failed);
  }
  CheckStream(saved, true);
  // Destruction retires registry state even if another observer keeps it alive.
  auto retained = FindEntry(host + 20);
  hc.r3.u32 = host;
  sub_82A15060(hc, base);
  CHECK(!FindEntry(host + 20));
  CHECK(retained->decoder != nullptr);
  // Independent contexts exercise registry insertion/removal concurrently.
  std::atomic<bool> concurrent_ok{true};
  std::vector<std::thread> threads;
  for (uint32_t i = 0; i < 4; ++i)
    threads.emplace_back([&, i] {
      try {
        PPCContext c{};
        uint32_t region = 0x1000000 + i * 0x100000;
        c.r1.u32 = region + 0xf0000;
        uint32_t obj = region + 0x1000;
        for (unsigned rep = 0; rep < 16; ++rep) {
          Initialize(c, base, obj, true);
          for (size_t f = 0; f < saved.size(); ++f) {
            Decode(c, base, obj, saved[f], region + 0x40000, region + 0x60000, region + 0x80000,
                   true);
            CHECK(c.r3.u32 == 0);
            CHECK(std::memcmp(base + region + 0x60000, expected[f].data(), expected[f].size()) ==
                  0);
          }
          c.r3.u32 = obj;
          sub_82A15060(c, base);
          CHECK(!FindEntry(obj + 20));
        }
      } catch (...) {
        concurrent_ok = false;
      }
    });
  for (auto& t : threads)
    t.join();
  CHECK(concurrent_ok);
  // A shared decoder's reset and decoding operations must serialize both the
  // native dictionary and the retail bookkeeping. Empty frames isolate that
  // ownership contract from any application-level ordering of resource data.
  Initialize(hc, base, host, true);
  Decode(hc, base, host, empty, input, hout, written, true);
  const auto shared_entry = FindEntry(host + 20);
  CHECK(shared_entry != nullptr);
  std::atomic<bool> shared_ok{true};
  std::thread resetting([&] {
    try {
      PPCContext reset{};
      reset.r1.u32 = 0x880000;
      for (unsigned iteration = 0; iteration < 128; ++iteration) {
        reset.r3.u32 = host + 20;
        sub_82A21BA8(reset, base);
      }
    } catch (...) {
      shared_ok = false;
    }
  });
  std::thread decoding([&] {
    try {
      PPCContext decode{};
      decode.r1.u32 = 0x980000;
      for (unsigned iteration = 0; iteration < 256; ++iteration) {
        Decode(decode, base, host, empty, 0xA00000, 0xA10000, 0xA20000, true);
        CHECK(decode.r3.u32 == 0);
      }
    } catch (...) {
      shared_ok = false;
    }
  });
  resetting.join();
  decoding.join();
  CHECK(shared_ok.load());
  CHECK(!shared_entry->failed);
  CHECK(shared_entry->decoder->total_output_bytes() == 0);
  uint64_t guest_ns = 0, host_ns = 0, bytes = 0, blocks = 0, files = 0;
  for (size_t i = pair_end + 1; i < size_t(argc); ++i) {
    auto frames = Parse(Read(argv[i]));
    CheckStream(frames, false);
    ++files;
    // Same inputs, initialization outside the timer, and validated outputs
    // above.
    Initialize(gc, base, guest, false);
    Initialize(hc, base, host, true);
    for (const auto& f : frames) {
      auto t = std::chrono::steady_clock::now();
      Decode(gc, base, guest, f, input, gout, written, false);
      guest_ns +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t)
              .count();
      CHECK(gc.r3.u32 == 0);
      t = std::chrono::steady_clock::now();
      Decode(hc, base, host, f, input, hout, written, true);
      host_ns +=
          std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t)
              .count();
      CHECK(hc.r3.u32 == 0);
      CHECK(std::memcmp(base + gout, base + hout, f.output) == 0);
      bytes += f.output;
      ++blocks;
    }
  }
  g_registry.clear();
  munmap(base, capacity);
  std::printf(
      "checks=%zu files=%llu blocks=%llu bytes=%llu guest_ns=%llu "
      "host_ns=%llu status=passed\n",
      checks.load(), (unsigned long long)files, (unsigned long long)blocks,
      (unsigned long long)bytes, (unsigned long long)guest_ns, (unsigned long long)host_ns);
  return 0;
} catch (const std::exception& e) {
  std::fprintf(stderr, "%s\n", e.what());
  return 1;
}
