#!/usr/bin/env python3
"""Exercise E routing and original compiled input consumers, without pseudocode.

Extract production bindings, merges and generated PPC functions verbatim. Only
host input snapshots, control selection and multiplayer state are test doubles.
"""
from pathlib import Path
import argparse
import re
import subprocess

SDK = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    end = source.index('\n}', start) + len('\n}')
    return source[start:end]


def declaration(source, signature):
    start = source.index(signature)
    end = source.index('\n};', start) + len('\n};')
    return source[start:end]


PREFIX = r'''
#include <array>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sys/mman.h>
#include <rex/input/mnk/encoded_action.h>
#include "gta4_input_action_routing.h"
union PPCRegister { uint64_t u64; int64_t s64; uint32_t u32; int32_t s32; uint8_t u8; };
struct Xer { uint32_t ca = 0; };
struct Cr {
  bool eq=false, lt=false, gt=false;
  template<class T> void compare(T a, T b, const Xer&) { eq=a==b; lt=a<b; gt=a>b; }
};
struct PPCContext { REGISTERS uint64_t lr=0; PPCRegister ctr{}; Xer xer; Cr cr6; };
template<class T> T Read(const uint8_t* p) { T v; std::memcpy(&v,p,sizeof(v)); return std::byteswap(v); }
template<class T> void Write(uint8_t* p,T v) { v=std::byteswap(v); std::memcpy(p,&v,sizeof(v)); }
#define REX_FUNC_PROLOGUE()
#define DEFINE_REX_FUNC(n) void __imp__##n(PPCContext& ctx,uint8_t* base)
#define REX_LOAD_U8(a) (*(base+uint32_t(a)))
#define REX_LOAD_U32(a) Read<uint32_t>(base+uint32_t(a))
#define REX_LOAD_U64(a) Read<uint64_t>(base+uint32_t(a))
#define REX_STORE_U8(a,v) (*(base+uint32_t(a))=uint8_t(v))
#define REX_STORE_U32(a,v) Write<uint32_t>(base+uint32_t(a),uint32_t(v))
#define REX_STORE_U64(a,v) Write<uint64_t>(base+uint32_t(a),uint64_t(v))
uint32_t active_control=kControl;
bool multiplayer=false, multiplayer_input_blocked=false;
void sub_821B41E8(PPCContext& ctx,uint8_t*) { ctx.r3.u64=active_control; }
void sub_821B42B8(PPCContext& ctx,uint8_t*) { ctx.r3.u64=kInterfaceControl; }
void sub_821B4318(PPCContext& ctx,uint8_t*) { ctx.r3.u64=kInterfaceControl; }
void sub_825D9D10(PPCContext& ctx,uint8_t*) { ctx.r3.u64=multiplayer; }
void sub_826CBDC0(PPCContext& ctx,uint8_t*) { ctx.r3.u64=multiplayer_input_blocked; }
extern "C" void sub_825D1308(PPCContext&,uint8_t*);
extern "C" void sub_825D1468(PPCContext&,uint8_t*);
namespace gta4::input {
using rex::ui::VirtualKey;
struct NativeInputState { std::array<uint8_t,256> keys{}; uint32_t user_index=0; };
struct InputEpoch {
  NativeInputState state{};
  std::array<uint8_t,256> pressed_keys{};
  bool valid=true, frontend_active=false, phone_visible=false, helicopter_controls=false;
};
InputEpoch epoch;
uint32_t script_thread=1;
InputEpoch ReadEpoch() { return epoch; }
uint32_t ReadTouchScriptThread(uint8_t*) { return script_thread; }
bool GTA4_TouchVirtualKeyDown(size_t) { return false; }
uint8_t LoadU8(uint8_t* base,uint32_t a) { return base[a]; }
uint32_t LoadU32(uint8_t* base,uint32_t a) { return REX_LOAD_U32(a); }
void StoreU8(uint8_t* base,uint32_t a,uint8_t v) { base[a]=v; }
enum class ScriptInputQueryTraceKind { kRawHeld,kRawPressed };
void TraceUpScriptInputQuery(uint8_t*,ScriptInputQueryTraceKind,uint32_t,uint32_t,uint32_t,uint32_t) {}
'''

