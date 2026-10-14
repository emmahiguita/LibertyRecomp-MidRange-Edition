#pragma once

#include <memory>
#include <string>

#include <rex/ui/graphics_provider.h>

namespace rex::ui::metal {

struct MetalContext;

// Apple objects remain private to Objective-C++ implementation files.
class MetalProvider final : public GraphicsProvider {
 public:
  static std::unique_ptr<MetalProvider> Create(std::string& error);
  ~MetalProvider() override;
  const std::shared_ptr<MetalContext>& context() const { return context_; }

  std::unique_ptr<Presenter> CreatePresenter(
      Presenter::HostGpuLossCallback callback = Presenter::FatalErrorHostGpuLossCallback) override;
  std::unique_ptr<ImmediateDrawer> CreateImmediateDrawer() override;

 private:
  explicit MetalProvider(std::shared_ptr<MetalContext> context);
  std::shared_ptr<MetalContext> context_;
};

}  // namespace rex::ui::metal
