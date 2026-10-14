#!/usr/bin/env python3
"""Offline Vulkan temporal variants of the complete current title shader inventory.

Original shader bodies, alpha tests, material calculations and discard paths are
retained. Vertex shaders evaluate the immutable previous constant bank in the same
invocation. No run-time compiler or alternate rendering of the scene is involved.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/metal"))
import shader_archive as stock

MAGIC = b"LRVTEMP1"
HEADER = struct.Struct("<8sII")
RECORD = struct.Struct("<QIII")
VERSION = 1
MAX_RECORDS, MAX_BYTES = 16384, 268435456
VERTEX, OVERRIDE, LATE = 1, 2, 4

PUSH_DECLARATION = "[[vk::push_constant]] ConstantBuffer<PushConstants> g_PushConstants;"
CONTRACT = """
struct LibertyTemporalPush {
  uint64_t VertexShaderConstants, PixelShaderConstants, SharedConstants;
  uint64_t PreviousVertexShaderConstants, PreviousSharedConstants, DrawParameters;
};
[[vk::push_constant]] ConstantBuffer<LibertyTemporalPush> libertyPush;
// DXC maps static globals to invocation-private storage. Shared sampling and
// vertex helper functions therefore see the same selected bank as the body.
static PushConstants g_PushConstants;
struct LibertyTemporalDraw {
  float2 inputExtent, jitterClip, previousJitterClip;
  uint validHistory, reactive, uiMode, padding0, padding1, padding2;
};
void liberty_temporal_current() {
  g_PushConstants.VertexShaderConstants=libertyPush.VertexShaderConstants;
  g_PushConstants.PixelShaderConstants=libertyPush.PixelShaderConstants;
  g_PushConstants.SharedConstants=libertyPush.SharedConstants;
}
"""
VARYINGS = """
#ifdef __spirv__
  [[vk::location(18)]] float4 libertyCurrentClip : TEXCOORD16;
  [[vk::location(19)]] float4 libertyPreviousClip : TEXCOORD17;
#endif
"""
OUTPUTS = """
#ifdef __spirv__
  float2 libertyMotion : SV_Target4;
  float libertyReactive : SV_Target5;
  float libertyPreviousDepth : SV_Target6;
#endif
"""
DEPTH_SHADER = "struct PushConstants {uint64_t VertexShaderConstants,PixelShaderConstants,SharedConstants;};\n" + CONTRACT + """
struct DepthInput {
  [[vk::location(18)]] float4 current : TEXCOORD16;
  [[vk::location(19)]] float4 previous : TEXCOORD17;
};
struct DepthOutput {float2 motion:SV_Target4;float reactive:SV_Target5;float previous:SV_Target6;};
DepthOutput shaderMain(DepthInput input) {
  LibertyTemporalDraw temporal=vk::RawBufferLoad<LibertyTemporalDraw>(libertyPush.DrawParameters,4);
  bool valid=temporal.validHistory&&all(isfinite(input.current))&&all(isfinite(input.previous))&&
    input.current.w>1.0e-6f&&input.previous.w>1.0e-6f;
  float2 velocity=valid?(input.previous.xy/input.previous.w-input.current.xy/input.current.w)*
    temporal.inputExtent*float2(0.5f,-0.5f):float2(0,0);
  valid=valid&&all(isfinite(velocity))&&all(abs(velocity)<=65504.0f);
  float z=valid?input.previous.z/input.previous.w:-1.0f;
  DepthOutput result;
  result.motion=valid?velocity:float2(0,0);
  result.reactive=(!valid||temporal.reactive)?1.0f:0.0f;
  result.previous=isfinite(z)&&z>=0&&z<=1?z:-1.0f;
  return result;
}
"""


def extend_block(source: str, marker: str, addition: str, locations: bool = False) -> str:
    start, end = stock._balanced_block(source, marker)
    body = source[start:end]
    if locations:
        # DXC requires a consistent explicit location policy for this signature.
        # The stock interface has TEXCOORD0..15 followed by COLOR0..1.
        def location(match):
            kind, index = match.group(2), int(match.group(3))
            if index >= (16 if kind == "TEXCOORD" else 2):
                raise ValueError("title varying occupies reserved temporal locations")
            slot = index if kind == "TEXCOORD" else 16 + index
            return f"[[vk::location({slot})]] " + match.group(0)
        body = re.sub(r"\b(float[1-4]?)\s+\w+\s*:\s*(TEXCOORD|COLOR)(\d+)\s*;", location, body)
    brace = body.index("{")
    body = body[:brace + 1] + addition + body[brace + 1:]
    return source[:start] + body + source[end:]


def transform_hlsl(source: str, vertex: bool) -> str:
    if "LibertyTemporalPush" in source:
        raise ValueError("shader is already temporal")
    if source.count(PUSH_DECLARATION) != 1:
        raise ValueError("shader does not expose exactly one original push-constant contract")
    source = source.replace(PUSH_DECLARATION, CONTRACT)
    source = extend_block(source, "struct Interpolators", VARYINGS, locations=True)
    result_type = "Interpolators" if vertex else "PixelShaderOutput"
    marker = result_type + " shaderMain("
    if source.count(marker) != 1:
        raise ValueError("shader entry point does not match its declared title interface")
    signature = source[source.index(marker):source.index("{",source.index(marker))]
    source = source.replace(marker, result_type + " liberty_temporal_original(")
    early = "[earlydepthstencil]" in source
    source = source.replace("[earlydepthstencil]", "")
    if vertex:
        wrapper = """
