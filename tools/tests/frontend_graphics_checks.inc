// Actual menu choice mutation and persistence, using the real CVar definitions.
static void ExerciseGraphicsMenuBindings(uint8_t* base) {
  using namespace rex::cvar;
  size_t transitions=0;
  for(std::string_view key:{"LR_FPS","LR_FSR","LR_TEXFILTER","LR_ANISO"}) {
    const auto row=std::find_if(kSettings.begin(),kSettings.end(),[&](const Setting& s){return s.key==key;});
    assert(row!=kSettings.end());
    assert(row->binding==(key=="LR_FSR" ? SettingBinding::kUpscalingQuality : SettingBinding::kCvar));
    const auto saved=CurrentSettingValue(*row);const auto* info=GetFlagInfo(row->cvar);
    std::cerr<<"menu_registry key="<<key<<" found="<<bool(info)<<" lifecycle="<<(info?int(info->lifecycle):-1)<<" restart="<<row->restart_required<<"\n";
    assert(info && info->lifecycle==(row->restart_required?Lifecycle::kRequiresRestart:Lifecycle::kHotReload));
    for(size_t choice=0;choice<row->choice_count;++choice) {
      assert(SetFlagByName(row->cvar,row->choices[choice].value));
      assert(CurrentChoiceIndex(*row)==choice);
      assert(!kStringPool[size_t(CurrentChoice(*row).text)].empty());
      ChangeSetting(*row,1);
      assert(CurrentSettingValue(*row)==row->choices[(choice+1)%row->choice_count].value);
      ChangeSetting(*row,-1);assert(CurrentSettingValue(*row)==row->choices[choice].value);
      // Configuration omits default-valued flags. A fresh launch starts from
      // defaults, not the previous test iteration's unsaved value.
      SaveGraphicsSettings();assert(SetFlagByName(row->cvar,info->default_value));
      LoadConfig(g_config_path);assert(CurrentSettingValue(*row)==row->choices[choice].value);
      assert(WriteSettingRow(base,0x83000080,*row));
      assert(FindSettingByKey(base,0x83000080+kOptionLabelOffset)==&*row);
      ++transitions;
    }
    assert(SetFlagByName(row->cvar,saved));
  }
  const auto aa=std::find_if(kSettings.begin(),kSettings.end(),[](const Setting& s){return s.key=="LR_AA";});
  assert(aa!=kSettings.end() && aa->binding==SettingBinding::kAntiAliasing);
  for(size_t i=0;i<aa->choice_count;++i) {
    const auto parsed=rex::graphics::gta4_native::ParseAntiAliasingMode(aa->choices[i].value);
    assert(parsed && rex::graphics::gta4_native::IsValidAntiAliasingRoute(rex::graphics::gta4_native::GetAntiAliasingRoute(*parsed)));
  }
  const auto upscale=std::find_if(kSettings.begin(),kSettings.end(),[](const Setting& s){return s.key=="LR_UPSCALE";});
  assert(upscale!=kSettings.end() && upscale->binding==SettingBinding::kUpscaler && upscale->restart_required);
  assert(upscale->choices[0].value=="native" && upscale->choices[1].value=="fsr1");
  std::vector<std::string_view> available;
  for(size_t i=0;i<upscale->choice_count;++i)
    if(ChoiceAvailable(*upscale,upscale->choices[i].value,0)) available.push_back(upscale->choices[i].value);
  assert((available==std::vector<std::string_view>{"native","fsr1"}));
  ClearPendingRestartFlags();g_display_menu.rebuild_pending=false;
  std::cout<<"PASS graphics menu mutation/save/reload transitions="<<transitions<<"; AA stage combinations are valid; upscaler restart binding preserved\n";
}
