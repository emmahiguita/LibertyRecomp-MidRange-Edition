#include <rex/ui/metal/provider.h>
#include <rex/ui/metal/presenter.h>
#include <rex/logging.h>

#include "context.h"
#include "immediate_drawer.h"

namespace rex::ui::metal {
MetalProvider::MetalProvider(std::shared_ptr<MetalContext> context) : context_(std::move(context)) {}
MetalProvider::~MetalProvider() = default;
std::unique_ptr<MetalProvider> MetalProvider::Create(std::string& error) {
  auto context = MetalContext::Create(error);
  return context ? std::unique_ptr<MetalProvider>(new MetalProvider(std::move(context))) : nullptr;
}
std::unique_ptr<Presenter> MetalProvider::CreatePresenter(Presenter::HostGpuLossCallback callback) {
  return MetalPresenter::Create(context_, std::move(callback));
}
std::unique_ptr<ImmediateDrawer> MetalProvider::CreateImmediateDrawer() {
  std::string error;
  auto drawer = MetalImmediateDrawer::Create(context_, error);
  if (!drawer) REXLOG_ERROR("gta4-metal: UI drawer: {}", error);
  return drawer;
}
}  // namespace rex::ui::metal