SUFFIX = r'''
int main() {
  using namespace gta4::input;
  auto* base=static_cast<uint8_t*>(mmap(nullptr,kMapBytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0));
  assert(base!=MAP_FAILED);
  const auto key=static_cast<size_t>(VirtualKey::kE);
  const auto query=[&](auto native,uint32_t group,uint32_t action) {
    PPCContext ctx{}; ctx.r1.u64=kStack; ctx.r3.u64=group; ctx.r4.u64=action;
    native(ctx,base); return ctx.r3.u32;
  };
  epoch.state.keys[key]=1; epoch.pressed_keys[key]=1;
  assert(!IsKeyboardControllerKey(VirtualKey::kE,false));
  assert(!IsKeyboardControllerKey(VirtualKey::kE,true));
  for (uint8_t polarity : {uint8_t{0},uint8_t{255}}) {
    for (uint32_t control : {kControl,kInterfaceControl}) {
      REX_STORE_U8(control+kPickup,polarity);
      REX_STORE_U8(control+kPickupCurrent,polarity);
      REX_STORE_U8(control+kPickupPrevious,polarity);
      bool found=false;
      for (const auto& binding:kButtonBindings) {
        if (binding.key!=VirtualKey::kE) continue;
        if (binding.action==Action::kPickup) {
          assert(IsNativeActionDown(epoch,binding.key));
          assert(MergeButton(base,control,binding.action,kPressed));
          found=true;
        } else {
          assert(binding.action==Action::kFrontendY);
          assert(!ShouldInjectKeyboardInterfaceAction(uint32_t(binding.action),binding.key,{}));
          assert(ShouldInjectKeyboardInterfaceAction(uint32_t(binding.action),binding.key,{.frontend_active=true}));
        }
      }
      assert(found);
    }
    for (uint32_t group : {0u,1u,2u}) {
      assert(query(__imp__sub_825D1908,group,23)==1);
      assert(query(__imp__sub_825D1980,group,23)==1);
    }
    for (uint32_t control : {kControl,kInterfaceControl}) {
      REX_STORE_U8(control+kPickupPrevious,REX_LOAD_U8(control+kPickupCurrent));
    }
    assert(query(__imp__sub_825D1980,2,23)==0);
    for (uint32_t control : {kControl,kInterfaceControl}) REX_STORE_U8(control+kPickupCurrent,polarity);
    assert(query(__imp__sub_825D1908,2,23)==0);
  }
  std::cout << "PASS production E binding reaches original held/press consumers for gameplay and script groups, both polarities and release\n";
  assert(query(__imp__sub_825D17E8,0,4)==1);
  assert(query(__imp__sub_825D1868,0,4)==1);
  assert(REX_LOAD_U32(kPadLB)==0 && REX_LOAD_U32(kPadLBPrevious)==0);
  for (uint32_t action : {37u,38u,56u,57u,58u,81u}) assert(query(__imp__sub_825D1908,0,action)==0);
  epoch.pressed_keys[key]=0;
  assert(query(__imp__sub_825D17E8,0,4)==1);
  assert(query(__imp__sub_825D1868,0,4)==0);
  epoch.state.keys[key]=0;
  assert(query(__imp__sub_825D17E8,0,4)==0);
  epoch.state.keys[key]=1; epoch.pressed_keys[key]=1;
  for (uint32_t button : {5u,6u,7u,8u,14u,15u,16u,17u}) {
    assert(query(__imp__sub_825D17E8,0,button)==0);
    assert(query(__imp__sub_825D1868,0,button)==0);
  }
  for (uint32_t group : {1u,2u,3u,4u}) {
    assert(query(__imp__sub_825D17E8,group,4)==0);
    assert(query(__imp__sub_825D1868,group,4)==0);
  }
  std::cout << "PASS script E has independent held/press edges, leaves raw LB and vehicle/flight actions untouched, and cannot affect other buttons or users\n";
  for (bool* gate : {&epoch.frontend_active,&epoch.phone_visible}) {
    *gate=true;
    assert(query(__imp__sub_825D17E8,0,4)==0);
    assert(query(__imp__sub_825D1868,0,4)==0);
    *gate=false;
  }
  epoch.valid=false; assert(query(__imp__sub_825D17E8,0,4)==0); epoch.valid=true;
  script_thread=0; assert(query(__imp__sub_825D17E8,0,4)==0); script_thread=1;
  active_control=0; assert(query(__imp__sub_825D17E8,0,4)==0); active_control=kControl;
  epoch.state.user_index=1; assert(query(__imp__sub_825D17E8,0,4)==0); epoch.state.user_index=0;
  REX_STORE_U8(kDisabled,1);
  assert(query(__imp__sub_825D1868,0,4)==0);
  REX_STORE_U8(kDisabled,0);
  multiplayer=true; multiplayer_input_blocked=true;
  assert(query(__imp__sub_825D17E8,0,4)==0);
  assert(query(__imp__sub_825D1868,0,4)==0);
  multiplayer=false; multiplayer_input_blocked=false;
  epoch.state.keys[key]=0; epoch.pressed_keys[key]=0;
  REX_STORE_U32(kPadLB,255);
  assert(query(__imp__sub_825D17E8,0,4)==1);
  assert(query(__imp__sub_825D1868,0,4)==1);
  REX_STORE_U32(kPadLBPrevious,255);
  assert(query(__imp__sub_825D1868,0,4)==0);
  std::cout << "PASS original suppression, focus/menu ownership, script ownership and physical controller behavior are preserved\n";
  munmap(base,kMapBytes);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    hooks = (SDK/'gta4-recomp/src/gta4_input_hooks.cpp').read_text()
    generated = (SDK/'gta4-recomp/generated/gta4_recomp.40.cpp').read_text()
    control, interface, stack = 0x100000, 0x110000, 0x200000
    pad = ((-32077 << 16) - 24808) & 0xffffffff
    pickup = 2328 + 23 * 12
    constants = {'kMapBytes': 1 << 32, 'kControl': control, 'kInterfaceControl': interface,
                 'kStack': stack, 'kPickup': pickup, 'kPickupCurrent': pickup + 2,
                 'kPickupPrevious': pickup + 3, 'kPadLB': pad + 20,
                 'kPadLBPrevious': pad + 100, 'kDisabled': control + 3408}
    text = '#include <cstdint>\n' + '\n'.join(
        f'constexpr uint64_t {name} = {value}ull;' for name, value in constants.items())
    text += '\n' + PREFIX.replace('REGISTERS', ' '.join(f'PPCRegister r{i}{{}};' for i in range(32)))
    for name in ['kActionArrayOffset', 'kActionStride', 'kActionCurrentOffset',
                 'kControlUserIndexOffset', 'kRawContextButtonDisabledOffset', 'kPressed']:
        text += re.search(r'constexpr [^\n]*\b' + name + r' = [^;]+;', hooks)[0] + '\n'
    for name in ['enum class Action', 'struct ButtonBinding', 'constexpr ButtonBinding kButtonBindings[]']:
        text += declaration(hooks, name) + '\n'
    for signature in ['uint32_t ActionAddress(', 'bool IsGameplayAction(', 'bool MergeButton(',
                      'bool IsNativeActionDown(', 'void MergeKeyboardScriptContextButton(']:
        text += function(hooks, signature) + '\n'
    text += '}\n'
    for name in ['sub_825D1308', 'sub_825D1468', 'sub_825D17E8', 'sub_825D1868',
                 'sub_825D1908', 'sub_825D1980']:
        text += function(generated, f'DEFINE_REX_FUNC({name})') + '\n'
    for name in ['sub_825D1308', 'sub_825D1468']:
        text += function(hooks, f'extern "C" void {name}(') + '\n'
    text += SUFFIX
    source = args.output/'keyboard_context_runtime.cpp'
    source.write_text(text)
    binary = args.output/'keyboard_context_runtime'
    subprocess.run(['clang++', '-std=c++23', '-O0', '-g', '-I'+str(SDK/'include'),
                    '-I'+str(SDK/'gta4-recomp/src'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
