#include <rex/ui/metal/presenter.h>

#import <QuartzCore/CAMetalDisplayLink.h>
#import <QuartzCore/CAMetalLayer.h>
#import <AppKit/AppKit.h>

#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>
#include <rex/logging.h>
#include <rex/cvar.h>
#include <span>
#include <rex/ui/surface_mac.h>
#include <rex/ui/window.h>

#include "context.h"
#include "frame_ring.h"
#include "display_timing.h"
#include "drawable_request.h"
#include <dispatch/dispatch.h>
#include <rex/diagnostics/policy.h>
#include "presentation_effects.h"
#include "presentation_image_cache.h"
#include "output_transfer.h"
#include <algorithm>
#include <cmath>
#include "guest_output_context.h"
#include "ui_draw_context.h"

REXCVAR_DEFINE_BOOL(gta4_metal_cache_presented_image, true, "GPU/Metal",
    "Reuse the processed game image during repeated UI presentations");
REXCVAR_DEFINE_BOOL(gta4_metal_fuse_hdr_output, true, "GTA IV/Metal",
                   "Combine compatible bilinear and HDR output without a full-window intermediate");


@interface LibertyMetalDisplayDelegate : NSObject <CAMetalDisplayLinkDelegate>
@property(nonatomic, copy) void (^update)(CAMetalDisplayLinkUpdate*);
@end
@implementation LibertyMetalDisplayDelegate
- (void)metalDisplayLink:(CAMetalDisplayLink*)link needsUpdate:(CAMetalDisplayLinkUpdate*)update {
  (void)link;
  if (_update) _update(update);
}
@end

