#pragma once

#include <memory>
#include <string>

#include <rex/ui/immediate_drawer.h>

namespace rex::ui::metal {

struct MetalContext;

class MetalImmediateDrawer final : public ImmediateDrawer {
 public:
  static std::unique_ptr<MetalImmediateDrawer> Create(std::shared_ptr<MetalContext> context,
                                                     std::string& error);
  ~MetalImmediateDrawer() override;
  std::unique_ptr<ImmediateTexture> CreateTexture(uint32_t width, uint32_t height,
      ImmediateTextureFilter filter, bool repeated, const uint8_t* data) override;
  void Begin(UIDrawContext& context, float width, float height) override;
  void BeginDrawBatch(const ImmediateDrawBatch& batch) override;
  void Draw(const ImmediateDraw& draw) override;
  void EndDrawBatch() override;
  void End() override;

 private:
  struct State;
  explicit MetalImmediateDrawer(std::shared_ptr<MetalContext> context);
  bool Initialize(std::string& error);
  std::unique_ptr<State> state_;
};

}  // namespace rex::ui::metal