#ifdef __spirv__
Interpolators shaderMain(VertexShaderInput input) {
  liberty_temporal_current();
  LibertyTemporalDraw temporal=vk::RawBufferLoad<LibertyTemporalDraw>(libertyPush.DrawParameters,4);
  Interpolators result=liberty_temporal_original(input);
  result.libertyCurrentClip=result.oPos;
  result.libertyCurrentClip.xy-=vk::RawBufferLoad<float2>(libertyPush.SharedConstants+552)*result.oPos.w;
  result.oPos.xy+=temporal.jitterClip*result.oPos.w;
  result.libertyPreviousClip=result.libertyCurrentClip;
  if(temporal.validHistory) {
    g_PushConstants.VertexShaderConstants=libertyPush.PreviousVertexShaderConstants;
    g_PushConstants.SharedConstants=libertyPush.PreviousSharedConstants;
    Interpolators previous=liberty_temporal_original(input);
    result.libertyPreviousClip=previous.oPos;
    result.libertyPreviousClip.xy-=vk::RawBufferLoad<float2>(libertyPush.PreviousSharedConstants+552)*previous.oPos.w;
    float2 currentRange=vk::RawBufferLoad<float2>(libertyPush.SharedConstants+1264);
    float2 previousRange=vk::RawBufferLoad<float2>(libertyPush.PreviousSharedConstants+1264);
    if(!all(currentRange==float2(0,1))||!all(previousRange==float2(0,1)))
      result.libertyPreviousClip.z=-result.libertyPreviousClip.w;
  }
  return result;
}
#endif
"""
    else:
        source = extend_block(source, "struct PixelShaderOutput", OUTPUTS)
        wrapper = "\n#ifdef __spirv__\n" + ("[earlydepthstencil]\n" if early else "") + """
