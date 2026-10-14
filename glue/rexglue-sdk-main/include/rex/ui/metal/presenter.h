#pragma once

#include <memory>

#include <rex/ui/presenter.h>

namespace rex::ui::metal {

struct MetalContext;

// Surface lifecycle and UI encoding are serialized by the existing Presenter.
// Display feedback is consumed on this same owner, never on the callback thread.
class MetalPresenter final : public Presenter {
 public:
  static std::unique_ptr<MetalPresenter> Create(std::shared_ptr<MetalContext> context,
                                               HostGpuLossCallback callback);
  ~MetalPresenter() override;
  Surface::TypeFlags GetSupportedSurfaceTypes() const override;
  bool CaptureGuestOutput(RawImage& image_out) override;
  uint64_t submitted_frames() const;
  struct DeliveryStatistics { uint64_t submissions, new_game_images, repeated_game_images, generated_images; };
  DeliveryStatistics delivery_statistics() const;

 protected:
  SurfacePaintConnectResult ConnectOrReconnectPaintingToSurfaceFromUIThread(
      Surface& surface, uint32_t width, uint32_t height, bool was_paintable,
      bool& implicit_vsync) override;
  void DisconnectPaintingFromSurfaceFromUIThreadImpl() override;
  bool RefreshGuestOutputImpl(uint32_t mailbox_index, uint32_t width, uint32_t height,
      std::function<bool(GuestOutputRefreshContext&)> refresher, bool& is_8bpc) override;
  PaintResult PaintAndPresentImpl(bool execute_ui_drawers) override;
  bool ScheduleFramePacingWakeup(uint64_t delay_ns) override;
  void PollPresentationTiming() override;
  std::optional<uint64_t> DisplayLinkTargetNs() const override;
  bool RequiresUIThreadPresentation() const override { return true; }
  bool UsesExplicitPaintDemand() const override { return true; }

 private:
  struct State;
  MetalPresenter(std::shared_ptr<MetalContext> context, HostGpuLossCallback callback);
  std::unique_ptr<State> state_;
};

}  // namespace rex::ui::metal
