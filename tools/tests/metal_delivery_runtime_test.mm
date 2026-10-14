// Real SDL/Metal presenter test: dormant registered UI, stale publication
// wakeups, visible animation, blank output and surface invalidations.
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
REXCVAR_DEFINE_STRING(gta4_present_mode,"vsync","Fixture","Presentation mode");
REXCVAR_DEFINE_STRING(gta4_native_hdr_mode,"off","Fixture","Output format");
static void Check(bool ok,const std::string& e) {if(!ok)throw std::runtime_error(e);}
struct Drawer : rex::ui::UIDrawer {
  rex::ui::Presenter* presenter=nullptr;
  unsigned remaining=0;
  void Draw(rex::ui::UIDrawContext&) override {
    if (remaining) {--remaining;presenter->RequestUIPaintFromUIThread();}
  }
};
int main(int argc,char** argv) {
 @autoreleasepool {
  try {
   Check(argc==2,"usage: delivery-test passes.metallib");
   std::string error;Check(rex::diagnostics::Configure(true,"logging,presenter",&error),error);rex::InitLoggingEarly();
   rex::ui::SDLWindowedAppContext app;Check(app.Initialize(),"SDL");
   auto context=rex::ui::metal::MetalContext::Create(error);Check(bool(context),error);
   NSError* e=nil;context->pass_library=[context->device newLibraryWithURL:[NSURL fileURLWithPath:@(argv[1])] error:&e];
   Check(context->pass_library!=nil,"passes");
   std::atomic<bool> lost{false},failed{false};
   auto presenter=rex::ui::metal::MetalPresenter::Create(context,[&](bool,bool){lost=true;});Check(bool(presenter),"presenter");
   auto window=rex::ui::Window::Create(app,"Metal delivery validation (not the game)",640,360);
   Check(window&&window->Open(),"window");window->SetPresenter(presenter.get());
   [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
   NSWindow* cocoa=(__bridge NSWindow*)window->GetNativeWindowHandle();
   [cocoa makeKeyAndOrderFront:nil];[NSApp activateIgnoringOtherApps:YES];
   Drawer drawer;drawer.presenter=presenter.get();presenter->AddUIDrawerFromUIThread(&drawer,1);
   std::thread producer([&]{
    try {
     using namespace std::chrono_literals;
     rex::ui::metal::FrameRing ring;uint32_t sequence=0;
     auto wait_for=[&](auto predicate,const char* text){
       const auto until=std::chrono::steady_clock::now()+3s;
       for(;!predicate()&&!lost&&std::chrono::steady_clock::now()<until;)std::this_thread::sleep_for(2ms);
       Check(!lost&&predicate(),text);
     };
     auto ui=[&](auto callback){Check(app.CallInUIThreadSynchronous(callback),"UI invocation");};
     auto publish=[&]{
       rex::ui::GuestOutputProvenance p{};p.submitted_frame=++sequence;
       auto before=presenter->delivery_statistics().new_game_images;
       Check(presenter->RefreshGuestOutput(320,180,16,9,[&](auto& output){
         @autoreleasepool {
          auto* slot=ring.Begin(error);if(!slot)return false;
          auto& metal=static_cast<rex::ui::metal::MetalGuestOutputRefreshContext&>(output);
          auto cb=[context->queue commandBuffer];auto pass=[MTLRenderPassDescriptor renderPassDescriptor];
          pass.colorAttachments[0].texture=metal.texture();pass.colorAttachments[0].loadAction=MTLLoadActionClear;
          pass.colorAttachments[0].storeAction=MTLStoreActionStore;
          pass.colorAttachments[0].clearColor=MTLClearColorMake(double(sequence%64)/64.0,0.25,0.5,1);
          auto enc=[cb renderCommandEncoderWithDescriptor:pass];if(!enc){ring.Cancel(*slot);return false;}
          [enc endEncoding];output.SetIs8bpc(false);return ring.Commit(*slot,cb);
         }
       },p),"publish: "+error);
       wait_for([&]{return presenter->delivery_statistics().new_game_images>before;},"new game image not submitted");
     };
     for(bool sync:{true,false}) {
       ui([&]{rex::cvar::SetFlagByName("gta4_present_mode",sync?"vsync":"immediate");presenter->OnSurfaceResizeFromUIThread();});
       publish();std::this_thread::sleep_for(600ms);
       const auto before=presenter->delivery_statistics();
       for(unsigned i=0;i<24;++i){
         publish();
         // Simulate late OS wakeups after this publication was already handled.
         for(unsigned wake=0;wake<4;++wake)ui([&]{window->RequestPaint();});
         std::this_thread::sleep_for(25ms);
       }
       std::this_thread::sleep_for(300ms);auto after=presenter->delivery_statistics();
       std::printf("DELIVERY sync=%u produced=24 submitted=%llu unique=%llu repeats=%llu\n",sync,
          (unsigned long long)(after.submissions-before.submissions),(unsigned long long)(after.new_game_images-before.new_game_images),
          (unsigned long long)(after.repeated_game_images-before.repeated_game_images));std::fflush(stdout);
       Check(after.new_game_images-before.new_game_images==24,"new publications lost");
       Check(after.submissions-before.submissions==24,"dormant drawer/stale wakeup redrew old game image");
       auto count=presenter->submitted_frames();
       ui([&]{presenter->RequestUIPaintFromUIThread();});
       wait_for([&]{return presenter->submitted_frames()>count;},"explicit UI invalidation lost");
       std::this_thread::sleep_for(200ms);Check(presenter->submitted_frames()==count+1,"single UI request caused extra draws");
       count=presenter->submitted_frames();
       ui([&]{drawer.remaining=3;presenter->RequestUIPaintFromUIThread();});
       wait_for([&]{return presenter->submitted_frames()>=count+4;},"UI animation stopped");
       std::this_thread::sleep_for(300ms);Check(presenter->submitted_frames()==count+4,"UI animation never stopped");
       count=presenter->submitted_frames();
       ui([&]{[cocoa setContentSize:NSMakeSize(sync?672:640,360)];});
       wait_for([&]{return presenter->submitted_frames()>count;},"real surface resize lost");
       std::this_thread::sleep_for(300ms);
       count=presenter->submitted_frames();
       (void)presenter->RefreshGuestOutput(0,0,0,0,[](auto&){return false;},{});
       wait_for([&]{return presenter->submitted_frames()>count;},"blank publication lost");
       std::this_thread::sleep_for(300ms);count=presenter->submitted_frames();
       for(unsigned i=0;i<5;++i)ui([&]{window->RequestPaint();});
       std::this_thread::sleep_for(300ms);Check(presenter->submitted_frames()==count,"blank output not acknowledged");
     }
     Check(ring.WaitIdle(error),error);std::puts("DELIVERY_PASS");
    } catch(const std::exception& e){failed=true;std::fprintf(stderr,"DELIVERY_ERROR %s\n",e.what());}
    app.RequestDeferredQuit();
   });
   auto result=app.RunMainMessageLoop();presenter->CancelFramePacingWaits();producer.join();
   presenter->RemoveUIDrawerFromUIThread(&drawer);window->SetPresenter(nullptr);presenter.reset();window->RequestClose();rex::FlushLogging();
   Check(!failed&&!lost&&result==0,"delivery fixture failed");return 0;
  } catch(const std::exception& e){std::fprintf(stderr,"DELIVERY_ERROR %s\n",e.what());return 1;}
 }
}