PixelShaderOutput shaderMain(Interpolators input,bool iFace : SV_IsFrontFace) {
  liberty_temporal_current();
  LibertyTemporalDraw temporal=vk::RawBufferLoad<LibertyTemporalDraw>(libertyPush.DrawParameters,4);
  PixelShaderOutput result=liberty_temporal_original(input,iFace);
  bool valid=temporal.validHistory&&all(isfinite(input.libertyCurrentClip))&&all(isfinite(input.libertyPreviousClip))&&
    input.libertyCurrentClip.w>1.0e-6f&&input.libertyPreviousClip.w>1.0e-6f;
  float2 velocity=float2(0,0);
  if(valid)velocity=(input.libertyPreviousClip.xy/input.libertyPreviousClip.w-
                    input.libertyCurrentClip.xy/input.libertyCurrentClip.w)*temporal.inputExtent*float2(0.5f,-0.5f);
  valid=valid&&all(isfinite(velocity))&&all(abs(velocity)<=65504.0f);
  result.libertyMotion=valid?velocity:float2(0,0);
  result.libertyReactive=(!valid||temporal.reactive)?1.0f:0.0f;
  float z=valid?input.libertyPreviousClip.z/input.libertyPreviousClip.w:-1.0f;
  result.libertyPreviousDepth=isfinite(z)&&z>=0&&z<=1?z:-1.0f;
  return result;
}
#endif
"""
        if "SV_IsFrontFace" not in signature:
            wrapper=wrapper.replace("liberty_temporal_original(input,iFace)","liberty_temporal_original(input)")
    return source + wrapper


def transform_glsl(source: str) -> str:
    marker = "    uint64_t _m2;\n} _10;"
    if source.count(marker) != 1 or source.count("void main()") != 1:
        raise ValueError("GLSL override does not match its verified push/main contract")
    source = source.replace(marker, "    uint64_t _m2, libertyPreviousVertex, libertyPreviousShared, libertyDraw;\n} _10;")
    source = source.replace("void main()", "void liberty_temporal_original()")
    return source + """