namespace rex::ui::metal {
namespace {
bool RequestedHdr() {
  const auto mode = rex::cvar::GetFlagByName("gta4_native_hdr_mode");
  return mode == "scrgb" || mode == "auto_hdr";
}
float HdrValue(const char* name, float fallback, float low, float high) {
  if (!rex::cvar::GetFlagInfo(name)) return fallback;
  const double value = rex::cvar::Query<double>(name);
  return std::isfinite(value) ? std::clamp(float(value), low, high) : fallback;
}

bool RequestedVSync() {
  const auto mode = rex::cvar::GetFlagByName("gta4_present_mode");
  if (!mode.empty()) return mode != "immediate";
  const auto generic = rex::cvar::GetFlagByName("vsync");
  return generic.empty() || generic == "true" || generic == "1";
}
id<MTLRenderPipelineState> PresentPipeline(const MetalContext& context, MTLPixelFormat format,
                                          std::string& error) {
  auto descriptor = [[MTLRenderPipelineDescriptor alloc] init];
  descriptor.label = @"Liberty final composite";
  descriptor.vertexFunction = [context.ui_library newFunctionWithName:@"liberty_present_vertex"];
  descriptor.fragmentFunction = [context.ui_library newFunctionWithName:@"liberty_present_fragment"];
  descriptor.colorAttachments[0].pixelFormat = format;
  NSError* native_error = nil;
  auto pipeline = [context.device newRenderPipelineStateWithDescriptor:descriptor error:&native_error];
  if (!pipeline) error = MetalError(native_error, "Presentation pipeline creation failed.");
  return pipeline;
}
}  // namespace

struct MetalPresenter::State {
  explicit State(std::shared_ptr<MetalContext> c) : context(std::move(c)) {}
  struct Output { id<MTLTexture> texture = nil; uint64_t version = 0,ready_value=0,read_value=0; std::shared_ptr<MetalGeneratedFrame> pair; };
  std::shared_ptr<MetalContext> context;
  id<MTLCommandQueue> display_queue=nil;
  id<MTLEvent> render_ready=nil,display_done=nil;
  uint64_t next_ready_value=0,next_read_value=0;
  FrameRing frames;
  struct CallbackOwner {
    MetalPresenter* owner = nullptr;  // Read/written only by the main run loop.
    std::atomic<bool> awaiting_frame_slot{false};
  };
  std::shared_ptr<CallbackOwner> callbacks = std::make_shared<CallbackOwner>();
  std::shared_ptr<DrawableRequest<id<CAMetalDrawable>>> drawable_request =
      std::make_shared<DrawableRequest<id<CAMetalDrawable>>>();
  dispatch_queue_t acquisition_queue = dispatch_queue_create(
      "Liberty drawable acquisition", DISPATCH_QUEUE_SERIAL);
  uint64_t callback_host_ns = 0;
  static void Wake(std::shared_ptr<CallbackOwner> callbacks) {
    const bool trace = rex::diagnostics::IsEnabled(rex::diagnostics::Category::kPresenter);
    const uint64_t requested_ns = trace ? FramePacerNowNs() : 0;
    dispatch_async(dispatch_get_main_queue(), ^{
      if (trace) REXLOG_INFO("FramePacer metal=wakeup posted-host-ns={} delivered-host-ns={}",
                            requested_ns, FramePacerNowNs());
      // The worker captures no presenter, window, or surface pointer.
      auto* owner = callbacks->owner;
      if (owner && owner->connected_window()) owner->connected_window()->RequestPaint();
    });
  }
  void RequestDrawable() {
    auto ticket = drawable_request->Begin();
    if (!ticket) return;
    auto request = drawable_request;
    auto lifetime = callbacks;
    CAMetalLayer* retained_layer = layer;
    const bool trace = rex::diagnostics::IsEnabled(rex::diagnostics::Category::kPresenter);
    const uint64_t requested_ns = trace ? FramePacerNowNs() : 0;
    dispatch_async(acquisition_queue, ^{
      @autoreleasepool {
        const uint64_t begin_ns = trace ? FramePacerNowNs() : 0;
        auto result = [retained_layer nextDrawable];
        if (trace) REXLOG_INFO("FramePacer metal=drawable request={} epoch={} requested-host-ns={} begin-host-ns={} end-host-ns={} ready={}",
            ticket.id, ticket.epoch, requested_ns, begin_ns, FramePacerNowNs(), result != nil);
        request->Complete(ticket, result);
        // Stale completions also wake deferred reconnects. Teardown nulls owner.
        Wake(lifetime);
      }
    });
  }
  CAMetalLayer* layer = nil;
  CAMetalDisplayLink* display_link = nil;
  LibertyMetalDisplayDelegate* delegate = nil;
  id<CAMetalDrawable> drawable = nil;
  id<MTLRenderPipelineState> present_pipeline = nil, capture_pipeline = nil;
  id<MTLSamplerState> sampler = nil;
  std::array<Output, kGuestOutputMailboxSize> outputs{};
  std::atomic<uint64_t> submitted_frames{0}, new_game_images{0}, repeated_game_images{0};
  uint64_t last_submitted_publication = 0;
  std::shared_ptr<MetalGeneratedFrame> pending_pair;
  GuestOutputProperties pending_properties{};
  GuestOutputPaintConfig pending_configuration{};
  uint64_t pending_surface_epoch=0,last_generated_publication=0;
  std::atomic<uint64_t> generated_images{0};
  uint64_t next_output_version = 0;
  uint64_t last_extent_trace_frame = UINT64_MAX;
  PresentationClockMapping display_clock;
  DisplayLinkActivity display_activity;
  SoftwarePaintDeadline software_deadline;
  dispatch_source_t paint_timer = nil;
  void ArmPaintTimer() {
    const uint64_t now = FramePacerNowNs();
    const uint64_t due = software_deadline.due();
    const int64_t delay = due > now ? int64_t(due - now) : 1;
    dispatch_source_set_timer(paint_timer, dispatch_time(DISPATCH_TIME_NOW, delay),
                              DISPATCH_TIME_FOREVER, 0);
  }
  void CancelPaintTimer() {
    software_deadline.Reset();
    if (paint_timer) dispatch_source_set_timer(paint_timer, DISPATCH_TIME_FOREVER,
                                               DISPATCH_TIME_FOREVER, 0);
  }
  std::shared_ptr<DisplayFeedbackInbox> feedback = std::make_shared<DisplayFeedbackInbox>();
  uint64_t surface_epoch = 0, display_target_ns = 0, display_deadline_ns = 0;
  uint64_t last_actual_ns = 0, last_feedback_generation = 0;
  uint32_t requested_rate = UINT32_MAX, screen_rate = 0;
  bool vsync = true;
  bool hdr = false;
  OutputTransfer hdr_transfer;
  PresentationImageCache game_image_cache;
  uint64_t cache_direct_frames = 0, cache_populated_frames = 0, cache_reused_frames = 0;
  id<MTLTexture> hdr_source = nil;
  PresentationEffects effects;
  std::array<id<MTLTexture>, 2> intermediates{};
};

MetalPresenter::MetalPresenter(std::shared_ptr<MetalContext> context, HostGpuLossCallback callback)
    : Presenter(std::move(callback)), state_(std::make_unique<State>(std::move(context))) {
  state_->callbacks->owner = this;
}
std::unique_ptr<MetalPresenter> MetalPresenter::Create(std::shared_ptr<MetalContext> context,
                                                      HostGpuLossCallback callback) {
  @autoreleasepool {
    if (!context || !context->device || !context->queue) return {};
    auto result = std::unique_ptr<MetalPresenter>(new MetalPresenter(std::move(context),std::move(callback)));
    result->state_->display_queue=result->state_->context->queue;
    if(rex::cvar::Query<bool>("gta4_metalfx_frame_generation")){
      result->state_->display_queue=[result->state_->context->device newCommandQueue];
      result->state_->render_ready=[result->state_->context->device newEvent];
      result->state_->display_done=[result->state_->context->device newEvent];
      if(!result->state_->display_queue||!result->state_->render_ready||!result->state_->display_done)return {};
      result->state_->display_queue.label=@"Liberty interpolated presentation";
    }
    std::string error;
    result->state_->present_pipeline = PresentPipeline(*result->state_->context,kPresentationFormat,error);
    result->state_->capture_pipeline = PresentPipeline(*result->state_->context,MTLPixelFormatRGBA8Unorm,error);
    if (!result->state_->present_pipeline || !result->state_->capture_pipeline) {
      REXLOG_ERROR("gta4-metal: {}",error); return {};
    }
    auto sampler = [[MTLSamplerDescriptor alloc] init];
    sampler.minFilter = sampler.magFilter = MTLSamplerMinMagFilterLinear;
    sampler.sAddressMode = sampler.tAddressMode = MTLSamplerAddressModeClampToEdge;
    result->state_->sampler = [result->state_->context->device newSamplerStateWithDescriptor:sampler];
    if (!result->state_->sampler || !result->state_->effects.Initialize(result->state_->context, error) ||
        !result->state_->hdr_transfer.Initialize(result->state_->context, error) ||
        !result->state_->game_image_cache.Initialize(result->state_->context, error) ||
        !result->InitializeCommonSurfaceIndependent()) {
      REXLOG_ERROR("gta4-metal: presentation initialization: {}", error); return {};
    }
    return result;
  }
}
MetalPresenter::~MetalPresenter() {
  state_->callbacks->owner = nullptr;
  state_->CancelPaintTimer();
  if (state_->paint_timer) { dispatch_source_cancel(state_->paint_timer); state_->paint_timer = nil; }
  state_->drawable_request->Reset(true);
  CancelFramePacingWaits();
  if (connected_window()) connected_window()->SetPresenter(nullptr);
  DisconnectPaintingFromSurfaceFromUIThreadImpl();
  std::string error;
  if (!state_->frames.WaitIdle(error)) REXLOG_ERROR("gta4-metal: shutdown: {}",error);
}
Surface::TypeFlags MetalPresenter::GetSupportedSurfaceTypes() const { return Surface::kTypeFlag_CAMetalLayer; }
uint64_t MetalPresenter::submitted_frames() const { return state_->submitted_frames.load(std::memory_order_relaxed); }

std::optional<uint64_t> MetalPresenter::DisplayLinkTargetNs() const {
  if (!state_->vsync) return std::nullopt;
  return state_->drawable ? state_->display_target_ns : 0;
}

bool MetalPresenter::ScheduleFramePacingWakeup(uint64_t delay_ns) {
  if (![NSThread isMainThread] || !state_->layer) return false;
  if (state_->vsync && state_->display_link) {
    // A pending UI animation needs the next display opportunity, not a second
    // one-millisecond timer stream interleaved with display callbacks.
    state_->display_activity.Demand(FramePacerNowNs());
    state_->display_link.paused = NO;
    return true;
  }
  if (!state_->paint_timer) {
    auto timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0,
        DISPATCH_TIMER_STRICT, dispatch_get_main_queue());
    if (!timer) return false;
    const auto lifetime = state_->callbacks;
    dispatch_source_set_event_handler(timer, ^{
      auto* owner = lifetime->owner;
      if (!owner) return;
      auto& state = *owner->state_;
      const uint64_t now = FramePacerNowNs();
      const uint64_t due = state.software_deadline.due();
      if (!due) return;
      if (!state.software_deadline.Consume(now)) { state.ArmPaintTimer(); return; }
      if (rex::diagnostics::IsEnabled(rex::diagnostics::Category::kPresenter))
        REXLOG_INFO("FramePacer timer=metal-fired due-host-ns={} actual-host-ns={}", due, now);
      if (owner->connected_window()) owner->connected_window()->RequestPaint();
    });
    state_->paint_timer = timer;
    dispatch_resume(timer);
  }
  const uint64_t now = FramePacerNowNs();
  if (state_->software_deadline.Request(now, delay_ns)) {
    state_->ArmPaintTimer();
    if (rex::diagnostics::IsEnabled(rex::diagnostics::Category::kPresenter))
      REXLOG_INFO("FramePacer timer=metal-request delay-ns={} due-host-ns={}",
                  delay_ns, state_->software_deadline.due());
  }
  return true;
}

