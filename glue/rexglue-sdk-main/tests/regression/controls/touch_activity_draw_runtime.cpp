#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>

#include <imgui.h>
#include <imgui_internal.h>
#include "input/context_touch_activity.h"
#include "input/context_touch_draw.h"

using namespace gta4::input;
namespace {
ContextTouchViewport View(float width,float height) {
  ContextTouchViewport v;
  v.generation=1;v.valid=v.focused=v.host_space=true;
  v.logical_width=v.output_width=v.safe_width=width;
  v.logical_height=v.output_height=v.safe_height=height;
  v.physical_surface_width=v.physical_output_width=static_cast<uint32_t>(width);
  v.physical_surface_height=v.physical_output_height=static_cast<uint32_t>(height);
  return v;
}
int Render(ImDrawList& draw, const ContextTouchOverlaySnapshot& snapshot) {
  draw._ResetForNewFrame();
  draw.PushTextureID(ImGui::GetIO().Fonts->TexID);
  draw.PushClipRect(ImVec2(0,0),ImVec2(snapshot.layout.viewport.logical_width,snapshot.layout.viewport.logical_height));
  DrawContextTouchOverlay(&draw,ImGui::GetFont(),ImGui::GetFontSize(),snapshot,
      snapshot.layout.viewport.logical_width,snapshot.layout.viewport.logical_height,nullptr,nullptr);
  draw.PopClipRect(); draw.PopTextureID();
  for(const auto& v:draw.VtxBuffer) {
    assert(std::isfinite(v.pos.x)&&std::isfinite(v.pos.y)&&std::isfinite(v.uv.x)&&std::isfinite(v.uv.y));
    assert(v.pos.x>=-1 && v.pos.y>=-1);
    assert(v.pos.x<=snapshot.layout.viewport.logical_width+1 && v.pos.y<=snapshot.layout.viewport.logical_height+1);
  }
  for(const auto& command:draw.CmdBuffer)
    assert(std::isfinite(command.ClipRect.x)&&std::isfinite(command.ClipRect.y)&&std::isfinite(command.ClipRect.z)&&std::isfinite(command.ClipRect.w));
  return draw.VtxBuffer.Size;
}
}
int main() {
  ImGui::CreateContext();
  auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;
  io.DisplaySize=ImVec2(1280,720);io.DeltaTime=1.0f/60.0f;
  unsigned char* pixels=nullptr;int width=0,height=0;
  io.Fonts->AddFontDefault();io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
  assert(pixels&&width>0&&height>0);
  ImGui::NewFrame();
  {
    ImDrawList draw(ImGui::GetDrawListSharedData());
    const std::pair<TouchActivityKind,TouchActivityPhase> cases[] = {
      {TouchActivityKind::kBowling,TouchActivityPhase::kStroke},
      {TouchActivityKind::kPool,TouchActivityPhase::kAim},
      {TouchActivityKind::kDarts,TouchActivityPhase::kAim},
      {TouchActivityKind::kQub3d,TouchActivityPhase::kPlaying},
      {TouchActivityKind::kDancing,TouchActivityPhase::kHold},
      {TouchActivityKind::kChampagne,TouchActivityPhase::kDrink},
      {TouchActivityKind::kHiLo,TouchActivityPhase::kChoice},
    };
    for(const auto& [kind,phase]:cases) for(bool handed:{false,true}) {
      ContextTouchOverlaySnapshot snapshot;
      auto viewport=View(handed?720:1280,handed?1280:720);
      TouchActivitySnapshot activity;activity.valid=true;activity.kind=kind;activity.phase=phase;activity.script_thread=71;
      ContextTouchLayoutOptions options;options.contextual=true;options.left_handed=handed;
      snapshot.layout=BuildTouchActivityLayout(viewport,activity,1,{},options);snapshot.visible=true;
      const auto count=Render(draw,snapshot);assert(count>0);
      snapshot.outgoing_layout=std::make_shared<const ContextTouchLayout>(snapshot.layout);
      snapshot.layout_alpha=0.5f;snapshot.outgoing_alpha=0.5f;
      assert(Render(draw,snapshot)>count);
      snapshot.outgoing_layout.reset();snapshot.layout_alpha=0;assert(Render(draw,snapshot)==0);
    }
    std::cout<<"PASS actual ImGui activity rendering emits finite in-bounds geometry and independently blends both layout layers\n";
    ContextTouchOverlaySnapshot native;
    native.layout.viewport=View(1280,720);native.layout.mode=ContextTouchMode::kOnFoot;native.visible=true;
    native.layout.control_count=1;auto& control=native.layout.controls[0];
    control.visible=true;control.native_hud=true;control.kind=ContextTouchControlKind::kUtility;
    control.action=TouchAction::kWeaponWheel;control.radius=30;control.center_x=100;control.center_y=100;
    assert(Render(draw,native)==0);
    std::cout<<"PASS native weapon silhouette hit target is never painted a second time\n";
  }
  ImGui::EndFrame();ImGui::DestroyContext();
}
