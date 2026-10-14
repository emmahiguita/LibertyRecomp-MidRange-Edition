// The extraction harness supplies only the runtime/device query boundary.
// These checks call the production resolver, visibility, mutation and save code.
static void ExerciseTemporalMenuBindings() {
  using namespace rex::cvar;
  using namespace rex::graphics::gta4_native;
  const auto row_for = [](SettingBinding binding) -> const Setting& {
    assert(std::count_if(kSettings.begin(), kSettings.end(),
        [&](const Setting& setting) { return setting.binding == binding; }) == 1);
    return *std::find_if(kSettings.begin(), kSettings.end(),
        [&](const Setting& setting) { return setting.binding == binding; });
  };
  const auto& upscale = row_for(SettingBinding::kUpscaler);
  const auto& quality = row_for(SettingBinding::kUpscalingQuality);
  const auto& generation = row_for(SettingBinding::kFrameGeneration);
  const auto& aa = row_for(SettingBinding::kAntiAliasing);
  std::vector<std::pair<std::string, std::string>> saved;
  for (const char* name : {"gpu_plugin", "gta4_native_upscaler", "gta4_fsr1_quality",
       "gta4_temporal_upscaler_quality", "gta4_metalfx_frame_generation",
       "gta4_native_frame_generation", "gta4_present_mode", "gta4_native_anti_aliasing",
       "gta4_native_anti_aliasing_unified", "gta4_native_msaa", "gta4_native_spatial_aa"}) {
    assert(GetFlagInfo(name));
    saved.emplace_back(name, GetFlagByName(name));
  }
  const auto visible_choices = [](const Setting& row, uint32_t capabilities) {
    std::vector<std::string_view> choices;
    for (size_t index = 0; index < row.choice_count; ++index)
      if (ChoiceAvailable(row, row.choices[index].value, capabilities))
        choices.push_back(row.choices[index].value);
    return choices;
  };
  const auto reset_aa = [] {
    assert(SetFlagByName("gta4_native_upscaler", "native"));
    assert(SetFlagByName("gta4_native_anti_aliasing", "off"));
    assert(SetFlagByName("gta4_native_anti_aliasing_unified", "true"));
    g_anti_aliasing_controller_initialized = false;
    InitializeAntiAliasingController();
  };

  frontend_fixture::capabilities = kCapabilityFsr3Upscaling;
  const auto before_queries = frontend_fixture::capability_queries;
  frontend_fixture::runtime_present = false;
  assert(TemporalCapabilities() == 0 && !SettingAvailable(upscale, 0));
  assert(!SettingAvailable(quality, 0));
  frontend_fixture::runtime_present = true;
  frontend_fixture::graphics_present = false;
  assert(TemporalCapabilities() == 0 && !SettingAvailable(upscale, 0));
  frontend_fixture::graphics_present = true;
  frontend_fixture::title_abi = 0;
  assert(TemporalCapabilities() == 0 && !SettingAvailable(quality, 0));
  assert(frontend_fixture::capability_queries == before_queries);
  frontend_fixture::title_abi = kTitleCommandAbi;
  frontend_fixture::query_succeeds = false;
  assert(TemporalCapabilities() == 0 && !SettingAvailable(generation, TemporalCapabilities()));
  frontend_fixture::query_succeeds = true;
  assert(TemporalCapabilities() == kCapabilityFsr3Upscaling);
  assert(frontend_fixture::capability_queries > before_queries);

  frontend_fixture::capabilities = 0;
  assert(SettingAvailable(upscale, 0) && SettingAvailable(quality, 0));
  assert(!SettingAvailable(generation, 0));
  assert((visible_choices(upscale, 0) == std::vector<std::string_view>{"native", "fsr1"}));
  assert((visible_choices(generation, 0) == std::vector<std::string_view>{"off"}));
  assert(!ChoiceAvailable(aa, "taa", 0) && !ChoiceAvailable(aa, "metalfx_taa", 0));

#if REX_PLATFORM_MAC && !REX_PLATFORM_IOS
  // Python verified capability masks: Metal = 0x184, Vulkan SR/FG = 0x78.
  constexpr uint32_t metal = kCapabilityNativeTemporalAA |
      kCapabilityMetalFxTemporalUpscaling | kCapabilityMetalFxFrameGeneration;
  frontend_fixture::capabilities = metal;
  assert((visible_choices(upscale, TemporalCapabilities()) ==
          std::vector<std::string_view>{"native", "fsr1", "metalfx"}));
  assert((visible_choices(generation, metal) ==
          std::vector<std::string_view>{"off", "metalfx"}));
  assert(SettingAvailable(generation, metal));
  assert(!SettingAvailable(generation, kCapabilityNativeTemporalAA |
                                     kCapabilityMetalFxTemporalUpscaling));
  assert(ChoiceAvailable(aa, "taa", metal) && ChoiceAvailable(aa, "metalfx_taa", metal));
  assert(!ChoiceAvailable(upscale, "fsr3", metal) && !ChoiceAvailable(upscale, "dlss", metal));
  reset_aa();
  assert(SetFlagByName("gpu_plugin", "gta4-metal"));
  assert(SetFlagByName("gta4_native_frame_generation", "off"));
  assert(SetFlagByName("gta4_metalfx_frame_generation", "false"));
  assert(ResolveSetting(generation).cvar == "gta4_metalfx_frame_generation");
  ChangeSetting(upscale, 1);
  assert(CurrentSettingValue(upscale) == "fsr1");
  ChangeSetting(upscale, 1);
  assert(CurrentSettingValue(upscale) == "metalfx");
  assert(GetConfiguredAntiAliasingMode() == AntiAliasingMode::kMetalFxTaa);
  ChangeSetting(upscale, 1);
  assert(CurrentSettingValue(upscale) == "native");
  ChangeSetting(upscale, -1);
  assert(CurrentSettingValue(upscale) == "metalfx");
  assert(SetFlagByName("gta4_present_mode", "immediate"));
  ChangeSetting(generation, 1);
  assert(GetFlagByName("gta4_metalfx_frame_generation") == "true");
  assert(GetFlagByName("gta4_native_frame_generation") == "off");
  assert(CurrentSettingValue(generation) == "metalfx");
  assert(GetFlagByName("gta4_present_mode") == "vsync");
  SaveGraphicsSettings();
  assert(SetFlagByName("gta4_metalfx_frame_generation", "false"));
  LoadConfig(g_config_path);
  assert(CurrentSettingValue(generation) == "metalfx");
  // A pending renderer selection must not change the active device's binding.
  assert(SetFlagByName("gpu_plugin", "gta4-native"));
  assert(ResolveSetting(generation).cvar == "gta4_metalfx_frame_generation");
  ChangeSetting(generation, -1);
  assert(CurrentSettingValue(generation) == "off");
  assert(GetFlagByName("gta4_metalfx_frame_generation") == "false");
  std::cout << "PASS active Metal offers Native/FSR1/MetalFX only; one FG row maps and persists the Metal bool\n";
#endif

  reset_aa();
  assert(SetFlagByName("gpu_plugin", "gta4-native"));
  constexpr uint32_t vulkan = kCapabilityFsr3Upscaling | kCapabilityDlssUpscaling |
      kCapabilityFsrFrameGeneration | kCapabilityDlssFrameGeneration;
  frontend_fixture::capabilities = vulkan;
  std::vector<std::string_view> expected_upscalers{"native", "fsr1"};
  std::vector<std::string_view> expected_generation{"off"};
#if defined(LIBERTY_HAS_FSR3)
  expected_upscalers.push_back("fsr3"); expected_generation.push_back("fsr3");
#endif
#if defined(LIBERTY_HAS_DLSS)
  expected_upscalers.push_back("dlss"); expected_generation.push_back("dlss");
#endif
  assert(visible_choices(upscale, vulkan) == expected_upscalers);
  assert(visible_choices(generation, vulkan) == expected_generation);
  assert(!ChoiceAvailable(upscale, "metalfx", vulkan));
  assert(!ChoiceAvailable(generation, "metalfx", vulkan));
  assert(!ChoiceAvailable(upscale, "dlss", kCapabilityFsr3Upscaling));
  assert(!ChoiceAvailable(upscale, "fsr3", kCapabilityDlssUpscaling));
  assert(!ChoiceAvailable(generation, "dlss", kCapabilityFsrFrameGeneration));
  assert(!ChoiceAvailable(generation, "fsr3", kCapabilityDlssFrameGeneration));
  assert(!SettingAvailable(generation, kCapabilityFsr3Upscaling | kCapabilityDlssUpscaling));
  assert(ResolveSetting(generation).cvar == "gta4_native_frame_generation");
  for (const auto value : expected_upscalers) {
    assert(CurrentSettingValue(upscale) == value);
    ChangeSetting(upscale, 1);
  }
  assert(CurrentSettingValue(upscale) == "native");

  assert(SetFlagByName("gta4_fsr1_quality", "quality"));
  assert(SetFlagByName("gta4_temporal_upscaler_quality", "balanced"));
  for (const char* value : {"native", "fsr1", "metalfx"}) {
    assert(SetFlagByName("gta4_native_upscaler", value));
    const Setting resolved = ResolveSetting(quality);
    assert(resolved.cvar == "gta4_fsr1_quality");
    assert(resolved.choices == kFsrQualityChoices.data());
    assert(CurrentSettingValue(quality) == "quality");
    ChangeSetting(quality, 1);
    assert(GetFlagByName("gta4_fsr1_quality") == "balanced");
    assert(GetFlagByName("gta4_temporal_upscaler_quality") == "balanced");
    ChangeSetting(quality, -1);
    assert(CurrentSettingValue(quality) == "quality");
  }
  for (const char* value : {"fsr3", "dlss"}) {
    assert(SetFlagByName("gta4_native_upscaler", value));
    const Setting resolved = ResolveSetting(quality);
    assert(resolved.cvar == "gta4_temporal_upscaler_quality");
    assert(resolved.choices == kTemporalQualityChoices.data());
    assert(CurrentSettingValue(quality) == "balanced");
    ChangeSetting(quality, 1);
    assert(GetFlagByName("gta4_temporal_upscaler_quality") == "performance");
    assert(GetFlagByName("gta4_fsr1_quality") == "quality");
    ChangeSetting(quality, -1);
    assert(CurrentSettingValue(quality) == "balanced");
  }
  SaveGraphicsSettings();
  // Config omits default values; emulate fresh-process defaults before load.
  ResetToDefault("gta4_fsr1_quality");
  ResetToDefault("gta4_temporal_upscaler_quality");
  LoadConfig(g_config_path);
  assert(GetFlagByName("gta4_fsr1_quality") == "quality");
  assert(GetFlagByName("gta4_temporal_upscaler_quality") == "balanced");
  std::cout << "PASS one Quality row routes FSR1/MetalFX and FSR3/DLSS to their real persisted CVars\n";

  // FG selection is independent of the SR vendor, but still requires a temporal
  // SR route and synchronized presentation. Metal's legacy bool is untouched.
  assert(SetFlagByName("gta4_metalfx_frame_generation", "true"));
  assert(SetFlagByName("gta4_native_frame_generation", "off"));
  assert(SetFlagByName("gta4_native_upscaler", "native"));
  ChangeSetting(generation, 1);
  assert(CurrentSettingValue(generation) == "off");
  assert(SetFlagByName("gta4_native_upscaler", "dlss"));
  for (const auto value : expected_generation) {
    assert(CurrentSettingValue(generation) == value);
    assert(SetFlagByName("gta4_present_mode", "immediate"));
    ChangeSetting(generation, 1);
    assert(GetFlagByName("gta4_metalfx_frame_generation") == "true");
    if (CurrentSettingValue(generation) != "off")
      assert(GetFlagByName("gta4_present_mode") == "vsync");
  }
  assert(CurrentSettingValue(generation) == "off");
  frontend_fixture::capabilities = 0;
  assert(!SettingAvailable(generation, TemporalCapabilities()));
  ChangeSetting(generation, 1);
  assert(CurrentSettingValue(generation) == "off");
  for (const auto& [name, value] : saved) assert(SetFlagByName(name, value));
  g_anti_aliasing_controller_initialized = false;
  InitializeAntiAliasingController();
  ClearPendingRestartFlags();
  g_display_menu.rebuild_pending = false;
  std::cout << "PASS one Vulkan FG row filters by device capability, maps provider names, and enforces temporal SR/vsync\n";
  std::cout << "PASS runtime query failure/absent graphics/wrong ABI hide unavailable temporal controls\n";
}