void MetalPresenter::PollPresentationTiming() {
  // CACurrentMediaTime is the Metal presentation clock; steady_clock is not
  // assumed to have the same epoch. Bound the uncertainty of each correlation.
  const uint64_t before = FramePacerNowNs();
  const uint64_t metal_now = MetalTimeNanoseconds(CACurrentMediaTime());
  const uint64_t now = FramePacerNowNs();
  state_->display_clock.Sample(before, metal_now, now);
  const auto feedback = state_->feedback->Drain();
  if (feedback.overwritten && rex::diagnostics::IsEnabled(rex::diagnostics::Category::kPresenter))
    REXLOG_WARN("FramePacer metal=feedback-overflow lost={}", feedback.overwritten);
  for (size_t i = 0; i < feedback.count; ++i) {
    const auto& value = feedback.values[i];
    if (value.stage != DisplayFeedback::Stage::kPresented) {
      if (value.surface_epoch == state_->surface_epoch &&
          value.generation == frame_pacer().generation() &&
          rex::diagnostics::IsEnabled(rex::diagnostics::Category::kPresenter)) {
        REXLOG_INFO("FramePacer metal={} epoch={} serial={} frame={} fps={} queue-host-ns={} "
                    "event-host-ns={} deadline-host-ns={} gpu-start-ns={} gpu-end-ns={} error={} presentation={}",
                    value.stage == DisplayFeedback::Stage::kScheduled ? "scheduled" : "completed",
                    value.surface_epoch, value.publication, value.frame, value.fps, value.queue_host_ns,
                    value.event_host_ns, value.deadline_host_ns, value.gpu_start_ns, value.gpu_end_ns,
                    value.gpu_error, value.presentation_id);
      }
      continue;
    }
    if (!value.actual_host_ns && IsCurrentDisplayReceipt(value, state_->surface_epoch,
        frame_pacer().generation(), frame_pacer().fps(), now)) {
      if (rex::diagnostics::IsEnabled(rex::diagnostics::Category::kPresenter))
        REXLOG_INFO("FramePacer metal=not-displayed epoch={} serial={} frame={} presentation={} fps={}",
            value.surface_epoch, value.publication, value.frame, value.presentation_id, value.fps);
      continue;
    }
    if (!IsCurrentDisplayFeedback(value, state_->surface_epoch, frame_pacer().generation(),
                                  frame_pacer().fps(), now)) continue;
    const bool same_generation = value.generation == state_->last_feedback_generation;
    if (same_generation && state_->last_actual_ns && value.actual_host_ns <= state_->last_actual_ns) continue;
    const uint64_t interval = same_generation && state_->last_actual_ns
        ? value.actual_host_ns - state_->last_actual_ns : 0;
    if(rex::diagnostics::IsEnabled(rex::diagnostics::Category::kPresenter))
      REXLOG_INFO("gta4-metal-displayed kind={} publication={} frame={} actual-ns={} interval-ns={} presentation={}",
          value.generated?"generated":"real",value.publication,value.frame,value.actual_host_ns,interval,value.presentation_id);
    state_->last_actual_ns = value.actual_host_ns;
    state_->last_feedback_generation = value.generation;
    // The display link's predictions already supply phase for the shared pacer.
    // Feedback measures actual display, without inventing another phase loop.
    if (rex::diagnostics::IsEnabled(rex::diagnostics::Category::kPresenter))
      REXLOG_INFO("FramePacer metal=feedback epoch={} serial={} frame={} fps={} target-host-ns={} "
                  "actual-host-ns={} interval-ns={} queue-host-ns={} clock-error-ns={} presentation={}",
                  value.surface_epoch, value.publication, value.frame, value.fps, value.target_host_ns,
                  value.actual_host_ns, interval, value.queue_host_ns, state_->display_clock.uncertainty(),
                  value.presentation_id);
  }
  if (state_->display_link) {
    // Explicit UI/publication requests wake an idle link. They do not render
    // against a fabricated display timestamp between callbacks.
    state_->display_activity.Demand(now);
    state_->display_link.paused = NO;
    NSWindow* window = connected_window() ? (__bridge NSWindow*)connected_window()->GetNativeWindowHandle() : nil;
    const uint32_t maximum = uint32_t(std::max(1L, long(window.screen.maximumFramesPerSecond)));
    const uint32_t requested = frame_pacer().fps();
    if (state_->requested_rate != requested || state_->screen_rate != maximum) {
      // Request opportunities, not a second cap. A 40 Hz preference may be
      // rounded down on a fixed 60 Hz display. The rational pacer chooses actual
      // opportunities, including mixed refresh intervals for non-divisor caps.
      const float preferred = float(maximum);
      state_->display_link.preferredFrameRateRange = CAFrameRateRangeMake(preferred, preferred, preferred);
      state_->requested_rate = requested;
      state_->screen_rate = maximum;
    }
  }
}

