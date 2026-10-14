#!/usr/bin/env python3
"""Portable archive and source-transform regression tests; no GPU claims."""
from __future__ import annotations

from dataclasses import replace
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / "tools/metal/shader_archive.py"
spec = importlib.util.spec_from_file_location("metal_archive", MODULE)
a = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = a
spec.loader.exec_module(a)

# A deliberately small source fixture tests lexical preservation only. The
# production build imports the real alpha_to_coverage_util.h algorithm.
COVERAGE_FIXTURE = '''#ifndef TEST_COVERAGE_H
#define TEST_COVERAGE_H
#include <cstdint>
namespace rex::graphics::gta4_native {
constexpr uint32_t kNativeAlphaToMaskEnable = 1u << 8;
constexpr bool IsNativeAlphaToMaskRequested(uint32_t mask) { return (mask & kNativeAlphaToMaskEnable) != 0; }
constexpr uint32_t ComputeNativeAlphaToMask(float alpha, uint32_t x, uint32_t y, uint32_t mask, uint32_t count) {
  return mask;
}
}
#endif
'''
PIXEL_SOURCE = '''#ifndef SHADER_COMMON_H_INCLUDED
#define SHADER_COMMON_H_INCLUDED
#ifdef __air__
#include <metal_stdlib>
using namespace metal;
#endif
#endif
struct PixelShaderOutput
{
#ifdef __air__
 float4 oC0 [[color(0)]];
 float4 oC2 [[color(2)]];
#else
 float4 oC0 : SV_Target0;
 float4 oC2 : SV_Target2;
#endif
};
#ifdef __air__
[[fragment]]
[[early_fragment_tests]]
#else
[earlydepthstencil]
#endif
PixelShaderOutput shaderMain(Interpolators input)
{
 PixelShaderOutput output = {};
 output.oC0 = float4(0.25f);
 output.oC2 = float4(0.75f);
 return output;
}
'''
VERTEX_SOURCE = '''struct VertexShaderInput {
#ifdef __air__
 float4 position [[attribute(0)]];
 uint4 blend [[attribute(18)]];
 float2 uv [[attribute(35)]];
#endif
};
[[vertex]]
Interpolators shaderMain(VertexShaderInput input) {
 Interpolators output;
 return output;
}
'''


def fixtures():
    return (
        a.Shader(0x1234, a.VERTEX, 3, 0x702, 0, 0, "vertex/日本語", (a.Attribute(0, 0, 0, 4), a.Attribute(1, 35, 2, 2)), b"MTLBvertex"),
        a.Shader(0xABCDEF, a.PIXEL, 1, 0x702, 5, a.FLAG_EARLY_TESTS | a.FLAG_LATE_VARIANT | a.FLAG_NATIVE_OUTPUT | a.FLAG_COVERAGE,
                 "pixel/alpha", (), b"MTLBprimary", b"MTLBlate"),
    )


def repair(data):
    value = bytearray(data)
    struct.pack_into("<Q", value, 24, len(value))
    value[32:64] = hashlib.sha256(value[64:]).digest()
    return bytes(value)


def altered(data, offset, fmt, value, checksum=True):
    result = bytearray(data)
    struct.pack_into(fmt, result, offset, value)
    return repair(result) if checksum else bytes(result)


