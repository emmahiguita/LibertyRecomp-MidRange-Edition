// Exercise the production MetalPresenter and SDL event loop with a small GPU
// producer. No game assets, replacement presenter, or persistent settings writes.
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#include <rex/cvar.h>
#include <rex/diagnostics/policy.h>
#include <rex/logging.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context_sdl.h>
#include <rex/ui/metal/presenter.h>
#include "ui/metal/context.h"
#include "ui/metal/frame_ring.h"
#include "ui/metal/guest_output_context.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

REXCVAR_DEFINE_STRING(gta4_present_mode, "vsync", "Pacing fixture", "Fixture presentation mode");
REXCVAR_DEFINE_STRING(gta4_native_hdr_mode, "off", "Pacing fixture", "Fixture output format");

namespace {
void Require(bool ok, const std::string& error) { if (!ok) throw std::runtime_error(error); }
}
int main(int argc, char** argv) {
  @autoreleasepool {
    try {
      Require(argc == 2, "usage: metal-pacing-runtime-test passes.metallib");
      std::string error;
      Require(rex::diagnostics::Configure(true, "logging,presenter", &error), error);
      rex::InitLoggingEarly();
      rex::ui::SDLWindowedAppContext app;
      Require(app.Initialize(), "SDL initialization failed");
      auto context = rex::ui::metal::MetalContext::Create(error); Require(bool(context), error);
      NSError* native_error = nil;
      context->pass_library = [context->device newLibraryWithURL:[NSURL fileURLWithPath:@(argv[1])] error:&native_error];
      Require(context->pass_library != nil, rex::ui::metal::MetalError(native_error, "Pass library failed"));
      std::atomic<bool> gpu_lost{false}, failed{false};
      auto presenter = rex::ui::metal::MetalPresenter::Create(context, [&](bool, bool){gpu_lost = true;});
      Require(bool(presenter), "MetalPresenter creation failed");
      auto window = rex::ui::Window::Create(app, "Metal pacing validation (not the game)", 640, 360);
      Require(window && window->Open(), "Window creation failed");
      window->SetPresenter(presenter.get());
      NSWindow* cocoa = (__bridge NSWindow*)window->GetNativeWindowHandle();
      [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
      cocoa.level = NSFloatingWindowLevel;
      cocoa.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary;
      [cocoa makeKeyAndOrderFront:nil];
      [NSApp activateIgnoringOtherApps:YES];
      const auto screen_rate = cocoa.screen.maximumFramesPerSecond;
      std::printf("FIXTURE screen_hz=%ld\n", long(screen_rate)); std::fflush(stdout);
      std::thread producer([&]{
        try {
          rex::ui::metal::FrameRing ring;
          uint32_t sequence = 0;
          for (bool sync : {false, true}) for (uint32_t fps : {30u, 40u, 60u, 120u, 0u}) {
            Require(app.CallInUIThreadSynchronous([&]{
              rex::cvar::SetFlagByName("gta4_present_mode", sync ? "vsync" : "immediate");
              presenter->OnSurfaceResizeFromUIThread();
              window->RequestPaint();
            }), "UI mode transition rejected");
            const auto begin = rex::ui::FramePacerNowNs();
            const auto initial = presenter->submitted_frames();
            std::printf("CASE_BEGIN sync=%u fps=%u host_ns=%llu\n", sync, fps, (unsigned long long)begin); std::fflush(stdout);
            while (rex::ui::FramePacerNowNs() - begin < 2 * rex::ui::FramePacer::kSecond && !gpu_lost) {
              rex::ui::GuestOutputProvenance provenance{};
              provenance.submitted_frame = ++sequence;
              provenance.frame_rate_limit = fps;
              provenance.producer_backpressure = true;
              const bool accepted = presenter->RefreshGuestOutput(320, 180, 16, 9,
                [&](rex::ui::Presenter::GuestOutputRefreshContext& output){
                  @autoreleasepool {
                    auto* slot = ring.Begin(error); if (!slot) return false;
                    auto& metal = static_cast<rex::ui::metal::MetalGuestOutputRefreshContext&>(output);
                    auto commands = [context->queue commandBuffer];
                    auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
                    pass.colorAttachments[0].texture = metal.texture();
                    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
                    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
                    pass.colorAttachments[0].clearColor = MTLClearColorMake(double(sequence % 64) / 64.0, 0.25, 0.5, 1);
                    auto encoder = [commands renderCommandEncoderWithDescriptor:pass];
                    if (!encoder) { ring.Cancel(*slot); return false; }
                    [encoder endEncoding]; output.SetIs8bpc(false);
                    return ring.Commit(*slot, commands);
                  }
                }, provenance);
              Require(accepted, "Publication failed: " + error);
            }
            const auto finish = rex::ui::FramePacerNowNs();
            const auto count = presenter->submitted_frames() - initial;
            std::printf("CASE_END sync=%u fps=%u elapsed_ns=%llu submitted=%llu lost=%u\n", sync, fps,
              (unsigned long long)(finish - begin), (unsigned long long)count, gpu_lost.load()); std::fflush(stdout);
            Require(!gpu_lost && count > 5, "No presentation progress");
          }
          Require(ring.WaitIdle(error), error);
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
          const auto idle_begin = presenter->submitted_frames();
          std::this_thread::sleep_for(std::chrono::milliseconds(500));
          const auto idle_extra = presenter->submitted_frames() - idle_begin;
          std::printf("IDLE extra_submissions=%llu\n", (unsigned long long)idle_extra); std::fflush(stdout);
          Require(idle_extra == 0, "Idle presenter submits unchanged images");
        } catch (const std::exception& e) { failed = true; std::fprintf(stderr,"FIXTURE_ERROR %s\n",e.what()); }
        app.RequestDeferredQuit();
      });
      const auto loop_result = app.RunMainMessageLoop();
      presenter->CancelFramePacingWaits(); producer.join();
      window->SetPresenter(nullptr); presenter.reset(); window->RequestClose();
      rex::FlushLogging();
      Require(loop_result == 0 && !failed && !gpu_lost, "Runtime fixture failed");
      std::puts("RUNTIME_PASS"); return 0;
    } catch (const std::exception& e) { std::fprintf(stderr,"FIXTURE_ERROR %s\n",e.what()); return 1; }
  }
}