Presenter::SurfacePaintConnectResult MetalPresenter::ConnectOrReconnectPaintingToSurfaceFromUIThread(
    Surface& surface, uint32_t width, uint32_t height, bool was_paintable, bool& implicit_vsync) {
  @autoreleasepool {
    const bool sync = RequestedVSync();
    const bool hdr = RequestedHdr();
    implicit_vsync = sync;
    if (![NSThread isMainThread] || surface.GetType() != Surface::kTypeIndex_CAMetalLayer || !width || !height) {
      DisconnectPaintingFromSurfaceFromUIThreadImpl();
      return SurfacePaintConnectResult::kFailureSurfaceUnusable;
    }
    CAMetalLayer* layer = (__bridge CAMetalLayer*)static_cast<CAMetalLayerSurface&>(surface).layer();
    if (!layer) { DisconnectPaintingFromSurfaceFromUIThreadImpl(); return SurfacePaintConnectResult::kFailure; }
    if (state_->layer == layer && state_->vsync == sync && state_->hdr == hdr && (!sync || state_->display_link)) {
      const bool same = layer.drawableSize.width == width && layer.drawableSize.height == height;
      if (!same) {
        if (state_->drawable_request->pending()) {
          DisconnectPaintingFromSurfaceFromUIThreadImpl();
          return SurfacePaintConnectResult::kFailure;
        }
        ++state_->surface_epoch;
        frame_pacer().Reset();
        state_->drawable_request->Reset();
        state_->drawable = nil;
        state_->display_target_ns = state_->display_deadline_ns = state_->last_actual_ns = 0;
      }
      layer.drawableSize = CGSizeMake(width,height);
      return was_paintable && same ? SurfacePaintConnectResult::kSuccessUnchanged : SurfacePaintConnectResult::kSuccess;
    }
    DisconnectPaintingFromSurfaceFromUIThreadImpl();
    // Do not mutate a layer or start its display link during an old acquisition.
    // Its completion posts one UI wake to retry the connection.
    if (state_->drawable_request->pending()) return SurfacePaintConnectResult::kFailure;
    frame_pacer().Reset();
    state_->layer = layer;
    state_->vsync = sync;
    state_->hdr = hdr;
    layer.displaySyncEnabled = sync;
    layer.allowsNextDrawableTimeout = YES;
    layer.device = state_->context->device;
    layer.pixelFormat = hdr ? MTLPixelFormatRGBA16Float : kPresentationFormat;
    layer.wantsExtendedDynamicRangeContent = hdr;
    layer.framebufferOnly = YES;
    layer.opaque = YES;
    layer.presentsWithTransaction = NO;
    layer.drawableSize = CGSizeMake(width,height);
    auto color_space = CGColorSpaceCreateWithName(hdr ? kCGColorSpaceExtendedLinearSRGB : kCGColorSpaceSRGB);
    layer.colorspace = color_space;
    CGColorSpaceRelease(color_space);
    // Immediate mode is publication-driven, not display-link-rate limited.
    if (!sync) return SurfacePaintConnectResult::kSuccess;
    state_->delegate = [[LibertyMetalDisplayDelegate alloc] init];
    // The delegate and presenter live on the UI thread. Invalidate and clear
    // this block before destroying the presenter or replacing the surface.
    const auto lifetime = state_->callbacks;
    state_->delegate.update = ^(CAMetalDisplayLinkUpdate* update) {
      @autoreleasepool {
        auto* owner = lifetime->owner;
        if (!owner) return;
        auto& state = *owner->state_;
        const uint64_t epoch = state.surface_epoch;
        const uint64_t before = FramePacerNowNs();
        const uint64_t metal_now = MetalTimeNanoseconds(CACurrentMediaTime());
        state.display_clock.Sample(before, metal_now, FramePacerNowNs());
        state.callback_host_ns = before;
        state.display_target_ns = state.display_clock.ToHost(MetalTimeNanoseconds(update.targetPresentationTimestamp));
        state.display_deadline_ns = state.display_clock.ToHost(MetalTimeNanoseconds(update.targetTimestamp));
        const bool usable = IsUsableDisplayOpportunity(FramePacerNowNs(), state.display_target_ns,
                                                       state.display_deadline_ns);
        state.drawable = usable ? update.drawable : nil;
        owner->PaintFromDisplayLink();
        // A GPU-loss callback can destroy the presenter in PaintFromDisplayLink.
        if (lifetime->owner != owner || state.surface_epoch != epoch) return;
        state.drawable = nil;
        state.display_target_ns = state.display_deadline_ns = 0;
        if (state.display_activity.ShouldPause(FramePacerNowNs(), owner->HasPendingPaintFromUIThread()))
          state.display_link.paused = YES;
      }
    };
    state_->display_link = [[CAMetalDisplayLink alloc] initWithMetalLayer:layer];
    if (!state_->display_link) { DisconnectPaintingFromSurfaceFromUIThreadImpl(); return SurfacePaintConnectResult::kFailure; }
    state_->display_link.preferredFrameLatency = 2.0f;
    state_->display_link.delegate = state_->delegate;
    [state_->display_link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    return SurfacePaintConnectResult::kSuccess;
  }
}
void MetalPresenter::DisconnectPaintingFromSurfaceFromUIThreadImpl() {
  state_->CancelPaintTimer();
  state_->drawable_request->Reset();
  state_->callbacks->awaiting_frame_slot.store(false, std::memory_order_release);
  [state_->display_link invalidate];
  state_->display_link.delegate = nil;
  state_->delegate.update = nil;
  state_->display_link = nil; state_->delegate = nil;
  state_->drawable = nil; state_->layer = nil;
  state_->hdr_source = nil; state_->intermediates = {};
  state_->pending_pair.reset();
  frame_pacer().SetMinimumInterval(0);
  state_->game_image_cache.Reset();
  ++state_->surface_epoch;
  state_->display_clock = {};
  state_->display_target_ns = state_->display_deadline_ns = state_->last_actual_ns = 0;
  state_->requested_rate = UINT32_MAX;
}

bool MetalPresenter::RefreshGuestOutputImpl(uint32_t index, uint32_t width, uint32_t height,
    std::function<bool(GuestOutputRefreshContext&)> refresher, bool& is_8bpc) {
  @autoreleasepool {
    if (index >= state_->outputs.size() || !width || !height || width > 16384 || height > 16384 || !refresher) return false;
    auto& output = state_->outputs[index];
    if (!output.texture || output.texture.width != width || output.texture.height != height) {
      auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
          width:width height:height mipmapped:NO];
      descriptor.storageMode = MTLStorageModePrivate;
      descriptor.hazardTrackingMode = MTLHazardTrackingModeTracked;
      descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
      const auto aa=rex::cvar::GetFlagByName("gta4_native_anti_aliasing");
      if(aa=="taa"||aa=="metalfx_taa"||rex::cvar::GetFlagByName("gta4_native_upscaler")=="metalfx")
        descriptor.usage|=MTLTextureUsageShaderWrite;
      auto texture = [state_->context->device newTextureWithDescriptor:descriptor];
      if (!texture) { REXLOG_ERROR("gta4-metal: guest output allocation failed ({}x{})",width,height); return false; }
      texture.label = @"Liberty guest output";
      output.texture = texture;
      output.version = ++state_->next_output_version;
    }
    output.pair.reset();
    output.ready_value=++state_->next_ready_value;
    MetalGuestOutputRefreshContext context(is_8bpc,output.texture,output.version,&output.pair,
        state_->render_ready,output.ready_value,state_->display_done,output.read_value);
    const bool updated=refresher(context);
    if(updated&&!context.synchronization_encoded()){
      REXLOG_ERROR("gta4-metal: cross-queue mailbox writer omitted its synchronization");return false;
    }
    return updated;
  }
}

