#!/usr/bin/env python3
"""Execute production touch-help placement and the original PPC HUD converter.

Host presentation and input snapshots are controlled. Geometry, native aspect
conversion, call-site isolation, and touch visibility come from production.
"""
import argparse
from pathlib import Path
import re
import subprocess

SDK = Path(__file__).resolve().parents[2]
SRC = SDK / 'gta4-recomp/src'


def function(source, signature):
    start = source.index(signature)
    end = source.index('\n}', start) + len('\n}')
    return source[start:end] + '\n'


PREFIX = r'''
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>
#include <sys/mman.h>
#include "input/context_touch_controls.h"
#include "input/context_touch_context.h"
#include "gta4_aspect_hooks.h"
#include <rex/input/absolute_pointer.h>
union PPCRegister { uint64_t u64; int64_t s64; uint32_t u32; int32_t s32; uint8_t u8; double f64; float f32; };
struct Xer {};
struct Cr { bool eq=false,lt=false,gt=false;
 template<class T> void compare(T a,T b,const Xer& = {}) {eq=a==b;lt=a<b;gt=a>b;}
};
struct Fpscr {void disableFlushMode(){}};
struct PPCContext { REGISTERS uint64_t lr{}; PPCRegister ctr{}; Xer xer; Cr cr6; Fpscr fpscr; };
uint32_t read32(const uint8_t* p){uint32_t v;std::memcpy(&v,p,4);return __builtin_bswap32(v);}
void write32(uint8_t* p,uint32_t v){v=__builtin_bswap32(v);std::memcpy(p,&v,4);}
#define REX_FUNC_PROLOGUE()
#define DEFINE_REX_FUNC(n) void __imp__##n(PPCContext& ctx,uint8_t* base)
#define REX_LOAD_U32(a) read32(base+uint32_t(a))
#define REX_STORE_U32(a,v) write32(base+uint32_t(a),uint32_t(v))
bool native_wide=false;
void __savegprlr_28(PPCContext&,uint8_t*){}
void __restgprlr_28(PPCContext&,uint8_t*){}
void sub_8222D7C8(PPCContext& c,uint8_t*){c.r3.u64=native_wide;}
namespace rex::memory {uint8_t* GuestPtr(uint8_t* base,uint32_t a){return base+a;}}
rex::input::TouchPresentationState presentation;
bool rex::input::GetTouchPresentationState(TouchPresentationState* out) noexcept {*out=presentation;return true;}
gta4::aspect::UiContext ui;
gta4::aspect::UiContext gta4::aspect::CurrentUi(uint8_t*) {return ui;}
namespace gta4::input {
ContextTouchOverlaySnapshot visual,current;
TouchContextSnapshot facts;
bool editor=false;
ContextTouchOverlaySnapshot GetContextTouchDrawableOverlaySnapshot(uint64_t) noexcept {return visual;}
ContextTouchOverlaySnapshot GetContextTouchOverlaySnapshot() noexcept {return current;}
bool IsContextTouchEditorActive() noexcept {return editor;}
TouchContextSnapshot GetTouchContextSnapshot() noexcept {return facts;}
bool RadarSpan(uint8_t* base,uint32_t a,size_t n,bool=false){return base && a>=4096 && uint64_t(a)+n<=kMemorySize;}
'''