class ArchiveTests(unittest.TestCase):
    def setUp(self):
        self.shaders = fixtures()
        self.data = a.build_archive(self.shaders)

    def test_roundtrip(self):
        self.assertEqual(a.parse_archive(self.data), self.shaders)

    def test_deterministic_sorted_index(self):
        self.assertEqual(a.build_archive(reversed(self.shaders)), self.data)

    def test_complete_inventory(self):
        many = tuple(replace(self.shaders[0], shader_hash=i + 1) for i in range(1356))
        self.assertEqual(a.parse_archive(a.build_archive(many)), many)

    def test_invalid_header(self):
        for offset, fmt, value in [(0, "Q", 0), (8, "I", 3), (12, "I", 0), (16, "I", 0), (20, "I", 0), (20, "I", a.MAX_SHADERS + 1)]:
            with self.subTest(offset=offset, value=value), self.assertRaises(ValueError):
                a.parse_archive(altered(self.data, offset, "<" + fmt, value))

    def test_digest_detects_change(self):
        value = bytearray(self.data)
        value[-1] ^= 1
        with self.assertRaises(ValueError): a.parse_archive(bytes(value))

    def test_truncated(self):
        for length in range(len(self.data)):
            with self.assertRaises(ValueError): a.parse_archive(self.data[:length])

    def test_outside_payload(self):
        for offset in (0, 64, (1 << 64) - 1, len(self.data) + 1):
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                a.parse_archive(altered(self.data, 64 + 32, "<Q", offset))

    def test_overflow_length(self):
        with self.assertRaises(ValueError):
            a.parse_archive(altered(self.data, 64 + 40, "<Q", (1 << 64) - 1))

    def test_duplicate_hash(self):
        with self.assertRaises(ValueError): a.build_archive([self.shaders[0], self.shaders[0]])
        with self.assertRaises(ValueError): a.parse_archive(altered(self.data, 64 + a.RECORD.size, "<Q", self.shaders[0].shader_hash))

    def test_invalid_metadata(self):
        for offset, value in [(8, 2), (12, 1 << 26), (20, 32), (24, 32), (28, 0x80), (76, 1)]:
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                a.parse_archive(altered(self.data, 64 + offset, "<I", value))

    def test_empty_offset_canonical(self):
        with self.assertRaises(ValueError): a.parse_archive(altered(self.data, 64 + 48, "<Q", 256))

    def test_no_alias_payload(self):
        first = a.RECORD.unpack_from(self.data, 64)
        with self.assertRaises(ValueError):
            a.parse_archive(altered(self.data, 64 + a.RECORD.size + 32, "<Q", first[7]))

    def test_trailing_bytes(self):
        with self.assertRaises(ValueError): a.parse_archive(repair(self.data + b"\0"))

    def test_vertex_contract(self):
        for bad in [replace(self.shaders[0], attributes=(a.Attribute(1, 0, 0, 4),)),
                    replace(self.shaders[0], attributes=(a.Attribute(0, 0, 0, 4), a.Attribute(1, 0, 0, 4))),
                    replace(self.shaders[0], outputs=1),
                    replace(self.shaders[0], attributes=(a.Attribute(0, 65, 0, 4),)),
                    replace(self.shaders[0], attributes=(a.Attribute(0, 0, 3, 4),))]:
            with self.assertRaises(ValueError): a.build_archive([bad])

    def test_variant_contract(self):
        for bad in [replace(self.shaders[1], late=b""), replace(self.shaders[1], early=b"wrong"),
                    replace(self.shaders[1], outputs=17), replace(self.shaders[1], outputs=4)]:
            with self.assertRaises(ValueError): a.build_archive([bad])

    def test_names(self):
        for name in ("", "nul\0name", "x" * 4097):
            with self.assertRaises(ValueError): a.build_archive([replace(self.shaders[0], name=name)])
        record = a.RECORD.unpack_from(self.data, 64)
        value = bytearray(self.data)
        value[record[11]] = 0xFF
        with self.assertRaises(ValueError): a.parse_archive(repair(value))

    def test_checked_range_boundary(self):
        a.checked_range(5, 0, 5, "zero")
        a.checked_range(0, 5, 5, "whole")
        for offset, length in [(-1, 1), (6, 0), (5, 1), (0, -1)]:
            with self.assertRaises(ValueError): a.checked_range(offset, length, 5, "bad")

    def test_atomic_write(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sub/archive.bin"
            a._atomic_write(path, self.data)
            self.assertEqual(path.read_bytes(), self.data)
            a._atomic_write(path, b"new")
            self.assertEqual(path.read_bytes(), b"new")
            self.assertEqual(len(list(path.parent.iterdir())), 1)


class SourceTests(unittest.TestCase):
    def test_pixel_variants(self):
        primary, late, attrs, stage, outputs, flags = a.prepare_source(PIXEL_SOURCE, COVERAGE_FIXTURE)
        self.assertEqual((stage, outputs, attrs), (a.PIXEL, 5, ()))
        self.assertTrue(flags & a.FLAG_COVERAGE)
        self.assertEqual(primary.count("[[sample_mask]]"), 1)
        self.assertEqual(primary.count("[[early_fragment_tests]]"), 1)
        self.assertNotIn("[[early_fragment_tests]]", late)
        self.assertNotIn("[earlydepthstencil]", late)
        self.assertIn("output.oC0 = float4(0.25f);", primary)
        self.assertLess(primary.index("output.libertySampleMask ="), primary.index("output.oC0 = libertyColorOutput"))
        self.assertIn("SharedConstants + 864", primary)
        self.assertIn("SharedConstants + 960", primary)

    def test_dense_vertex_binding(self):
        source, late, attributes, stage, outputs, flags = a.prepare_source(VERTEX_SOURCE, COVERAGE_FIXTURE)
        self.assertEqual([attribute.semantic_location for attribute in attributes], [0, 18, 35])
        self.assertEqual([attribute.index for attribute in attributes], [0, 1, 2])
        self.assertIn("uv [[attribute(2)]]", source)
        self.assertEqual((late, stage, outputs, flags), ("", a.VERTEX, 0, 0))
        self.assertEqual(source.replace("blend [[attribute(1)]]", "blend [[attribute(18)]]").replace("uv [[attribute(2)]]", "uv [[attribute(35)]]"), VERTEX_SOURCE)

    def test_reject_double_adaptation(self):
        source = a.prepare_source(PIXEL_SOURCE, COVERAGE_FIXTURE)[0]
        with self.assertRaises(ValueError): a.prepare_source(source, COVERAGE_FIXTURE)

    def test_multiple_entry_returns(self):
        source = PIXEL_SOURCE.replace("return output;", "if (input.iPos.x > 0) return output;\nreturn output;")
        primary, late, *_ = a.prepare_source(source, COVERAGE_FIXTURE)
        self.assertEqual(primary.count("output.libertySampleMask ="), 2)
        self.assertEqual(late.count("output.libertySampleMask ="), 2)
        self.assertIn("if (input.iPos.x > 0) {", primary)

    def test_reject_output_return_outside_entry(self):
        with self.assertRaises(ValueError):
            a.prepare_source(PIXEL_SOURCE + "\nPixelShaderOutput helper(){return output;}", COVERAGE_FIXTURE)

    def test_reject_already_covered(self):
        with self.assertRaises(ValueError): a.prepare_source(PIXEL_SOURCE.replace("float4 oC0 [[color(0)]];", "float4 oC0 [[color(0)]]; uint mask [[sample_mask]];"), COVERAGE_FIXTURE)

    def test_depth_writer(self):
        source = PIXEL_SOURCE.replace("[[early_fragment_tests]]", "").replace("[earlydepthstencil]", "")
        source = source.replace("float4 oC2 [[color(2)]];", "float oDepth [[depth(any)]];")
        _, late, _, _, outputs, flags = a.prepare_source(source, COVERAGE_FIXTURE)
        self.assertEqual(outputs, 17)
        self.assertEqual(late, "")
        self.assertFalse(flags & a.FLAG_EARLY_TESTS)

    def test_reject_early_depth_writer(self):
        with self.assertRaises(ValueError):
            a.prepare_source(PIXEL_SOURCE.replace("float4 oC2 [[color(2)]];", "float oDepth [[depth(any)]];"), COVERAGE_FIXTURE)

    def test_oracle_body_preserved(self):
        result = a.coverage_source(COVERAGE_FIXTURE)
        self.assertIn("return mask;", result)
        self.assertIn("namespace liberty_metal_contract", result)
        self.assertNotIn("#include", result)
        self.assertNotIn("uint32_t", result)

    def test_reject_changed_oracle(self):
        for header in [COVERAGE_FIXTURE.replace("<cstdint>", "<vector>"), COVERAGE_FIXTURE.replace("return mask;", "return std::max(mask, count);"), ""]:
            with self.assertRaises(ValueError): a.coverage_source(header)

    def test_reject_oracle_macro_semantics_change(self):
        header = COVERAGE_FIXTURE.replace("#include <cstdint>", "#include <cstdint>\n#define MASK_LIMIT 7")
        with self.assertRaises(ValueError): a.coverage_source(header)

    def test_reject_oracle_conditional_semantics_change(self):
        header = COVERAGE_FIXTURE.replace("return mask;", "#if DIFFERENT\nreturn mask;\n#endif")
        with self.assertRaises(ValueError): a.coverage_source(header)

    def test_no_fake_shader_on_bad_source(self):
        for source in ("", "float4 shaderMain(){return 1;}", PIXEL_SOURCE + VERTEX_SOURCE):
            with self.assertRaises(ValueError): a.prepare_source(source, COVERAGE_FIXTURE)


class CppAgreementTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("clang++") or shutil.which("g++")
        if not compiler: raise unittest.SkipTest("C++ compiler unavailable")
        cls.temporary = tempfile.TemporaryDirectory(prefix="metal-archive-test-")
        cls.root = Path(cls.temporary.name)
        source = ROOT / "glue/rexglue-sdk-main/src/graphics/gta4_metal"
        cls.executable = cls.root / "archive-test"
        command = [compiler, "-std=c++20", "-O1", "-g", "-UNDEBUG", "-fsanitize=address,undefined",
                   "-fno-omit-frame-pointer", "-I", str(source), str(source / "shader_archive.cpp"),
                   str(ROOT / "tools/tests/metal_shader_archive_test.cpp"),
                   *([] if platform.system() == "Darwin" else ["-lcrypto"]), "-o", str(cls.executable)]
        subprocess.run(command, check=True, capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_cross_language_mutation_corpus(self):
        seed = a.build_archive(fixtures())
        cases = [seed, b"", seed[:32], repair(seed + b"junk")]
        rng = random.Random(2048)
        for index in range(500):
            data = bytearray(seed)
            if index % 3 == 0:
                at = rng.randrange(len(data))
                data[at] ^= rng.randrange(1, 256)
            else:
                at = rng.randrange(64, 64 + a.RECORD.size * 2 - 8)
                data[at:at + 8] = rng.getrandbits(64).to_bytes(8, "little")
            cases.append(repair(data) if index % 2 else bytes(data))
        expected = []
        paths = []
        for index, data in enumerate(cases):
            path = self.root / f"case-{index}.bin"
            path.write_bytes(data)
            paths.append(str(path))
            try:
                count = len(a.parse_archive(data))
                expected.append((1, count))
            except ValueError:
                expected.append((0, 0))
        completed = subprocess.run([str(self.executable), *paths], capture_output=True, text=True, check=True,
                                   env={**os.environ, "ASAN_OPTIONS": "detect_leaks=" + ("0" if platform.system() == "Darwin" else "1") + ":halt_on_error=1"})
        actual = [tuple(map(int, row.split("\t")[1:])) for row in completed.stdout.splitlines()]
        self.assertEqual(actual, expected)
        self.assertNotIn("ERROR", completed.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