Presenter::PaintResult MetalPresenter::PaintAndPresentImpl(bool execute_ui_drawers) {
  @autoreleasepool {
    if (!state_->layer || state_->vsync != RequestedVSync() || state_->hdr != RequestedHdr())
      return PaintResult::kNotPresentedConnectionOutdated;
    if (state_->vsync && !state_->drawable) return PaintResult::kNotPresented;
    std::string error;
    auto* frame = state_->frames.TryBegin(error);
    if (!error.empty()) { REXLOG_ERROR("gta4-metal: {}",error); return PaintResult::kGpuLostResponsible; }
    if (!frame && !state_->vsync) {
      // Close the completion-before-registration race with a second readiness
      // check. Completion wakes the main run loop; no 1 ms resource polling.
      state_->callbacks->awaiting_frame_slot.store(true, std::memory_order_release);
      frame = state_->frames.TryBegin(error);
      if (!error.empty()) { REXLOG_ERROR("gta4-metal: {}",error); return PaintResult::kGpuLostResponsible; }
    }
    if (!frame) return PaintResult::kNotPresented;
    state_->callbacks->awaiting_frame_slot.store(false, std::memory_order_release);
    id<CAMetalDrawable> drawable = state_->vsync ? state_->drawable : state_->drawable_request->Take();
    if (!drawable) {
      state_->frames.Cancel(*frame);
      if (!state_->vsync) state_->RequestDrawable();
      return PaintResult::kNotPresented;
    }
    const auto cancel = [&]() { state_->frames.Cancel(*frame); return PaintResult::kNotPresentedRetry; };
    auto commands = [state_->display_queue commandBuffer];
    if (!commands) return cancel();
    commands.label = @"Liberty display and UI";
    uint32_t index;
    GuestOutputProperties properties;
    GuestOutputPaintConfig configuration;
    auto output_lock = ConsumeGuestOutput(index,&properties,&configuration);
    commands.label = [NSString stringWithFormat:@"Liberty display and UI game=%u serial=%llu",
        properties.provenance.submitted_frame, properties.provenance.publication_serial];
    const uint32_t width = uint32_t(state_->layer.drawableSize.width);
    const uint32_t height = uint32_t(state_->layer.drawableSize.height);
    GuestOutputPaintFlow flow{};
    id<MTLTexture> source = nil;
    if (index < state_->outputs.size()) source = state_->outputs[index].texture;
    bool generated=false,finishing_pair=false;
    std::shared_ptr<MetalGeneratedFrame> selected_pair;
    if(state_->pending_pair&&state_->pending_surface_epoch==state_->surface_epoch){
      selected_pair=state_->pending_pair;source=selected_pair->real;
      properties=state_->pending_properties;configuration=state_->pending_configuration;finishing_pair=true;
    }else if(state_->vsync&&index<state_->outputs.size()&&state_->outputs[index].pair&&
             properties.provenance.publication_serial>state_->last_generated_publication){
      selected_pair=state_->outputs[index].pair;source=selected_pair->generated;generated=true;
    }
    commands.label=[NSString stringWithFormat:@"Liberty %@ frame=%u serial=%llu",generated?@"MetalFX generated":@"real display",properties.provenance.submitted_frame,properties.provenance.publication_serial];
    const uint64_t ready_value=selected_pair?selected_pair->ready_value:
        index<state_->outputs.size()?state_->outputs[index].ready_value:0;
    if(state_->render_ready&&ready_value)[commands encodeWaitForEvent:state_->render_ready value:ready_value];
    const bool reads_mailbox=!selected_pair&&index<state_->outputs.size()&&source;
    const uint64_t read_value=state_->display_done&&reads_mailbox?++state_->next_read_value:0;
    frame_pacer().SetMinimumInterval(selected_pair?selected_pair->interval_ns:0);
    if(selected_pair){
      auto held=std::make_shared<std::shared_ptr<MetalGeneratedFrame>>(selected_pair);
      [commands addCompletedHandler:^(id<MTLCommandBuffer>){held->reset();}];
    }
    configuration.SetDither(rex::cvar::Query<bool>("gta4_native_output_dither") && !state_->hdr);
    if (source) flow = GetGuestOutputPaintFlow(properties,width,height,16384,16384,configuration);
    if (drawable.texture.width != width || drawable.texture.height != height) {
      state_->frames.Cancel(*frame); return PaintResult::kNotPresentedConnectionOutdated;
    }
    OutputTransferConstants hdr{};
    if (state_->hdr) {
      hdr.hdr_mode = rex::cvar::GetFlagByName("gta4_native_hdr_mode") == "auto_hdr" ? 2 : 1;
      NSWindow* window = connected_window() ? (__bridge NSWindow*)connected_window()->GetNativeWindowHandle() : nil;
      const double headroom = window.screen.maximumExtendedDynamicRangeColorComponentValue;
      hdr.hdr_headroom = std::isfinite(headroom) ? float(std::max(1.0, headroom)) : 1.0f;
      hdr.paper_white_nits = HdrValue("gta4_native_hdr_paper_white_nits", 203, 80, 500);
      hdr.peak_nits = HdrValue("gta4_native_hdr_peak_nits", 400, 80, 2000);
      hdr.shoulder_start = HdrValue("gta4_native_auto_hdr_shoulder_start", 0, 0, 1);
      hdr.shoulder_power = HdrValue("gta4_native_auto_hdr_shoulder_power", 2.5f, 1, 10);
    }
    PresentationImageKey image_key{};
    image_key.publication = properties.provenance.publication_serial;
    image_key.surface_epoch = state_->surface_epoch;
    image_key.texture_version = index < state_->outputs.size() ? state_->outputs[index].version : 0;
    image_key.source = reinterpret_cast<uintptr_t>((__bridge void*)source);
    image_key.source_format = source.pixelFormat;
    image_key.output_format = drawable.texture.pixelFormat;
    image_key.source_width = uint32_t(source.width);
    image_key.source_height = uint32_t(source.height);
    image_key.output_width = width; image_key.output_height = height;
    image_key.frontbuffer_width = properties.frontbuffer_width;
    image_key.frontbuffer_height = properties.frontbuffer_height;
    image_key.aspect_x = properties.display_aspect_ratio_x;
    image_key.aspect_y = properties.display_aspect_ratio_y;
    image_key.is_8bpc = properties.is_8bpc;
    image_key.active = flow.effect_count != 0;
    image_key.SetFlow(flow);
    image_key.effect = uint32_t(configuration.GetEffect());
    image_key.overscan = configuration.GetAllowOverscanCutoff();
    image_key.dither = configuration.GetDither();
#if defined(REX_HAS_FIDELITYFX_FSR1)
    image_key.cas_sharpness = configuration.GetCasAdditionalSharpness();
    image_key.fsr_sharpness = configuration.GetFsrSharpnessReduction();
    image_key.fsr_passes = configuration.GetFsrMaxUpsamplingPasses();
    image_key.fsr_quality = uint32_t(configuration.GetFsrQualityMode());
#endif
    image_key.hdr_mode = hdr.hdr_mode; image_key.output_mode = hdr.output_mode;
    image_key.output_mode|=rex::cvar::Query<bool>("gta4_metal_fuse_hdr_output")?0x100u:0u;
    image_key.hdr = {hdr.hdr_headroom, hdr.paper_white_nits, hdr.peak_nits,
                     hdr.shoulder_start, hdr.shoulder_power};
    const bool cache_enabled = rex::cvar::Query<bool>("gta4_metal_cache_presented_image");
    if (!cache_enabled) state_->game_image_cache.policy.Reset();
    auto cache_action = cache_enabled ? state_->game_image_cache.policy.Inspect(image_key)
                                      : PresentationCachePolicy::Action::kDirect;
    auto game_target = drawable.texture;
    if (cache_action == PresentationCachePolicy::Action::kPopulate) {
      auto cached = state_->game_image_cache.Prepare(width, height, game_target.pixelFormat, error);
      if (cached) game_target = cached;
      else { cache_action = PresentationCachePolicy::Action::kDirect; error.clear(); }
    }
    if (source && rex::diagnostics::IsEnabled(rex::diagnostics::Category::kNativeTrace) &&
        properties.provenance.submitted_frame != state_->last_extent_trace_frame &&
        (properties.provenance.submitted_frame <= 3 || properties.provenance.submitted_frame % 120 == 0)) {
      state_->last_extent_trace_frame = properties.provenance.submitted_frame;
      REXLOG_INFO("gta4-metal-resolution point=display frame={} publication={} mailbox={}x{} drawable={}x{} effects={} hdr={} vsync={}",
          properties.provenance.submitted_frame, properties.provenance.publication_serial, source.width, source.height,
          width, height, flow.effect_count, state_->hdr, state_->vsync);
      for (size_t effect = 0; effect < flow.effect_count; ++effect) {
        int32_t x, y; flow.GetEffectOutputOffset(effect, x, y);
        REXLOG_INFO("gta4-metal-resolution point=effect frame={} index={} kind={} output={}x{} origin={},{}",
            properties.provenance.submitted_frame, effect, uint32_t(flow.effects[effect]),
            flow.effect_output_sizes[effect].first, flow.effect_output_sizes[effect].second, x, y);
      }
    }
    static_assert(size_t(GuestOutputPaintEffect::kCount) == size_t(PresentationEffect::kCount));
    auto draw_effect = [&](size_t i, id<MTLRenderCommandEncoder> encoder,
                           id<MTLTexture> input, id<MTLTexture> target) {
      int32_t x, y; flow.GetEffectOutputOffset(i,x,y);
      const auto extent = flow.effect_output_sizes[i];
      const MTLViewport viewport{double(x),double(y),double(extent.first),double(extent.second),0,1};
      const auto draw = [&](const auto& constants) {
        return state_->effects.Draw(encoder,PresentationEffect(flow.effects[i]),input,target,viewport,
            std::as_bytes(std::span(&constants,1)),error);
      };
      switch (flow.effects[i]) {
        case GuestOutputPaintEffect::kBilinear:
        case GuestOutputPaintEffect::kBilinearDither: {
          BilinearConstants constants{}; constants.Initialize(flow,i); return draw(constants);
        }
        case GuestOutputPaintEffect::kCasSharpen:
        case GuestOutputPaintEffect::kCasSharpenDither: {
          CasSharpenConstants constants{}; constants.Initialize(flow,i,configuration); return draw(constants);
        }
        case GuestOutputPaintEffect::kCasResample:
        case GuestOutputPaintEffect::kCasResampleDither: {
          CasResampleConstants constants{}; constants.Initialize(flow,i,configuration); return draw(constants);
        }
        case GuestOutputPaintEffect::kFsrEasu: {
          FsrEasuConstants constants{}; constants.Initialize(flow,i); return draw(constants);
        }
        case GuestOutputPaintEffect::kFsrRcas:
        case GuestOutputPaintEffect::kFsrRcasDither: {
          FsrRcasConstants constants{}; constants.Initialize(flow,i,configuration); return draw(constants);
        }
        default: error = "Unknown Metal presentation effect"; return false;
      }
    };
    id<MTLRenderCommandEncoder> encoder = nil;
    if (cache_action != PresentationCachePolicy::Action::kReuse) {
      // Two reusable tracked textures suffice for the serial effect chain.
      // Neither texture's contents are touched by the CPU or discarded between dependent passes.
      for (size_t i=0; i+1<flow.effect_count; ++i) {
        const auto extent = flow.effect_output_sizes[i];
        auto& target = state_->intermediates[i % state_->intermediates.size()];
        if (!target || target.width != extent.first || target.height != extent.second) {
          auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
              width:extent.first height:extent.second mipmapped:NO];
          descriptor.storageMode = MTLStorageModePrivate;
          descriptor.hazardTrackingMode = MTLHazardTrackingModeTracked;
          descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
          auto replacement = [state_->context->device newTextureWithDescriptor:descriptor];
          if (!replacement) return cancel();
          replacement.label = @"Liberty presentation intermediate"; target = replacement;
        }
        auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = target;
        pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        auto encoder = [commands renderCommandEncoderWithDescriptor:pass];
        if (!encoder) return cancel();
        encoder.label = @"Liberty presentation effect";
        const bool ok = draw_effect(i,encoder,source,target);
        [encoder endEncoding];
        if (!ok) { REXLOG_ERROR("gta4-metal: {}",error); return cancel(); }
        source = target;
      }
      int32_t last_x = 0, last_y = 0;
      if (flow.effect_count) flow.GetEffectOutputOffset(flow.effect_count - 1, last_x, last_y);
      // Preserve the frontend rectangle, bilinear sampling and former FP16
      // rounding, but perform the final HDR conversion in the same draw.
      // CAS/FSR, SDR dither and the original safe fallback remain unchanged.
      const bool identity_hdr=state_->hdr && source && flow.effect_count==1 &&
          flow.effects[0]==GuestOutputPaintEffect::kBilinear && last_x==0 && last_y==0 &&
          flow.effect_output_sizes[0].first==width && flow.effect_output_sizes[0].second==height &&
          source.width==width && source.height==height && source.pixelFormat==MTLPixelFormatRGBA16Float;
      const bool fused_hdr = !identity_hdr && state_->hdr && source && flow.effect_count == 1 &&
          flow.effects[0] == GuestOutputPaintEffect::kBilinear &&
          source.pixelFormat == MTLPixelFormatRGBA16Float &&
          rex::cvar::Query<bool>("gta4_metal_fuse_hdr_output");
      const bool direct_hdr=fused_hdr||identity_hdr;
      if (direct_hdr) state_->hdr_source = nil;
      // Upscale in the title's perceptual space; tone-map only the completed
      // image. One reusable full-window scratch also preserves letterbox geometry.
      if (state_->hdr && flow.effect_count && !direct_hdr && (!state_->hdr_source ||
          state_->hdr_source.width != width || state_->hdr_source.height != height)) {
        if (!width || !height || uint64_t(width) * height * 8 > 512ull * 1024 * 1024) return cancel();
        auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
            width:width height:height mipmapped:NO];
        descriptor.storageMode = MTLStorageModePrivate;
        descriptor.hazardTrackingMode = MTLHazardTrackingModeTracked;
        descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        auto replacement = [state_->context->device newTextureWithDescriptor:descriptor];
        if (!replacement) return cancel();
        replacement.label = @"Liberty pre-EDR output";
        state_->hdr_source = replacement;
      }
      auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
      auto final_target = state_->hdr && flow.effect_count && !direct_hdr ? state_->hdr_source : game_target;
      pass.colorAttachments[0].texture = final_target;
      pass.colorAttachments[0].loadAction = MTLLoadActionClear;
      pass.colorAttachments[0].storeAction = MTLStoreActionStore;
      pass.colorAttachments[0].clearColor = MTLClearColorMake(0,0,0,1);
      encoder = [commands renderCommandEncoderWithDescriptor:pass];
      if (!encoder) return cancel();
      encoder.label = @"Liberty final composite";
      if (flow.effect_count && !direct_hdr && !draw_effect(flow.effect_count-1,encoder,source,final_target)) {
        [encoder endEncoding]; REXLOG_ERROR("gta4-metal: {}",error); return cancel();
      }
      if (state_->hdr && flow.effect_count) {
        if (!direct_hdr) {
          [encoder endEncoding];
          auto hdr_pass = [MTLRenderPassDescriptor renderPassDescriptor];
          hdr_pass.colorAttachments[0].texture = game_target;
          hdr_pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
          hdr_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
          encoder = [commands renderCommandEncoderWithDescriptor:hdr_pass];
          if (!encoder) return cancel();
        }
        encoder.label = fused_hdr ? @"Liberty fused bilinear HDR output" : direct_hdr ? @"Liberty direct EDR output" : @"Liberty EDR output";
        const auto last_extent=flow.effect_output_sizes[flow.effect_count-1];
        const MTLViewport hdr_viewport{double(last_x),double(last_y),double(last_extent.first),double(last_extent.second),0,1};
        const bool transferred=fused_hdr
            ? state_->hdr_transfer.DrawBilinearHdr(encoder,source,game_target,hdr_viewport,hdr,error)
            : state_->hdr_transfer.Draw(encoder,direct_hdr?source:final_target,game_target,hdr,error);
        if (!transferred) {
          [encoder endEncoding]; REXLOG_ERROR("gta4-metal: {}", error); return cancel();
        }
      }
    }
    if (cache_action != PresentationCachePolicy::Action::kDirect) {
      // The first repeat finishes an owned game-only image. All later repeats
      // reuse those exact pixels, with no additional scaling, HDR or dither.
      if (encoder) [encoder endEncoding];
      auto cached_pass = [MTLRenderPassDescriptor renderPassDescriptor];
      cached_pass.colorAttachments[0].texture = drawable.texture;
      cached_pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
      cached_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
      encoder = [commands renderCommandEncoderWithDescriptor:cached_pass];
      if (!encoder) return cancel();
      encoder.label = @"Liberty cached game image and UI";
      if (!state_->game_image_cache.Draw(encoder, drawable.texture, error)) {
        [encoder endEncoding]; REXLOG_ERROR("gta4-metal: {}", error); return cancel();
      }
    }
    if (execute_ui_drawers) {
      MetalUIDrawContext context(*this,width,height,encoder,frame->uploads);
      context.target_format = drawable.texture.pixelFormat;
      context.linear_output = state_->hdr;
      ExecuteUIDrawersFromUIThread(context);
    }
    [encoder endEncoding];
    DisplayFeedback receipt{};
    receipt.surface_epoch = state_->surface_epoch;
    receipt.generated=generated;
    receipt.presentation_id = state_->submitted_frames.load(std::memory_order_relaxed) + 1;
    receipt.generation = pacing_attempt().generation;
    receipt.publication = index < state_->outputs.size() ? properties.provenance.publication_serial : 0;
    receipt.frame = index < state_->outputs.size() ? properties.provenance.submitted_frame : 0;
    receipt.fps = pacing_attempt().fps;
    receipt.queue_host_ns = FramePacerNowNs();
    receipt.target_host_ns = state_->vsync ? state_->display_target_ns : pacing_attempt().slot_ns;
    receipt.callback_host_ns = state_->vsync ? state_->callback_host_ns : 0;
    receipt.deadline_host_ns = state_->vsync ? state_->display_deadline_ns : 0;
    const auto inbox = state_->feedback;
    const auto clock = state_->display_clock;
    [drawable addPresentedHandler:^(id<MTLDrawable> presented) {
      auto value = receipt;
      const uint64_t actual = MetalTimeNanoseconds(presented.presentedTime);
      value.actual_host_ns = actual ? clock.ToHost(actual) : 0;
      inbox->Push(value);
    }];
    const bool trace_timing = rex::diagnostics::IsEnabled(rex::diagnostics::Category::kPresenter);
    const auto lifetime = state_->callbacks;
    if (trace_timing) {
      [commands addScheduledHandler:^(id<MTLCommandBuffer>) {
        auto value = receipt; value.stage = DisplayFeedback::Stage::kScheduled;
        value.event_host_ns = FramePacerNowNs(); inbox->Push(value);
      }];
    }
    [commands addCompletedHandler:^(id<MTLCommandBuffer> completed) {
      if (trace_timing) {
        auto value = receipt; value.stage = DisplayFeedback::Stage::kCompleted;
        value.event_host_ns = FramePacerNowNs();
        const auto gpu_begin = MetalTimeNanoseconds(completed.GPUStartTime);
        const auto gpu_end = MetalTimeNanoseconds(completed.GPUEndTime);
        value.gpu_start_ns = gpu_begin ? clock.ToHost(gpu_begin) : 0;
        value.gpu_end_ns = gpu_end ? clock.ToHost(gpu_end) : 0;
        value.gpu_error = completed.status == MTLCommandBufferStatusError;
        inbox->Push(value);
      }
      if (lifetime->awaiting_frame_slot.exchange(false, std::memory_order_acq_rel)) State::Wake(lifetime);
    }];
    // CAMetalDisplayLink forbids time-targeted presentation calls. Its target
    // timestamp is admission information, not an atTime argument. The deadline
    // is measured at scheduling, separate from GPU completion and display time.
    // Untimed presentation is registered before commit and runs only after
    // Metal schedules this command buffer. Calling drawable.present directly
    // after commit can race scheduling and expose an older drawable contents.
    // The display link still owns VSync opportunities; no CPU wait or new timer.
    if(read_value)[commands encodeSignalEvent:state_->display_done value:read_value];
    [commands presentDrawable:drawable];
    if (!state_->frames.Commit(*frame, commands)) return cancel();
    if(read_value)state_->outputs[index].read_value=read_value;
    if (cache_enabled) state_->game_image_cache.policy.Submitted(image_key, cache_action);
    switch (cache_action) {
      case PresentationCachePolicy::Action::kDirect: ++state_->cache_direct_frames; break;
      case PresentationCachePolicy::Action::kPopulate: ++state_->cache_populated_frames; break;
      case PresentationCachePolicy::Action::kReuse: ++state_->cache_reused_frames; break;
    }
    state_->CancelPaintTimer();
    const uint64_t serial = properties.provenance.publication_serial;
    // Inactive/blank publications also need acknowledgment; screenshots do not.
    if(generated){
      state_->pending_pair=selected_pair;state_->pending_properties=properties;state_->pending_configuration=configuration;
      state_->pending_surface_epoch=state_->surface_epoch;state_->last_generated_publication=serial;
      state_->generated_images.fetch_add(1,std::memory_order_relaxed);
      // The next scene may now render on its queue, but paint demand remains
      // pending until this pair's real frame has actually been submitted.
      AdmitPacedPublication(serial);
      if(index<state_->outputs.size()&&state_->outputs[index].pair==selected_pair)state_->outputs[index].pair.reset();
    }else{
      AcceptPacedPublication(serial);
      if(finishing_pair)state_->pending_pair.reset();
    }
    if(rex::diagnostics::IsEnabled(rex::diagnostics::Category::kPresenter))
      REXLOG_INFO("gta4-metal-delivery kind={} frame={} publication={} generated-count={}",generated?"generated":"real",properties.provenance.submitted_frame,serial,state_->generated_images.load());
    if (!generated && index < state_->outputs.size() && serial) {
      if (serial > state_->last_submitted_publication) state_->new_game_images.fetch_add(1, std::memory_order_relaxed);
      else state_->repeated_game_images.fetch_add(1, std::memory_order_relaxed);
    }
    if(!generated)state_->last_submitted_publication = std::max(state_->last_submitted_publication, serial);
    state_->submitted_frames.fetch_add(1,std::memory_order_relaxed);
    if (rex::diagnostics::IsEnabled(rex::diagnostics::Category::kNativeTrace) &&
        state_->submitted_frames.load(std::memory_order_relaxed) % 300 == 0)
      REXLOG_INFO("gta4-metal-presentation-cache: enabled={} direct={} populated={} reused={}",
          cache_enabled, state_->cache_direct_frames, state_->cache_populated_frames, state_->cache_reused_frames);
    return PaintResult::kPresented;
  }
}

