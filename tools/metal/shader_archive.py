#!/usr/bin/env python3
"""Build and validate a Metal-only archive from LibertyRecomp's shader cache.

The existing stock cache is input only. Source recovery uses the repository's
validated MTLB source-archive reader. No shader hash is deleted, no Vulkan cache
is regenerated, and Metal compilation takes place only during this build step.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import ctypes
import ctypes.util
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from typing import Callable, Iterable

MAGIC = b"LRMETAL2"
VERSION = 2
HEADER = struct.Struct("<8sIIIIQ32s")
RECORD = struct.Struct("<QIIIIIIQQQQQIIQQ")
ATTRIBUTE = struct.Struct("<IIII")
MAX_BYTES = 256 * 1024 * 1024
MAX_SHADERS = 16384
MAX_ATTRIBUTES = 31
MASK_TEXTURES = (1 << 26) - 1
FLAG_EARLY_TESTS = 1
FLAG_LATE_VARIANT = 2
FLAG_NATIVE_OUTPUT = 4
FLAG_COVERAGE = 8
PIXEL = 0
VERTEX = 1
SCALAR_CODES = {"float": 0, "int": 1, "uint": 2}


@dataclass(frozen=True)
class Attribute:
    index: int
    semantic_location: int
    scalar_type: int
    components: int


@dataclass(frozen=True)
class Shader:
    shader_hash: int
    stage: int
    texture_mask: int
    specialization_mask: int
    outputs: int
    flags: int
    name: str
    attributes: tuple[Attribute, ...]
    early: bytes
    late: bytes = b""


def checked_range(offset: int, length: int, total: int, label: str) -> None:
    if offset < 0 or length < 0 or offset > total or length > total - offset:
        raise ValueError(f"{label}: range is outside the archive")


def validate_shader(shader: Shader) -> None:
    if not 0 < shader.shader_hash < 1 << 64:
        raise ValueError("invalid shader hash")
    if shader.stage not in (PIXEL, VERTEX):
        raise ValueError("invalid shader stage")
    if shader.texture_mask < 0 or shader.texture_mask & ~MASK_TEXTURES:
        raise ValueError("texture mask exceeds the title interface")
    if not 0 <= shader.specialization_mask < 1 << 32:
        raise ValueError("invalid specialization mask")
    if shader.outputs < 0 or shader.outputs & ~31:
        raise ValueError("invalid fragment outputs")
    if shader.stage == VERTEX and (shader.outputs or shader.late or shader.flags):
        raise ValueError("vertex shader has fragment-only metadata")
    if shader.flags & ~(FLAG_EARLY_TESTS | FLAG_LATE_VARIANT | FLAG_NATIVE_OUTPUT | FLAG_COVERAGE):
        raise ValueError("unknown shader flags")
    if bool(shader.late) != bool(shader.flags & FLAG_LATE_VARIANT):
        raise ValueError("late-variant metadata disagrees with payload")
    if shader.flags & FLAG_EARLY_TESTS and shader.outputs & 16:
        raise ValueError("depth-writing fragment shader has early-test metadata")
    if shader.flags & FLAG_COVERAGE and not shader.outputs & 1:
        raise ValueError("coverage output requires color zero")
    encoded_name = shader.name.encode("utf-8")
    if not encoded_name or len(encoded_name) > 4096 or b"\0" in encoded_name:
        raise ValueError("invalid shader name")
    if len(shader.attributes) > MAX_ATTRIBUTES or (shader.stage == PIXEL and shader.attributes):
        raise ValueError("invalid vertex attributes")
    if [a.index for a in shader.attributes] != list(range(len(shader.attributes))):
        raise ValueError("attribute indexes are not dense")
    if len({a.semantic_location for a in shader.attributes}) != len(shader.attributes):
        raise ValueError("duplicate vertex semantic location")
    for a in shader.attributes:
        if not 0 <= a.semantic_location < 64 or a.scalar_type not in SCALAR_CODES.values() or not 1 <= a.components <= 4:
            raise ValueError("unsupported vertex attribute")
    for variant in (shader.early, shader.late):
        if variant and (len(variant) < 4 or variant[:4] != b"MTLB"):
            raise ValueError("variant is not a Metal library")
    if not shader.early:
        raise ValueError("missing primary Metal library")


def build_archive(shaders: Iterable[Shader]) -> bytes:
    records = sorted(shaders, key=lambda x: x.shader_hash)
    if not records or len(records) > MAX_SHADERS:
        raise ValueError("invalid archive shader count")
    if len({s.shader_hash for s in records}) != len(records):
        raise ValueError("duplicate shader hash")
    payload = bytearray(HEADER.size + RECORD.size * len(records))
    table = []

    def append(data: bytes) -> tuple[int, int]:
        padding = (-len(payload)) % 16
        if len(payload) + padding + len(data) > MAX_BYTES:
            raise ValueError("Metal archive exceeds the configured limit")
        payload.extend(b"\0" * padding)
        start = len(payload)
        payload.extend(data)
        return start, len(data)

    for shader in records:
        validate_shader(shader)
        attrs = b"".join(ATTRIBUTE.pack(a.index, a.semantic_location, a.scalar_type, a.components) for a in shader.attributes)
        attrs_offset, attrs_size = append(attrs) if attrs else (0, 0)
        name_offset, name_size = append(shader.name.encode("utf-8"))
        early_offset, early_size = append(shader.early)
        late_offset, late_size = append(shader.late) if shader.late else (0, 0)
        table.append(RECORD.pack(shader.shader_hash, shader.stage, shader.texture_mask,
            shader.specialization_mask, shader.outputs, len(shader.attributes), shader.flags,
            early_offset, early_size, late_offset, late_size, name_offset, name_size, 0,
            attrs_offset, attrs_size))
    for index, record in enumerate(table):
        begin = HEADER.size + index * RECORD.size
        payload[begin:begin + RECORD.size] = record
    digest = hashlib.sha256(payload[HEADER.size:]).digest()
    payload[:HEADER.size] = HEADER.pack(MAGIC, VERSION, HEADER.size, RECORD.size, len(records), len(payload), digest)
    result = bytes(payload)
    parse_archive(result)
    return result


def parse_archive(data: bytes, verify_digest: bool = True) -> tuple[Shader, ...]:
    if len(data) < HEADER.size or len(data) > MAX_BYTES:
        raise ValueError("invalid archive byte size")
    magic, version, header_size, record_size, count, total, digest = HEADER.unpack_from(data)
    if magic != MAGIC or version != VERSION or header_size != HEADER.size or record_size != RECORD.size or total != len(data):
        raise ValueError("invalid archive header")
    if not 0 < count <= MAX_SHADERS:
        raise ValueError("invalid archive count")
    table_end = HEADER.size + count * RECORD.size
    checked_range(HEADER.size, count * RECORD.size, len(data), "index")
    if verify_digest and hashlib.sha256(data[HEADER.size:]).digest() != digest:
        raise ValueError("archive checksum mismatch")
    occupied: list[tuple[int, int]] = []

    def read_payload(offset: int, size: int, label: str, optional: bool = False) -> bytes:
        if not size:
            if not optional or offset:
                raise ValueError(f"{label}: missing or noncanonical empty range")
            return b""
        checked_range(offset, size, len(data), label)
        if offset < table_end or offset % 16:
            raise ValueError(f"{label}: invalid payload placement")
        occupied.append((offset, offset + size))
        return data[offset:offset + size]

    shaders: list[Shader] = []
    previous_hash = 0
    for index in range(count):
        (shader_hash, stage, texture_mask, spec_mask, outputs, attr_count, flags,
         early_offset, early_size, late_offset, late_size, name_offset, name_size,
         reserved, attrs_offset, attrs_size) = RECORD.unpack_from(data, HEADER.size + index * RECORD.size)
        if reserved or shader_hash <= previous_hash:
            raise ValueError("invalid reserved field or non-unique shader order")
        previous_hash = shader_hash
        if attr_count > MAX_ATTRIBUTES or attrs_size != attr_count * ATTRIBUTE.size:
            raise ValueError("invalid attribute record size")
        name = read_payload(name_offset, name_size, "name").decode("utf-8", errors="strict")
        attributes_data = read_payload(attrs_offset, attrs_size, "attributes", True)
        attributes = tuple(Attribute(*values) for values in ATTRIBUTE.iter_unpack(attributes_data))
        shader = Shader(shader_hash, stage, texture_mask, spec_mask, outputs, flags,
                        name, attributes, read_payload(early_offset, early_size, "primary"),
                        read_payload(late_offset, late_size, "late", True))
        validate_shader(shader)
        shaders.append(shader)
    end = table_end
    for start, stop in sorted(occupied):
        if start < end:
            raise ValueError("overlapping shader payloads")
        if start - end >= 16 or any(data[end:start]):
            raise ValueError("noncanonical payload padding")
        end = stop
    if end != len(data):
        raise ValueError("unreferenced archive tail")
    return tuple(shaders)


def _balanced_block(source: str, marker: str) -> tuple[int, int]:
    matches = list(re.finditer(re.escape(marker), source))
    if len(matches) != 1:
        raise ValueError(f"expected one {marker}")
    opening = source.find("{", matches[0].end())
    if opening < 0:
        raise ValueError(f"missing body for {marker}")
    # Recompiler-emitted declarations contain no brace-bearing strings/comments.
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if not depth:
                return opening, index
    raise ValueError(f"unterminated {marker}")


def source_metadata(source: str) -> tuple[int, int, tuple[Attribute, ...]]:
    pixel = bool(re.search(r"\bPixelShaderOutput\s+shaderMain\s*\(", source))
    vertex = bool(re.search(r"\bInterpolators\s+shaderMain\s*\(", source))
    if pixel == vertex:
        raise ValueError("ambiguous shader entry point")
    if pixel:
        start, end = _balanced_block(source, "struct PixelShaderOutput")
        body = source[start:end]
        colors = [int(value) for value in re.findall(r"\[\[color\((\d+)\)\]\]", body)]
        if len(colors) != len(set(colors)) or any(c >= 4 for c in colors):
            raise ValueError("invalid fragment color interface")
        outputs = sum(1 << value for value in colors)
        if "[[depth(any)]]" in body:
            outputs |= 16
        return PIXEL, outputs, ()
    start, end = _balanced_block(source, "struct VertexShaderInput")
    body = source[start:end]
    declarations = list(re.finditer(r"\b(float|uint|int)([1-4]?)\s+\w+\s+\[\[attribute\((\d+)\)\]\]", body))
    if len(declarations) != body.count("[[attribute("):
        raise ValueError("unsupported Metal vertex attribute declaration")
    locations = [int(match.group(3)) for match in declarations]
    if len(locations) != len(set(locations)):
        raise ValueError("duplicate vertex attribute locations")
    ordered = sorted(declarations, key=lambda m: int(m.group(3)))
    attributes = tuple(Attribute(i, int(m.group(3)), SCALAR_CODES[m.group(1)], int(m.group(2) or "1")) for i, m in enumerate(ordered))
    if len(attributes) > MAX_ATTRIBUTES:
        raise ValueError("vertex interface exceeds Metal's available attributes")
    return VERTEX, 0, attributes


def coverage_source(header: str) -> str:
    """Use the actual checked-in CPU coverage oracle, not a second algorithm."""
    includes = re.findall(r"^\s*#include\s+(.+)$", header, re.MULTILINE)
    if includes != ["<cstdint>"]:
        raise ValueError("coverage oracle dependencies changed; review its Metal translation")
    for token in ("ComputeNativeAlphaToMask", "IsNativeAlphaToMaskRequested", "kNativeAlphaToMaskEnable"):
        if token not in header:
            raise ValueError(f"coverage oracle lacks {token}")
    if "std::" in header:
        raise ValueError("coverage oracle now requires a host library")
    guards = re.findall(r"^\s*#ifndef\s+(\w+)\s*$", header, re.MULTILINE)
    definitions = re.findall(r"^\s*#define\s+([^\n]+)", header, re.MULTILINE)
    if len(guards) != 1 or [value.strip() for value in definitions] != guards:
        raise ValueError("unreviewed coverage oracle macro definitions")
    if len(re.findall(r"^\s*#endif\b", header, re.MULTILINE)) != 1:
        raise ValueError("coverage oracle guard structure changed")
    text = re.sub(r"^\s*#(?:ifndef|define|endif|include)\b[^\n]*\n?", "", header, flags=re.MULTILINE)
    if re.search(r"^\s*#", text, re.MULTILINE):
        raise ValueError("unreviewed coverage oracle preprocessor branch")
    text, count = re.subn(r"namespace\s+rex::graphics::gta4_native\s*\{", "namespace liberty_metal_contract {", text)
    if count != 1:
        raise ValueError("coverage oracle namespace changed")
    text = re.sub(r"\buint32_t\b", "uint", text)
    # MSL requires namespace-scope data in the constant address space. Function
    # bodies remain byte-for-byte the CPU coverage oracle after type spelling.
    text = re.sub(r"^constexpr uint (kNativeAlphaToMaskEnable\s*=)", r"constant uint \1", text, flags=re.MULTILINE)
    return text


OUTPUT_HELPERS = r'''
#if defined(__air__)
struct LibertyColorOutput {
    float4 scale;
    float4 minimum;
    float4 maximum;
};
inline float4 libertyColorOutput(float4 value, device const LibertyColorOutput& contract) {
    value *= contract.scale;
    for (uint component = 0; component < 4; ++component) {
        if (contract.minimum[component] <= contract.maximum[component]) {
            if (isnan(value[component])) value[component] = 0.0f;
            value[component] = clamp(value[component], contract.minimum[component], contract.maximum[component]);
        }
    }
    return value;
}
#endif
'''


def prepare_source(source: str, coverage_header: str) -> tuple[str, str, tuple[Attribute, ...], int, int, int]:
    """Translate only the binding/output contract; retain the generated game body."""
    stage, outputs, attributes = source_metadata(source)
    if stage == VERTEX:
        locations = {attribute.semantic_location: attribute.index for attribute in attributes}
        updated = re.sub(r"\[\[attribute\((\d+)\)\]\]", lambda m: f"[[attribute({locations[int(m.group(1))]})]]", source)
        return updated, "", attributes, stage, outputs, 0
    if "libertyColorOutput" in source or "libertySampleMask" in source:
        raise ValueError("source already contains the Metal output contract")
    start, end = _balanced_block(source, "struct PixelShaderOutput")
    body = source[start:end]
    if "[[sample_mask]]" in body:
        raise ValueError("source already declares a sample mask; reconcile rather than duplicate")
    if outputs & 1:
        marker = "#ifdef __air__"
        branch = source.find(marker, start, end)
        if branch < 0:
            raise ValueError("fragment interface lacks its Metal branch")
        at = branch + len(marker)
        source = source[:at] + "\n    uint libertySampleMask [[sample_mask]];" + source[at:]
    # Original shaders may return early from conditional blocks. Apply the
    # output contract at every exit in shaderMain, without changing control flow.
    entry = re.search(r"\bPixelShaderOutput\s+shaderMain\s*\(", source)
    if not entry:
        raise ValueError("missing fragment entry point")
    entry_start, entry_end = _balanced_block(source, entry.group())
    returns = list(re.finditer(r"\breturn\s+output\s*;", source))
    if not returns or any(not entry_start < item.start() < entry_end for item in returns):
        raise ValueError("unexpected generated output return structure")
    helper = OUTPUT_HELPERS
    flags = FLAG_NATIVE_OUTPUT
    if outputs & 1:
        helper += "\n#if defined(__air__)\n" + coverage_source(coverage_header) + "\n#endif\n"
        flags |= FLAG_COVERAGE
    declaration = source.find("struct PixelShaderOutput")
    source = source[:declaration] + helper + source[declaration:]
    # A block preserves the meaning of an unbraced if (...) return output.
    lines = ["{", "#if defined(__air__)"]
    if outputs & 1:
        lines += [
            "    const uint libertyCoverage = *(reinterpret_cast<device const uint*>(g_PushConstants.SharedConstants + 736));",
            "    const uint libertySamples = *(reinterpret_cast<device const uint*>(g_PushConstants.SharedConstants + 740));",
            "    const uint2 libertyPixel = uint2(input.iPos.xy);",
            "    output.libertySampleMask = liberty_metal_contract::ComputeNativeAlphaToMask(output.oC0.w, libertyPixel.x, libertyPixel.y, libertyCoverage, libertySamples);",
        ]
    for color in range(4):
        if outputs & (1 << color):
            offset = 0x360 + 48 * color
            lines.append(f"    output.oC{color} = libertyColorOutput(output.oC{color}, *(reinterpret_cast<device const LibertyColorOutput*>(g_PushConstants.SharedConstants + {offset})));" )
    lines += ["#endif", "    return output;", "}"]
    source = re.sub(r"\breturn\s+output\s*;", "\n".join(lines), source)
    early_count = source.count("[[early_fragment_tests]]")
    if early_count > 1:
        raise ValueError("multiple early-test declarations")
    if early_count:
        if outputs & 16:
            raise ValueError("depth-writing shader cannot use this early variant")
        flags |= FLAG_EARLY_TESTS | FLAG_LATE_VARIANT
        late = source.replace("[[early_fragment_tests]]", "")
        if late.count("[earlydepthstencil]") > 1:
            raise ValueError("multiple HLSL early-test declarations")
        late = late.replace("[earlydepthstencil]", "")
    else:
        late = ""
    return source, late, attributes, stage, outputs, flags


def _load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    if not spec or not spec.loader:
        raise ValueError(f"cannot load source reader {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def _decompress(compressed: bytes, expected: int, library_path: str | None) -> bytes:
    if not 0 < expected <= MAX_BYTES:
        raise ValueError("source cache exceeds the archive budget")
    library_path = library_path or ctypes.util.find_library("zstd")
    if not library_path:
        raise ValueError("libzstd is unavailable; supply --zstd-library")
    zstd = ctypes.CDLL(library_path)
    zstd.ZSTD_decompress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
    zstd.ZSTD_decompress.restype = ctypes.c_size_t
    zstd.ZSTD_isError.argtypes = [ctypes.c_size_t]
    zstd.ZSTD_isError.restype = ctypes.c_uint
    output = ctypes.create_string_buffer(expected)
    result = zstd.ZSTD_decompress(output, expected, compressed, len(compressed))
    if zstd.ZSTD_isError(result) or result != expected:
        raise ValueError("stock Metal cache decompression failed")
    return output.raw


def _atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + ".", delete=False) as stream:
            temporary_name = stream.name
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, path)
    finally:
        if temporary_name and os.path.exists(temporary_name):
            os.unlink(temporary_name)


class MetalCompiler:
    def __init__(self, work: Path, sdk: str, deployment: str):
        self.work = work
        self.sdk = sdk
        self.deployment = deployment
        self.metal = subprocess.check_output(["xcrun", "--find", "metal"], text=True).strip()
        self.metallib = subprocess.check_output(["xcrun", "--find", "metallib"], text=True).strip()
        self.version = subprocess.check_output([self.metal, "--version"], text=True).strip()
        self.sdk_path = subprocess.check_output(["xcrun", "--sdk", sdk, "--show-sdk-path"], text=True).strip()
        self.work.mkdir(parents=True, exist_ok=True)

    def compile(self, source: str) -> bytes:
        recipe = (self.version + "\0" + self.sdk_path + "\0" + self.deployment + "\0" + source).encode()
        fingerprint = hashlib.sha256(recipe).hexdigest()
        output = self.work / (fingerprint + ".metallib")
        sidecar = self.work / (fingerprint + ".json")
        if output.exists() and sidecar.exists():
            data = output.read_bytes()
            metadata = json.loads(sidecar.read_text())
            if metadata.get("recipe") == fingerprint and metadata.get("sha256") == hashlib.sha256(data).hexdigest() and data[:4] == b"MTLB":
                return data
        # Each invocation has its own temporary directory, even for identical
        # sources submitted concurrently. Publishing final files is atomic.
        with tempfile.TemporaryDirectory(prefix="compile-", dir=self.work) as directory:
            root = Path(directory)
            metal_file, air_file, library_file = root / "shader.metal", root / "shader.air", root / "shader.metallib"
            metal_file.write_text(source, encoding="utf-8")
            command = [self.metal, "-c", str(metal_file), "-o", str(air_file),
                       "-isysroot", self.sdk_path, "-std=metal3.0", "-D__air__", "-DGTA4_RECOMP",
                       "-fno-fast-math", "-Wno-unused-variable", f"-mmacosx-version-min={self.deployment}"]
            completed = subprocess.run(command, text=True, capture_output=True, timeout=180)
            if completed.returncode:
                error = self.work / (fingerprint + ".failed.metal")
                _atomic_write(error, source.encode())
                raise RuntimeError(f"Metal compile failed; source={error}\n{completed.stdout}{completed.stderr}")
            completed = subprocess.run([self.metallib, str(air_file), "-o", str(library_file)], text=True, capture_output=True, timeout=180)
            if completed.returncode:
                raise RuntimeError(f"Metal link failed\n{completed.stdout}{completed.stderr}")
            data = library_file.read_bytes()
            if len(data) < 4 or data[:4] != b"MTLB":
                raise ValueError("Metal compiler did not produce a library")
        _atomic_write(output, data)
        _atomic_write(sidecar, json.dumps({"recipe": fingerprint, "sha256": hashlib.sha256(data).hexdigest()}).encode())
        return data


def build_from_repo(root: Path, output: Path, work: Path, jobs: int = 2,
                    compile_source: Callable[[str], bytes] | None = None,
                    zstd_library: str | None = None, emit_only: bool = False) -> dict:
    cache_path = root / "LibertyRecompLib/shader/shader_cache.cpp"
    source_bytes = cache_path.read_bytes()
    cache_fingerprint = hashlib.sha256(source_bytes).hexdigest()
    source_text = source_bytes.decode("utf-8")
    reader = _load_module(root / "tools/validate_shader_preservation.py", "liberty_metal_source_cache")
    recovery = _load_module(root / "tools/recover_shader_sources.py", "liberty_metal_source_recovery")
    entries = reader.RANGES.parse_entries(source_text, cache_path)
    declared = reader.RANGES.declared_size(source_text, "g_shaderCacheEntryCount", cache_path)
    if len(entries) != declared or len(entries) > MAX_SHADERS or len({e.shader_hash for e in entries}) != len(entries):
        raise ValueError("stock cache entry inventory is inconsistent")
    expected = reader.RANGES.declared_size(source_text, "g_airCacheDecompressedSize", cache_path)
    compressed = reader.compressed_array(source_text, "g_compressedAirCache", cache_path)
    data = _decompress(compressed, expected, zstd_library)
    header = (root / "glue/rexglue-sdk-main/src/graphics/gta4_native/alpha_to_coverage_util.h").read_text()
    work.mkdir(parents=True, exist_ok=True)
    recovered = []
    for entry in entries:
        checked_range(entry.air_offset, entry.air_size, len(data), entry.filename)
        if entry.used_texture_mask is None:
            raise ValueError(f"{entry.shader_hash:016X}: missing texture-use metadata")
        library = data[entry.air_offset:entry.air_offset + entry.air_size]
        original, _ = recovery.embedded_source(library)
        try:
            primary, late, attrs, stage, outputs, flags = prepare_source(original.decode("utf-8"), header)
        except ValueError as error:
            raise ValueError(f"{entry.shader_hash:016X} ({entry.filename}): {error}") from error
        if entry.late_spirv_size and not late:
            if stage != PIXEL or flags & FLAG_EARLY_TESTS:
                raise ValueError(f"{entry.shader_hash:016X}: inconsistent late-test source")
            # A source without forced early tests is already suitable for the
            # late path. Keep that variant explicitly instead of dropping it.
            late = primary
            flags |= FLAG_LATE_VARIANT
        recovered.append((entry, primary, late, attrs, stage, outputs, flags, hashlib.sha256(original).hexdigest()))
    source_directory = work / "sources"
    source_directory.mkdir(exist_ok=True)
    for entry, primary, late, *_ in recovered:
        _atomic_write(source_directory / f"{entry.shader_hash:016X}.early.metal", primary.encode())
        if late:
            _atomic_write(source_directory / f"{entry.shader_hash:016X}.late.metal", late.encode())
    if compile_source is None and not emit_only:
        compile_source = MetalCompiler(work / "compiled", "macosx", "26.0").compile
    result = {"schema": VERSION, "stock_cache_sha256": cache_fingerprint,
              "coverage_header_sha256": hashlib.sha256(header.encode()).hexdigest(),
              "entries": len(entries), "late_variants": sum(bool(item[2]) for item in recovered),
              "mode": "source-only" if emit_only else "compiled",
              "sources": [{"hash": f"{item[0].shader_hash:016X}", "original_sha256": item[7]} for item in recovered]}
    if not emit_only:
        assert compile_source is not None
        def compile_item(item) -> Shader:
            entry, primary, late, attrs, stage, outputs, flags, _ = item
            shader = Shader(entry.shader_hash, stage, entry.used_texture_mask, entry.spec_constants_mask,
                            outputs, flags, entry.filename, attrs, compile_source(primary), compile_source(late) if late else b"")
            print(f"compiled {entry.shader_hash:016X} {entry.filename}", flush=True)
            return shader
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(jobs, 4))) as executor:
            shaders = list(executor.map(compile_item, recovered))
        archive = build_archive(shaders)
        if {entry.shader_hash for entry in entries} != {shader.shader_hash for shader in parse_archive(archive)}:
            raise ValueError("compiled archive does not preserve the complete stock inventory")
        if hashlib.sha256(cache_path.read_bytes()).hexdigest() != cache_fingerprint:
            raise ValueError("stock cache changed during generation; refusing stale archive publication")
        _atomic_write(output, archive)
        result.update({"archive_bytes": len(archive), "archive_sha256": hashlib.sha256(archive).hexdigest()})
    _atomic_write(work / "manifest.json", (json.dumps(result, indent=2) + "\n").encode())
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--work", type=Path)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--zstd-library")
    parser.add_argument("--emit-only", action="store_true")
    parser.add_argument("--verify", type=Path)
    args = parser.parse_args()
    if args.verify:
        shaders = parse_archive(args.verify.read_bytes())
        print(json.dumps({"valid": True, "entries": len(shaders)}))
        return 0
    if args.output is None or args.work is None:
        parser.error("--output and --work are required for generation")
    result = build_from_repo(args.repo.resolve(), args.output, args.work, args.jobs,
                             zstd_library=args.zstd_library, emit_only=args.emit_only)
    print(json.dumps({key: value for key, value in result.items() if key != "sources"}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"Metal shader archive: {error}", file=sys.stderr)
        raise SystemExit(1)