HARNESS = r'''
int main() {
 using namespace gta4::input;
 namespace a=gta4::aspect;
 auto* base=static_cast<uint8_t*>(mmap(nullptr,kMemorySize,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));
 assert(base!=MAP_FAILED);
 RadarFloat(base,kNativeScale,0.75);
 const auto close=[](double a,double b){assert(std::abs(a-b)<0.00001);};
 const auto setup=[&](bool touch) {
   facts={};visual={};current={};editor=false;
   visual.visible=touch;
   presentation={};presentation.valid=presentation.focused=true;
   presentation.output_width=presentation.physical_output_width=presentation.safe_area_width=1280;
   presentation.output_height=presentation.physical_output_height=presentation.safe_area_height=720;
   ui={.transform={},.render={1280,720},.role=a::UiRole::kHelp,.active=true};
   RadarFloat(base,kAuthoredRadarRect,0.048);RadarFloat(base,kRadarY,0.745);
   RadarFloat(base,kRadarWidth,0.12075);RadarFloat(base,kRadarHeight,0.211);
 };
 const auto invoke=[&](uint32_t caller=0x8222438C) {
   RadarFloat(base,kPosition,0.072);RadarFloat(base,kPositionY,0.055);
   RadarFloat(base,kWrap,0.072);RadarFloat(base,kWrapRight,0.372);
   PPCContext c{};c.r1.u32=kStack;c.lr=caller;c.r3.u32=2;
   c.r4.u32=kPosition;c.r6.u32=kWrap;
   sub_821C31A8(c,base);assert(c.r1.u32==kStack);return c;
 };
 for (bool wide:{false,true}) {
   native_wide=wide;setup(false);invoke();
   const std::array baseline={RadarRead(base,kPosition),RadarRead(base,kPositionY),
                              RadarRead(base,kWrap),RadarRead(base,kWrapRight)};
   const auto unchanged=[&] {
     invoke();assert((baseline==std::array{RadarRead(base,kPosition),RadarRead(base,kPositionY),
                                         RadarRead(base,kWrap),RadarRead(base,kWrapRight)}));
   };
   unchanged();
   visual.visible=true;facts.frontend=true;unchanged();facts.frontend=false;
   facts.map=true;unchanged();facts.map=false;
   presentation.focused=false;unchanged();presentation.focused=true;
   presentation.valid=false;unchanged();presentation.valid=true;
   visual.visible=false;unchanged();
   visual.visible=true;invoke(0x821C0000);
   assert((baseline==std::array{RadarRead(base,kPosition),RadarRead(base,kPositionY),
                              RadarRead(base,kWrap),RadarRead(base,kWrapRight)}));
   for(int visible_source:{0,1,2}) {
     visual.visible=visible_source==0;current.visible=visible_source==1;editor=visible_source==2;
     invoke();assert(RadarFloat(base,kPosition)>kRadarRight);
     close(RadarFloat(base,kPosition),kExpectedLeft);
     close(RadarFloat(base,kWrap),RadarFloat(base,kPosition));
     close(RadarFloat(base,kWrapRight)-RadarFloat(base,kWrap),wide?0.225:0.3);
   }
   editor=false;current.visible=false;visual.visible=false;unchanged();
 }
 std::cout<<"PASS touch-off, pause, map, invalid/unfocused presentation and unrelated native calls preserve original coordinate bytes; fade/editor gates match radar\n";
 setup(true);native_wide=true;invoke();
 const double final_edge=RadarFloat(base,kWrapRight);
 RadarFloat(base,kIconWidth,0.046);RadarFloat(base,kIconOffset,0.005);
 PPCContext icon{};icon.r1.u32=kStack;icon.lr=0x822244B4;icon.r3.u32=7;icon.r5.u32=kIconOffset;
 // First icon-size conversion precedes the offset conversion in retail.
 RadarFloat(base,kIconWidth,0.0345);
 sub_821C31A8(icon,base);
 close(RadarFloat(base,kWrapRight)+RadarFloat(base,kIconWidth)+RadarFloat(base,kIconOffset),final_edge);
 std::cout<<"PASS icon width and padding stay inside the same final wrap edge without changing font size or HUD definitions\n";
 setup(true);
 for (const auto& fixture:kFixtures) {
   presentation.physical_output_width=fixture.w;presentation.physical_output_height=fixture.h;
   presentation.output_width=fixture.w;presentation.output_height=fixture.h;
   presentation.safe_area_width=int(fixture.w);presentation.safe_area_height=int(fixture.h);
#if !defined(GTA4_TOUCH_LEGACY_HOST)
   ui.transform=a::Layout({fixture.w,fixture.h},{0,0});
#endif
   invoke();
   const auto result=ui.transform.Map(a::Point{RadarFloat(base,kPosition),RadarFloat(base,kPositionY)});
#if defined(GTA4_TOUCH_LEGACY_HOST)
   close(result.x,fixture.legacy_x);
   close(result.y,fixture.legacy_y);
#else
   close(result.x,fixture.modern_x);
   close(result.y,fixture.modern_y);
#endif
 }
 std::cout<<"PASS Python-derived phone, tablet, portrait and desktop fixtures relocate before native measurement in both host pipelines\n";
 setup(true);
 auto& view=visual.layout.viewport;
 view.valid=view.focused=true;view.output_width=1280;view.output_height=720;
 auto& button=visual.layout.controls[0];visual.layout.control_count=1;
 button.visible=true;button.kind=ContextTouchControlKind::kNativeButton;
 button.center_x=400;button.center_y=80;button.radius=40;
 invoke();
 const auto avoids_button=[&] {
   assert(RadarFloat(base,kWrapRight)<kButtonLeft || RadarFloat(base,kPosition)>kButtonRight ||
          RadarFloat(base,kPositionY)>kButtonBottom);
 };
 avoids_button();
 const auto beside=a::TouchHelpArea({.048,.044,.16875,.255},{0,0,1,1},{.054,.055},.225,{1280,720},
                                  std::array{a::Rect{.16,.04,.30,.20}});
 assert(beside && (beside->left>.30 || beside->top>.20));
 auto outgoing=std::make_shared<ContextTouchLayout>(visual.layout);
 visual.layout.control_count=0;visual.outgoing_layout=outgoing;visual.outgoing_alpha=0.5;
 invoke();avoids_button();
 visual.outgoing_layout.reset();invoke();assert(RadarFloat(base,kWrapRight)>kButtonLeft);
 visual.layout.control_count=1;button.kind=ContextTouchControlKind::kActivitySurface;
 button.minimum_x=360;button.minimum_y=0;button.maximum_x=1280;button.maximum_y=720;
 invoke();assert(RadarFloat(base,kWrapRight)<kButtonLeft);
 assert(!a::TouchHelpArea({}, {0,0,1,1},{0,0},.3,{1280,720}));
 std::cout<<"PASS current, custom and outgoing control layouts constrain wrapping without moving controls or retaining stale bounds\n";
 munmap(base,kMemorySize);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    hooks = (SRC / 'gta4_touch_guest_hooks.cpp').read_text()
    controls = (SRC / 'input/context_touch_controls.cpp').read_text()
    layout = (SRC / 'input/context_touch_layout.cpp').read_text()
    original = (SDK / 'gta4-recomp/generated/gta4_recomp.4.cpp').read_text()
    stack = 0x200000
    constants = {'kMemorySize':1 << 32, 'kStack':stack,
                 'kPosition':stack+168, 'kPositionY':stack+172,
                 'kWrap':stack+160, 'kWrapRight':stack+164,
                 'kIconWidth':stack+192, 'kIconOffset':stack+200,
                 'kNativeScale':((-32254 << 16)-26308)&0xffffffff,
                 'kRadarY':0x82AA0A88+4, 'kRadarWidth':0x82AA0A88+8,
                 'kRadarHeight':0x82AA0A88+12}
    source = '#include <cstdint>\n' + '\n'.join(f'constexpr uint64_t {k}={v}ull;' for k,v in constants.items())
    source += f'\nconstexpr double kRadarRight={.048+.12075}, kExpectedLeft={.048+.12075+720*.016/1280}, kButtonLeft={(400-40)/1280}, kButtonRight={(400+40)/1280}, kButtonBottom={(80+40)/720};\n'
    source += 'struct Fixture {uint32_t w,h;double modern_x,legacy_x,modern_y,legacy_y;};\nconstexpr Fixture kFixtures[]={\n'
    for w,h in [(1280,720),(1979,1280),(1024,768),(2400,1080),(360,800)]:
        gap=min(w,h)*.016/w
        gap_y=min(w,h)*.016/h
        drop=min(w,h)*0.05/h
        modern_y=max(gap_y,.055*min(1,(w/h)/(16/9)))+drop
        legacy_y=max(gap_y,.055)+drop
        source += f'{{{w},{h},{(.048+.12075)*min(1,(16/9)/(w/h))+gap},{.048+.12075+gap},{modern_y},{legacy_y}}},\n'
    source += '};\n'
    regs=' '.join(f'PPCRegister {prefix}{i}{{}};' for prefix in ['r','f'] for i in range(32))
    source += PREFIX.replace('REGISTERS',regs)
    source += re.search(r'constexpr uint32_t kAuthoredRadarRect = [^;]+;',hooks)[0]+'\n'
    for signature in ['uint32_t RadarRead(', 'double RadarFloat(', 'void RadarFloat(',
                      'gta4::aspect::Rect RadarViewportRect(', 'bool RadarRectValid(']:
        source += function(hooks,signature)
    source += function(controls,'bool ContextTouchHudLayoutActive(')
    source += function(layout,'bool ValidHudBounds(')
    source += function(layout,'std::optional<ContextTouchHudBounds> MapContextTouchHudBounds(')
    source += 'struct HelpWrapState {uint32_t stack=0,wrap=0;double right=0;};\nthread_local HelpWrapState help_wrap;\n'
    source += function(hooks,'void PlaceTouchHelp(')+'}\n'
    source += function(original,'DEFINE_REX_FUNC(sub_821C31A8)')
    source += function(hooks,'extern "C" void sub_821C31A8(')+HARNESS
    cpp=args.output/'touch_help_runtime.cpp'
    cpp.write_text(source)
    for mode,flags in [('modern',[]),('legacy',['-DGTA4_TOUCH_LEGACY_HOST=1'])]:
        binary=args.output/mode
        subprocess.run(['clang++','-std=c++23','-O0','-g','-I'+str(SRC),'-I'+str(SDK/'include'),
                        *flags,str(cpp),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)


if __name__ == '__main__':
    main()
