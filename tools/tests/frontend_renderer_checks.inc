// Used by the real-hook extraction harness; changes only its temporary config.
static void ExerciseRendererMenu(uint8_t* base) {
  using namespace rex::cvar;
  const auto& setting = kSettings.front();
  assert(setting.key == "LR_RENDERER" && setting.cvar == "gpu_plugin");
  assert(setting.restart_required && setting.binding == SettingBinding::kCvar);
  assert(setting.choice_count == 3);
  const std::array<std::pair<std::string_view, std::string_view>, 3> choices{{
      {"xenos", "Emulated"}, {"gta4-native", "Vulkan"}, {"gta4-metal", "Metal"}}};
  const auto* info = GetFlagInfo("gpu_plugin");
  assert(info && info->lifecycle == Lifecycle::kRequiresRestart);
  const std::string saved = GetFlagByName("gpu_plugin");
  assert(SetFlagByName("gpu_plugin", ""));
  assert(CurrentSettingValue(setting) == "gta4-native");
  FinalizeInit();
  for (size_t i = 0; i < choices.size(); ++i) {
    assert(setting.choices[i].value == choices[i].first);
    assert(kStringPool[size_t(setting.choices[i].text)] == choices[i].second);
    assert(SetFlagByName("gpu_plugin", choices[i].first));
    assert(CurrentChoiceIndex(setting) == i);
    ChangeSetting(setting, 1);
    assert(CurrentSettingValue(setting) == choices[(i + 1) % choices.size()].first);
    ChangeSetting(setting, -1);
    assert(CurrentSettingValue(setting) == choices[i].first);
    const auto pending = GetPendingRestartFlags();
    assert(std::find(pending.begin(), pending.end(), "gpu_plugin") != pending.end());
    SaveGraphicsSettings();
    assert(std::filesystem::is_regular_file(g_config_path));
    assert(SetFlagByName("gpu_plugin", ""));
    LoadConfig(g_config_path);
    assert(GetFlagByName("gpu_plugin") == choices[i].first);
  }
  assert(WriteSettingRow(base, 0x83000080, setting));
  assert(FindSettingByKey(base, 0x83000080 + kOptionLabelOffset) == &setting);
  assert(SetFlagByName("gpu_plugin", saved));
  const auto motion = std::find_if(kSettings.begin(), kSettings.end(),
      [](const Setting& value) { return value.key == "LR_MBLUR"; });
  assert(motion != kSettings.end() && motion->cvar == "gta4_motion_blur" && motion->choice_count == 2);
  assert(motion->choices[0].value == "false" && motion->choices[1].value == "true");
  ClearPendingRestartFlags(); g_display_menu.rebuild_pending = false;
  std::cout << "PASS Renderer is first: Emulated/Vulkan/Metal; restart-only cycling, labels and saved reload\n";
  std::cout << "PASS Motion Blur frontend row is a live Off/On gta4_motion_blur binding\n";
}