MetalPresenter::DeliveryStatistics MetalPresenter::delivery_statistics() const {
  return {state_->submitted_frames.load(std::memory_order_relaxed),
      state_->new_game_images.load(std::memory_order_relaxed),
      state_->repeated_game_images.load(std::memory_order_relaxed),state_->generated_images.load(std::memory_order_relaxed)};
}

bool MetalPresenter::CaptureGuestOutput(RawImage& result) {
  @autoreleasepool {
    result = {};
    uint32_t index;
    GuestOutputProperties properties;
    auto lock = ConsumeGuestOutput(index,&properties,nullptr);
    if (index >= state_->outputs.size() || !state_->outputs[index].texture) return false;
    auto source = state_->outputs[index].texture;
    const size_t width = source.width, height = source.height;
    if (!width || !height || width > 16384 || height > 16384) return false;
    const size_t pitch = (width * 4 + 255) & ~size_t(255);
    const size_t bytes = pitch * height;
    if (bytes > 512 * 1024 * 1024) return false;
    auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:width height:height mipmapped:NO];
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = MTLTextureUsageRenderTarget;
    auto target = [state_->context->device newTextureWithDescriptor:descriptor];
    auto readback = [state_->context->device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    auto commands = [state_->context->queue commandBuffer];
    if (!target || !readback || !commands) return false;
    auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = target;
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    auto encoder = [commands renderCommandEncoderWithDescriptor:pass];
    if (!encoder) return false;
    [encoder setRenderPipelineState:state_->capture_pipeline];
    [encoder setFragmentTexture:source atIndex:0];
    [encoder setFragmentSamplerState:state_->sampler atIndex:0];
    const uint32_t dither = 0;
    [encoder setFragmentBytes:&dither length:sizeof(dither) atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [encoder endEncoding];
    auto blit = [commands blitCommandEncoder];
    if (!blit) return false;
    [blit copyFromTexture:target sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(width,height,1) toBuffer:readback destinationOffset:0
        destinationBytesPerRow:pitch destinationBytesPerImage:bytes];
    [blit endEncoding];
    [commands commit];
    [commands waitUntilCompleted];  // Explicit synchronous screenshot request, not the render loop.
    if (commands.status != MTLCommandBufferStatusCompleted) return false;
    result.width = uint32_t(width); result.height = uint32_t(height); result.stride = width * 4;
    result.data.resize(result.stride * height);
    for (size_t row=0;row<height;++row)
      std::memcpy(result.data.data()+row*result.stride,static_cast<const std::byte*>(readback.contents)+row*pitch,result.stride);
    return true;
  }
}
}  // namespace rex::ui::metal