layout(location=18) in vec4 libertyCurrentClip;
layout(location=19) in vec4 libertyPreviousClip;
layout(location=4) out vec2 libertyMotion;
layout(location=5) out float libertyReactive;
layout(location=6) out float libertyPreviousDepth;
void main() {
  liberty_temporal_original();
  bool valid=uintPointer(_10.libertyDraw+24ul).value!=0u&&
    !any(isnan(libertyCurrentClip))&&!any(isinf(libertyCurrentClip))&&
    !any(isnan(libertyPreviousClip))&&!any(isinf(libertyPreviousClip))&&
    libertyCurrentClip.w>1.0e-6&&libertyPreviousClip.w>1.0e-6;
  vec2 velocity=vec2(0);
  if(valid)velocity=(libertyPreviousClip.xy/libertyPreviousClip.w-libertyCurrentClip.xy/libertyCurrentClip.w)*
    vec2Pointer(_10.libertyDraw).value*vec2(0.5,-0.5);
  valid=valid&&!any(isnan(velocity))&&!any(isinf(velocity))&&all(lessThanEqual(abs(velocity),vec2(65504)));
  libertyMotion=valid?velocity:vec2(0);
  libertyReactive=(!valid||uintPointer(_10.libertyDraw+28ul).value!=0u)?1.0:0.0;
  float z=valid?libertyPreviousClip.z/libertyPreviousClip.w:-1.0;
  libertyPreviousDepth=!isnan(z)&&!isinf(z)&&z>=0&&z<=1?z:-1.0;
}
"""


def validate_push_contract(words: tuple[int, ...]) -> None:
    """Reflect the six-address ABI from SPIR-V, including each byte offset."""
    integers, structures, pointers, offsets, pushes = {}, {}, {}, {}, []
    cursor = 5
    while cursor < len(words):
        size, opcode = words[cursor] >> 16, words[cursor] & 0xFFFF
        row = words[cursor:cursor + size]
        if opcode == 21:  # OpTypeInt
            integers[row[1]] = tuple(row[2:])
        elif opcode == 30:  # OpTypeStruct
            structures[row[1]] = tuple(row[2:])
        elif opcode == 32:  # OpTypePointer
            pointers[row[1]] = (row[2], row[3])
        elif opcode == 72 and row[3] == 35:  # OpMemberDecorate Offset
            offsets[row[1], row[2]] = row[4]
        elif opcode == 59 and row[3] == 9:  # OpVariable PushConstant
            pushes.append(row[1])
        cursor += size
    if len(pushes) != 1 or pointers.get(pushes[0], (None,))[0] != 9:
        raise ValueError("temporal shader does not expose exactly one push-constant block")
    structure = pointers[pushes[0]][1]
    members = structures.get(structure, ())
    if len(members) != 6 or any(integers.get(member) != (64, 0) for member in members):
        raise ValueError("temporal push constants are not six unsigned buffer addresses")
    if tuple(offsets.get((structure, member)) for member in range(6)) != tuple(range(0, 48, 8)):
        raise ValueError("temporal push-constant byte offsets disagree with shader_contract.h")


def write_archive(records: list[tuple[int,int,bytes]], path: Path) -> None:
    if not 0 < len(records) <= MAX_RECORDS:
        raise ValueError("invalid temporal shader record count")
    records.sort(key=lambda row: (row[0], row[1]))
    if len({(row[0], row[1]) for row in records}) != len(records):
        raise ValueError("duplicate temporal shader identity")
    offset = HEADER.size + RECORD.size * len(records)
    table, payload = bytearray(), bytearray()
    for shader_hash, flags, data in records:
        if len(data) % 4 or data[:4] != struct.pack("<I", 0x07230203):
            raise ValueError("invalid SPIR-V payload")
        table += RECORD.pack(shader_hash, flags, offset // 4, len(data) // 4)
        payload += data
        offset += len(data)
        if offset > MAX_BYTES:
            raise ValueError("temporal shader archive exceeds its size bound")
    stock._atomic_write(path, HEADER.pack(MAGIC, VERSION, len(records)) + table + payload)


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--jobs",type=int,default=4)
    parser.add_argument("--dxc",type=Path,required=True,
                        help="host DXC executable selected by the native renderer CMake configuration")
    parser.add_argument("--dxc-library-path",type=Path,
                        help="matching DXC shared-library directory (Windows defaults to the executable directory)")
    parser.add_argument("--spirv-val",default="spirv-val")
    parser.add_argument("--glslang",default="glslangValidator")
    parser.add_argument("--zstd-library")
    args=parser.parse_args()
    if not 1<=args.jobs<=4:parser.error("jobs must be in [1,4]")
    args.dxc=args.dxc.resolve()
    if not args.dxc.is_file():parser.error(f"DXC executable is missing: {args.dxc}")
    if sys.platform=="win32":
        # Windows loads this bundled driver's DLL from its executable directory;
        # LD_LIBRARY_PATH cannot select a different compiler as on Unix hosts.
        if args.dxc_library_path and args.dxc_library_path.resolve()!=args.dxc.parent:
            parser.error("Windows DXC must use dxcompiler.dll beside the selected executable")
        args.dxc_library_path=args.dxc.parent
    elif args.dxc_library_path is None:
        parser.error("pass the native renderer's selected --dxc-library-path")
    args.dxc_library_path=args.dxc_library_path.resolve()
    args.spirv_val=shutil.which(args.spirv_val)
    args.glslang=shutil.which(args.glslang)
    if not args.spirv_val or not args.glslang:
        parser.error("the selected spirv-val and glslang executables must exist")
    cache_path=ROOT/"LibertyRecompLib/shader/shader_cache.cpp"
    if args.output.resolve()==cache_path.resolve():
        parser.error("temporal variants are additive; the stock shader cache is input only")
    stock_before=hashlib.sha256(cache_path.read_bytes()).hexdigest()
    args.work.mkdir(parents=True,exist_ok=True)
    recovery=stock.build_from_repo(ROOT,args.work/"stock.metadata",args.work/"stock",emit_only=True,
                                   zstd_library=args.zstd_library)
    override_root=ROOT/"LibertyRecompLib/shader_overrides"
    compiler=stock._load_module(ROOT/"glue/rexglue-sdk-main/tools/gta4_shader_override_compiler.py","liberty_temporal_override_compiler")
    overrides=compiler.load_manifest(override_root/"manifest.json",override_root)
    reader=stock._load_module(ROOT/"tools/validate_shader_cache_candidate.py","liberty_temporal_inventory")
    entries=reader.parse_entries(cache_path.read_text(),cache_path)
    builtin=args.work/"depth_only.hlsl";builtin.write_text(DEPTH_SHADER)
    pending=[(0,0,"depth",builtin,[],args.work)]
    for entry in entries:
        primary=args.work/"stock/sources"/f"{entry.shader_hash:016X}.early.metal"
        stage=stock.source_metadata(primary.read_text())[0]
        for late in ([False,True] if entry.late_spirv_size else [False]):
            source=args.work/"stock/sources"/f"{entry.shader_hash:016X}.{'late' if late else 'early'}.metal"
            if not source.exists():
                raise ValueError(f"required stock variant is missing: {source}")
            pending.append((entry.shader_hash,(VERTEX if stage==stock.VERTEX else 0)|(LATE if late else 0),
                            "hlsl",source,[],source.parent))
    for entry in overrides:
        for late in ([False,True] if compiler.requires_late_fragment_tests(entry) else [False]):
            pending.append((entry["hash"],OVERRIDE|(VERTEX if entry["stage_name"]=="vertex" else 0)|(LATE if late else 0),
                            entry["language"],entry["source"],entry["defines"],entry["source"].parent))
    compiler_hash=hashlib.sha256(args.dxc.read_bytes()).hexdigest()
    # The executable is a driver; CMake deliberately selects a newer shared
    # compiler on macOS because bundled DXC 1.8 crashes on original Bink PS.
    compiler_libraries={}
    for library_name in ("libdxcompiler.dylib","libdxcompiler.so","dxcompiler.dll"):
        library=args.dxc_library_path/library_name
        if library.is_file():
            compiler_libraries[library_name]=hashlib.sha256(library.read_bytes()).hexdigest()
    if not compiler_libraries:
        parser.error(f"no DXC shared compiler found in {args.dxc_library_path}")
    compiler_identity=compiler_hash+json.dumps(compiler_libraries,sort_keys=True)
    glslang_hash=hashlib.sha256(Path(args.glslang).read_bytes()).hexdigest()
    generator_hash=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    # Includes participate in cache identity, even when only a helper changed.
    includes_hash=hashlib.sha256()
    for path in sorted(override_root.rglob("*")):
        if path.is_file() and path.suffix in (".hlsl",".hlsli",".h",".glsl"):
            includes_hash.update(path.read_bytes())
    include_digest=includes_hash.hexdigest()
    environment=compiler._compiler_environment(str(args.dxc_library_path))
    compiled_dir=args.work/"compiled";compiled_dir.mkdir(exist_ok=True)
    preprocess_dir=args.work/"preprocessed";preprocess_dir.mkdir(exist_ok=True)
    def compile_item(item):
        shader_hash,flags,language,path,defines,include_dir=item
        text=path.read_text()
        if flags&LATE:
            defines=[*defines,"XENOS_RECOMP_LATE_FRAGMENT_TESTS"]
        if language=="hlsl" and flags&OVERRIDE:
            # Some exact overrides keep their ABI and entry point in guarded
            # includes. Let the same pinned compiler resolve those branches;
            # textual include concatenation can change conditional semantics.
            preprocess_key=hashlib.sha256((compiler_identity+include_digest+str(flags)+str(defines)+
                                           str(path)+text).encode()).hexdigest()
            preprocessed=preprocess_dir/(preprocess_key+".hlsl")
            if not preprocessed.exists():
                command=[str(args.dxc),"-spirv","-HV","2021","-T",
                         "vs_6_0" if flags&VERTEX else "ps_6_0","-E","shaderMain",
                         "-DGTA4_RECOMP","-I",str(override_root),"-I",str(include_dir)]
                if not flags&VERTEX:command.append("-DXENOS_RECOMP_PIXEL_SHADER")
                command += ["-D"+value for value in defines]
                command += ["-P","-Fi",str(preprocessed),str(path)]
                result=subprocess.run(command,env=environment,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
                if result.returncode:
                    preprocessed.unlink(missing_ok=True)
                    raise RuntimeError(f"{shader_hash:016X} preprocessing {path}: {result.stdout}")
            text=preprocessed.read_text()
        try:
            transformed=transform_hlsl(text,bool(flags&VERTEX)) if language=="hlsl" else transform_glsl(text) if language=="glsl" else text
        except ValueError as error:
            raise ValueError(f"{shader_hash:016X} flags={flags} source={path}: {error}") from error
        if flags&LATE:
            transformed=transformed.replace("[earlydepthstencil]","")
        # Content, compiler, stage/defines and includes determine HLSL output.
        # Retain valid compiled stock modules when only generator plumbing
        # changes. GLSL's exported shaderMain contract has its own revision.
        identity=compiler_identity+include_digest+str(flags)+str(defines)+transformed
        if language=="glsl":identity="glsl-shaderMain-v1"+glslang_hash+identity
        key=hashlib.sha256(identity.encode()).hexdigest()
        output=compiled_dir/(key+".spv")
        source=compiled_dir/(key+(".glsl" if language=="glsl" else ".hlsl"))
        if not output.exists():
            source.write_text(transformed)
            if language!="glsl":
                command=[str(args.dxc),"-spirv","-fspv-target-env=vulkan1.0","-HV","2021","-T",
                         "vs_6_0" if flags&VERTEX else "ps_6_0","-E","shaderMain","-all-resources-bound",
                         "-fvk-use-dx-layout","-O3","-Qstrip_debug","-WX","-DGTA4_RECOMP"]
                command += ["-fvk-invert-y"] if flags&VERTEX else ["-DXENOS_RECOMP_PIXEL_SHADER"]
                command += ["-I",str(override_root),"-I",str(include_dir)]
                command += ["-D"+value for value in defines]
                command += ["-Fo",str(output),str(source)]
            else:
                command=[args.glslang,"-V","--target-env","vulkan1.0","-S","frag",
                         "--source-entrypoint","main","-e","shaderMain","-g0","-Os"]
                command += ["-D"+value for value in defines]
                command += ["-o",str(output),str(source)]
            result=subprocess.run(command,env=environment,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            if result.returncode:
                output.unlink(missing_ok=True)
                raise RuntimeError(f"{shader_hash:016X} flags={flags} compiler_exit={result.returncode}: {result.stdout}")
        validation=subprocess.run([args.spirv_val,"--target-env","vulkan1.2",str(output)],text=True,
                                  stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if validation.returncode:raise RuntimeError(f"{shader_hash:016X}: {validation.stdout}")
        payload=output.read_bytes()
        words,interface=compiler.validate_spirv(payload,0 if flags&VERTEX else 4,"shaderMain",
                                               forbid_early_fragment_tests=bool(flags&LATE))
        validate_push_contract(words)
        if flags&VERTEX:
            if not {18,19}.issubset(interface.output_locations):
                raise ValueError(f"{shader_hash:016X}: missing current/previous vertex positions")
        elif not {4,5,6}.issubset(interface.output_locations) or not {18,19}.issubset(interface.input_locations):
            raise ValueError(f"{shader_hash:016X}: missing motion/reactive/previous-depth interface")
        return shader_hash,flags,payload
    records=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for record in pool.map(compile_item,pending):
            records.append(record)
            if len(records)%100==0:print(f"validated temporal shaders {len(records)}/{len(pending)}",flush=True)
    expected={(item[0],item[1]) for item in pending}
    if len(expected)!=len(pending) or {(h,flags) for h,flags,_ in records}!=expected:
        raise ValueError("temporal variants do not exactly cover the complete stock/override inventory")
    if hashlib.sha256(cache_path.read_bytes()).hexdigest()!=stock_before or recovery["stock_cache_sha256"]!=stock_before:
        raise ValueError("stock cache changed during temporal archive generation; preserving the prior archive")
    write_archive(records,args.output)
    manifest={"version":VERSION,"stock_cache_sha256":recovery["stock_cache_sha256"],
              "dxc_sha256":compiler_hash,"dxc_libraries_sha256":compiler_libraries,
              "glslang_sha256":glslang_hash,
              "generator_sha256":generator_hash,"override_sources_sha256":include_digest,
              "entries":len(records),"stock_shaders":len(entries),"override_shaders":len(overrides),
              "archive_sha256":hashlib.sha256(args.output.read_bytes()).hexdigest(),
              "records":[{"hash":f"{h:016X}","flags":flags,"bytes":len(data),
                          "sha256":hashlib.sha256(data).hexdigest()} for h,flags,data in records]}
    stock._atomic_write(args.output.with_suffix(args.output.suffix+".json"),(json.dumps(manifest,indent=2)+"\n").encode())
    print(json.dumps({key:value for key,value in manifest.items() if key!="records"},indent=2))
    return 0

if __name__=="__main__":
    raise SystemExit(main())
