// Actual frontend mutation and AA controller functions are included by the
// extraction harness. Only the unrelated SSAA device-capability query is stubbed.
static void ExerciseHybridAntiAliasingMenu() {
  using namespace rex::cvar;
  using namespace rex::graphics::gta4_native;
  const auto row=std::find_if(kSettings.begin(),kSettings.end(),[](const Setting& s){return s.key=="LR_AA";});
  assert(row!=kSettings.end());
  const auto hybrid=std::find_if(row->choices,row->choices+row->choice_count,[](const Choice& c){return c.value=="msaa4x_smaa";});
  assert(hybrid!=row->choices+row->choice_count && hybrid!=row->choices);
  assert((hybrid-1)->value=="msaa4x");
  assert(kStringPool[size_t(hybrid->text)]=="MSAA 4x + SMAA");
  assert(SetFlagByName("gta4_native_anti_aliasing_unified","true"));
  assert(SetFlagByName("gta4_native_anti_aliasing","off"));
  assert(SetFlagByName("gta4_native_upscaler","native"));
  g_anti_aliasing_controller_initialized=false;
  InitializeAntiAliasingController();
  assert(GetConfiguredAntiAliasingMode()==AntiAliasingMode::kOff);
  assert(GetActiveAntiAliasingMode()==AntiAliasingMode::kOff);
  ClearPendingRestartFlags();
  assert(SetConfiguredAntiAliasingMode("msaa4x_smaa")==AntiAliasingApplyResult::kRestartRequired);
  assert(GetConfiguredAntiAliasingMode()==AntiAliasingMode::kMsaa4xSmaa);
  assert(GetActiveAntiAliasingMode()==AntiAliasingMode::kOff);
  assert(GetFlagByName("gta4_native_msaa")=="4x");
  auto pending=GetPendingRestartFlags();
  assert(std::find(pending.begin(),pending.end(),"gta4_native_msaa")!=pending.end());
  SaveGraphicsSettings();
  assert(SetFlagByName("gta4_native_anti_aliasing","smaa"));
  LoadConfig(g_config_path);
  assert(GetFlagByName("gta4_native_anti_aliasing")=="msaa4x_smaa");
  // Reinitialize from saved flags exactly as a fresh application process does.
  g_anti_aliasing_controller_initialized=false;
  InitializeAntiAliasingController();
  assert(GetActiveAntiAliasingMode()==AntiAliasingMode::kMsaa4xSmaa);
  ClearPendingRestartFlags();
  ChangeSetting(*row,-1);
  assert(CurrentSettingValue(*row)=="msaa4x");
  assert(GetActiveAntiAliasingMode()==AntiAliasingMode::kMsaa4x);
  assert(GetPendingRestartFlags().empty());
  ChangeSetting(*row,1);
  assert(CurrentSettingValue(*row)=="msaa4x_smaa");
  assert(GetActiveAntiAliasingMode()==AntiAliasingMode::kMsaa4xSmaa);
  assert(GetPendingRestartFlags().empty());
  // Leaving four-sample topology queues a restart without changing active targets.
  assert(SetConfiguredAntiAliasingMode("fxaa")==AntiAliasingApplyResult::kRestartRequired);
  assert(GetConfiguredAntiAliasingMode()==AntiAliasingMode::kFxaa);
  assert(GetActiveAntiAliasingMode()==AntiAliasingMode::kMsaa4xSmaa);
  assert(SetConfiguredAntiAliasingMode("msaa8x")==AntiAliasingApplyResult::kRejected);
  assert(GetConfiguredAntiAliasingMode()==AntiAliasingMode::kFxaa);
  ClearPendingRestartFlags();g_display_menu.rebuild_pending=false;
  std::cout<<"PASS hybrid AA real menu/controller: labels, save/reload, restart latch, live 4x toggle, unsupported 8x rejection\n";
}
