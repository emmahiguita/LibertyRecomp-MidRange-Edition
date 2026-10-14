#include <array>
#include <chrono>
#include <cstdio>
#include <future>
#include <rex/ui/frame_pacer.h>
#include <stdexcept>
#include <thread>
using namespace rex::ui;
using namespace std::chrono_literals;
namespace {
size_t checks = 0;
void Check(bool x, const char *message) {
  ++checks;
  if (!x)
    throw std::runtime_error(message);
}
} // namespace
int main() {
  try {
    constexpr uint64_t origin = 1000000000;
    for (uint32_t fps : {30u, 40u, 60u, 120u}) {
      FramePacer p;
      p.Configure(fps, origin);
      uint64_t now = origin;
      for (uint64_t i = 0; i < 2000; ++i) {
        auto a = p.Plan(now);
        now += a.delay_ns;
        a = p.Plan(now);
        Check(a.delay_ns == 0, "software cadence delay");
        Check(a.slot_ns == origin + i * FramePacer::kSecond / fps,
              "rational cap regression");
        p.Queued(now);
      }
    }
    for (uint32_t cap : {0u, 60u, 120u}) {
      FramePacer p;
      p.Configure(cap, origin);
      std::array<uint64_t, 10> delivered{};
      size_t count = 0;
      for (uint64_t opportunity = 1;
           opportunity < 100 && count < delivered.size(); ++opportunity) {
        const auto target = origin + opportunity * FramePacer::kSecond / 120;
        const auto a = p.PlanForDisplay(target - 1000000, target);
        if (a.delay_ns)
          continue;
        p.SetMinimumInterval(25000000);
        delivered[count++] = target;
        p.Queued(target - 500000);
      }
      Check(count == delivered.size(), "interpolated pairs never delivered");
      for (size_t i = 1; i < count; ++i)
        Check(delivered[i] - delivered[i - 1] == 25000000,
              "generated/real output bunched instead of single paced interval");
      p.SetMinimumInterval(0);
      const auto a = p.PlanForDisplay(delivered.back() + 90000000,
                                      delivered.back() + 100000000);
      Check(a.delay_ns == 0, "disabling frame generation left stale pacing");
    }
    FramePacer p;
    p.Configure(0, origin);
    auto a = p.PlanForDisplay(origin, origin + 10000000);
    Check(!a.delay_ns, "uncapped initial display");
    p.SetMinimumInterval(20000000);
    p.Queued(origin + 1000);
    Check(p.PlanForDisplay(origin + 1000000, origin + 20000000).delay_ns > 0,
          "first midpoint failed to establish a real-frame interval");
    FramePublicationGate gate;
    gate.SetAvailable(true);
    auto serial = gate.Publish(0, true);
    auto wait =
        std::async(std::launch::async, [&] { return gate.Wait(serial); });
    Check(wait.wait_for(10ms) == std::future_status::timeout,
          "producer bypassed paired admission");
    gate.Admit(serial);
    Check(wait.wait_for(50ms) == std::future_status::ready && wait.get(),
          "generated submission did not admit the next render");
    const auto next = gate.Publish(0, true);
    auto wait2 =
        std::async(std::launch::async, [&] { return gate.Wait(next); });
    gate.Accept(serial);
    Check(wait2.wait_for(10ms) == std::future_status::timeout,
          "old real frame admitted an unrelated newer scene");
    gate.SetAvailable(false);
    Check(wait2.wait_for(50ms) == std::future_status::ready && wait2.get(),
          "occlusion did not release render admission");
    std::printf("metal_temporal_pacing=passed checks=%zu\n", checks);
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "Temporal pacing test: %s\n", e.what());
    return 1;
  }
}
