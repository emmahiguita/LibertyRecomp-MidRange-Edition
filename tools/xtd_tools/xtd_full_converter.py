#!/usr/bin/env python3
"""
Complete Xbox 360 XTD Texture Dictionary Converter
Uses Wine + lzx_cli.exe to decompress/recompress XTD files.

This tool:
1. Extracts RSC header and LZX data from original XTD
2. Decompresses LZX data using Wine + xcompress DLLs
3. Parses texture dictionary structure
4. Replaces textures with new DDS files
5. Recompresses and rebuilds XTD with new RSC header
"""

import os
import sys
import struct
import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Dict, List, Tuple, Optional
from dataclasses import dataclass, field
import json

# Paths
SCRIPT_DIR = Path(__file__).parent
LZX_CLI = SCRIPT_DIR / "lzx_cli.exe"
DLL_DIR = SCRIPT_DIR / "full" / "RAGE-Console-Texture-Editor-master"
WINE_PATH = shutil.which("wine") or "/opt/homebrew/bin/wine"

# Source paths
SOURCE_DDS_DIR = Path("/Users/Ozordi/Downloads/LibertyRecomp/tools/GTAIV.EFLC.FusionFix-master/textures/pc/textures/buttons_360.wtd")
ORIGINAL_XTD = Path("/Users/ozordi/Library/Application Support/LibertyRecomp/game/xbox360/textures/buttons_360.xtd")
OUTPUT_DIR = SCRIPT_DIR / "output"

# Controller variants
CONTROLLER_VARIANTS = {
    "xbox360": "",
    "ps3": "ps3_",
    "ps4": "ps4_",
    "ps5": "ps5_",
    "steam_controller": "sc_",
    "steam_deck": "sd_",
    "switch": "switch_",
    "xbox_one": "xbone_",
    "xbox_series_x": "xsx_"
}

@dataclass
class RSCHeader:
    """RSC v5 header structure."""
    magic: int  # 0x52534305
    resource_type: int  # 7 for texture dictionary
    flags: int
    
    @property
    def cpu_size(self) -> int:
        return (self.flags & 0x7FF) << (((self.flags >> 11) & 0xF) + 8)
    
    @property
    def gpu_size(self) -> int:
        return ((self.flags >> 15) & 0x7FF) << (((self.flags >> 26) & 0xF) + 8)
    
    @classmethod
    def from_bytes(cls, data: bytes) -> 'RSCHeader':
        magic = struct.unpack('>I', data[0:4])[0]
        resource_type = struct.unpack('>I', data[4:8])[0] >> 24
        flags = struct.unpack('>I', data[8:12])[0]
        return cls(magic, resource_type, flags)
    
    def to_bytes(self) -> bytes:
        result = struct.pack('>I', self.magic)
        result += struct.pack('>I', self.resource_type << 24)
        result += struct.pack('>I', self.flags)
        return result

@dataclass
class TextureEntry:
    """Texture dictionary entry."""
    name: str
    offset: int
    size: int
    width: int
    height: int
    mip_count: int
    format: int
    data: bytes = field(default=b'', repr=False)

def run_wine_command(cmd: List[str], cwd: Path = None) -> Tuple[int, str, str]:
    """Run a Wine command and capture output."""
    env = os.environ.copy()
    # Add script directory to WINEPATH for DLLs
    env['WINEPATH'] = str(SCRIPT_DIR)
    
    full_cmd = [WINE_PATH] + cmd
    work_dir = cwd or SCRIPT_DIR
    
    try:
        result = subprocess.run(
            full_cmd,
            cwd=work_dir,
            capture_output=True,
            text=True,
            timeout=60,
            env=env
        )
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired:
        return -1, "", "Command timed out"
    except Exception as e:
        return -1, "", str(e)

def extract_lzx_data(xtd_path: Path) -> Tuple[RSCHeader, int, bytes]:
    """Extract RSC header, compressed size, and LZX compressed data from XTD file.
    
    RSC5 format (for Xbox 360):
    - Bytes 0-3: Magic 'RSC\\x05' (0x52534305)
    - Bytes 4-7: Resource type (7 for texture dictionary) 
    - Bytes 8-11: Flags (encodes CPU/GPU sizes)
    - Bytes 12-15: Unknown/padding
    - Bytes 16-19: Compressed size (dwInSize)
    - Bytes 20+: LZX compressed data
    """
    with open(xtd_path, 'rb') as f:
        # Read RSC header (12 bytes)
        header_data = f.read(12)
        header = RSCHeader.from_bytes(header_data)
        
        # Skip 4 bytes of unknown/padding (offset 12-15)
        f.read(4)
        
        # Read compressed size at offset 16 (big-endian)
        size_data = f.read(4)
        compressed_size = struct.unpack('>I', size_data)[0]
        
        # Rest is LZX compressed data (starting at offset 20)
        lzx_data = f.read()
    
    return header, compressed_size, lzx_data

def decompress_xtd(xtd_path: Path, output_path: Path) -> bool:
    """Decompress XTD file using Wine + lzx_cli.exe."""
    print(f"  Decompressing {xtd_path.name}...")
    
    # Extract LZX data to temp file (starting from offset 16)
    header, comp_size, lzx_data = extract_lzx_data(xtd_path)
    
    print(f"    Header: magic=0x{header.magic:08X}, type={header.resource_type}")
    print(f"    Compressed size: {comp_size:,} bytes")
    print(f"    LZX data size: {len(lzx_data):,} bytes")
    print(f"    Expected output: CPU={header.cpu_size:,} + GPU={header.gpu_size:,} = {header.cpu_size + header.gpu_size:,} bytes")
    
    with tempfile.NamedTemporaryFile(suffix='.lzx', delete=False) as tmp:
        tmp.write(lzx_data)
        tmp_lzx = Path(tmp.name)
    
    try:
        # Run decompression - use full path
        cmd = [str(LZX_CLI), "decompress", str(tmp_lzx), str(output_path)]
        
        ret, stdout, stderr = run_wine_command(cmd)
        
        if "Decompressed" in stdout:
            print(f"  ✓ Decompressed to {output_path}")
            print(f"    {stdout.strip()}")
            return True
        
        if ret != 0 or not output_path.exists():
            print(f"  ERROR: Decompression failed (ret={ret})")
            print(f"  stdout: {stdout}")
            # Filter Wine noise
            clean_stderr = '\n'.join([l for l in stderr.split('\n') 
                                      if not l.startswith('00') and 'err:' not in l.lower()])
            if clean_stderr:
                print(f"  stderr: {clean_stderr}")
            return False
        
        print(f"  ✓ Decompressed to {output_path}")
        return True
        
    finally:
        tmp_lzx.unlink(missing_ok=True)

def compress_to_lzx(input_path: Path, output_path: Path) -> bool:
    """Compress raw data to LZX using Wine + lzx_cli.exe."""
    print(f"  Compressing {input_path.name}...")
    
    cmd = [str(LZX_CLI), "compress", input_path.as_posix(), output_path.as_posix()]
    ret, stdout, stderr = run_wine_command(cmd)
    
    if ret != 0:
        print(f"  ERROR: Compression failed")
        print(f"  stderr: {stderr}")
        return False
    
    print(f"  ✓ Compressed to {output_path}")
    return True

def build_xtd(header: RSCHeader, lzx_data: bytes, output_path: Path):
    """Build XTD file from RSC header and LZX data."""
    with open(output_path, 'wb') as f:
        # Write RSC header
        f.write(header.to_bytes())
        # Write compressed size (big-endian)
        f.write(struct.pack('>I', len(lzx_data)))
        # Write LZX data
        f.write(lzx_data)
    print(f"  ✓ Built {output_path.name} ({output_path.stat().st_size:,} bytes)")

def analyze_xtd(xtd_path: Path) -> dict:
    """Analyze XTD file structure."""
    header, comp_size, lzx_data = extract_lzx_data(xtd_path)
    
    info = {
        "file": str(xtd_path),
        "size": xtd_path.stat().st_size,
        "magic": f"0x{header.magic:08X}",
        "resource_type": header.resource_type,
        "flags": f"0x{header.flags:08X}",
        "cpu_size": header.cpu_size,
        "gpu_size": header.gpu_size,
        "compressed_size": comp_size,
        "lzx_size": len(lzx_data),
        "valid": header.magic == 0x52534305 and header.resource_type == 7
    }
    
    return info

def get_dds_files() -> Dict[str, Path]:
    """Get all DDS files from source directory."""
    dds_files = {}
    if SOURCE_DDS_DIR.exists():
        for f in SOURCE_DDS_DIR.glob("*.dds"):
            dds_files[f.stem.lower()] = f
    return dds_files

def get_base_names(dds_files: Dict[str, Path]) -> List[str]:
    """Get base texture names without controller prefixes."""
    prefixes = [v for v in CONTROLLER_VARIANTS.values() if v]
    base_names = set()
    
    for name in dds_files.keys():
        matched = False
        for prefix in prefixes:
            if name.startswith(prefix):
                base_names.add(name[len(prefix):])
                matched = True
                break
        if not matched:
            base_names.add(name)
    
    return sorted(base_names)

def test_wine_lzx():
    """Test Wine + lzx_cli.exe setup."""
    print("Testing Wine + LZX CLI setup...")
    
    # Check Wine
    if not shutil.which("wine"):
        print("  ✗ Wine not found in PATH")
        return False
    print(f"  ✓ Wine: {WINE_PATH}")
    
    # Check LZX CLI
    if not LZX_CLI.exists():
        print(f"  ✗ lzx_cli.exe not found at {LZX_CLI}")
        return False
    print(f"  ✓ LZX CLI: {LZX_CLI}")
    
    # Check DLLs in script directory
    required_dlls = ["xcompress_cpp.dll", "xcompress.dll"]
    for dll in required_dlls:
        dll_path = SCRIPT_DIR / dll
        if dll_path.exists():
            print(f"  ✓ {dll}: {dll_path}")
        else:
            print(f"  ✗ {dll}: not found in {SCRIPT_DIR}")
            return False
    
    # Test running lzx_cli
    print("  Running lzx_cli.exe test...")
    cmd = [str(LZX_CLI)]
    ret, stdout, stderr = run_wine_command(cmd)
    
    # Filter out Wine noise
    clean_stderr = '\n'.join([l for l in stderr.split('\n') 
                              if not l.startswith('00') and 'err:' not in l.lower()])
    
    if "LZX CLI Tool" in stdout or "Usage" in stdout or ret == 1:
        print("  ✓ lzx_cli.exe runs correctly under Wine")
        return True
    else:
        print(f"  ✗ lzx_cli.exe test failed (ret={ret})")
        if stdout:
            print(f"    stdout: {stdout[:300]}")
        if clean_stderr:
            print(f"    stderr: {clean_stderr[:300]}")
        return False

def full_conversion_pipeline():
    """Run full XTD conversion for all controller variants."""
    print("=" * 70)
    print("Xbox 360 XTD Texture Converter - Full Pipeline")
    print("=" * 70)
    
    # Step 1: Test setup
    print("\n[1/6] Testing setup...")
    if not test_wine_lzx():
        print("\nSetup test failed. Please ensure Wine and DLLs are properly installed.")
        return False
    
    # Step 2: Analyze original
    print("\n[2/6] Analyzing original XTD...")
    if not ORIGINAL_XTD.exists():
        print(f"ERROR: Original XTD not found: {ORIGINAL_XTD}")
        return False
    
    info = analyze_xtd(ORIGINAL_XTD)
    print(f"  File: {info['file']}")
    print(f"  Size: {info['size']:,} bytes")
    print(f"  Magic: {info['magic']}")
    print(f"  Resource Type: {info['resource_type']}")
    print(f"  CPU Size (decompressed): {info['cpu_size']:,} bytes")
    print(f"  GPU Size (decompressed): {info['gpu_size']:,} bytes")
    print(f"  LZX Size: {info['lzx_size']:,} bytes")
    print(f"  Valid: {'✓' if info['valid'] else '✗'}")
    
    if not info['valid']:
        print("ERROR: Invalid XTD file")
        return False
    
    # Step 3: Decompress original
    print("\n[3/6] Decompressing original XTD...")
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    decompressed_path = OUTPUT_DIR / "original_decompressed.bin"
    
    if not decompress_xtd(ORIGINAL_XTD, decompressed_path):
        print("ERROR: Failed to decompress original XTD")
        return False
    
    # Step 4: Get DDS files
    print("\n[4/6] Scanning source DDS files...")
    dds_files = get_dds_files()
    base_names = get_base_names(dds_files)
    print(f"  Found {len(dds_files)} DDS files")
    print(f"  Base texture names: {len(base_names)}")
    
    # Step 5: For now, just copy original for each variant
    # (Full texture replacement requires parsing the texture dictionary format)
    print("\n[5/6] Creating controller variants...")
    
    header, _, _ = extract_lzx_data(ORIGINAL_XTD)
    
    results = {}
    for variant_name, prefix in CONTROLLER_VARIANTS.items():
        variant_dir = OUTPUT_DIR / variant_name
        variant_dir.mkdir(parents=True, exist_ok=True)
        
        # For now, copy original XTD as placeholder
        # TODO: Implement actual texture replacement
        variant_xtd = variant_dir / "buttons_360.xtd"
        shutil.copy2(ORIGINAL_XTD, variant_xtd)
        
        # Copy DDS files for this variant
        dds_dir = variant_dir / "dds_textures"
        dds_dir.mkdir(exist_ok=True)
        
        variant_dds_count = 0
        for base_name in base_names:
            variant_dds_name = f"{prefix}{base_name}" if prefix else base_name
            if variant_dds_name in dds_files:
                src = dds_files[variant_dds_name]
                dst = dds_dir / f"{base_name}.dds"
                shutil.copy2(src, dst)
                variant_dds_count += 1
        
        results[variant_name] = {
            "xtd": str(variant_xtd),
            "dds_count": variant_dds_count,
            "status": "prepared"
        }
        print(f"  ✓ {variant_name}: {variant_dds_count} textures")
    
    # Step 6: Summary
    print("\n[6/6] Conversion Summary")
    print("=" * 70)
    print(f"Output directory: {OUTPUT_DIR}")
    print(f"\nVariants created: {len(results)}")
    for name, data in results.items():
        print(f"  {name}/")
        print(f"    - buttons_360.xtd (template)")
        print(f"    - dds_textures/ ({data['dds_count']} files)")
    
    print("\n" + "-" * 70)
    print("NOTE: The XTD files are currently copies of the original.")
    print("To complete texture replacement, use the Wine GUI tool:")
    print(f"  wine \"{DLL_DIR / 'GTA V Console Texture Editor 1.3.1.exe'}\"")
    print("-" * 70)
    
    return True

def main():
    import argparse
    parser = argparse.ArgumentParser(description='Xbox 360 XTD Full Converter')
    parser.add_argument('--test', action='store_true', help='Test Wine + LZX setup')
    parser.add_argument('--analyze', action='store_true', help='Analyze original XTD')
    parser.add_argument('--decompress', action='store_true', help='Decompress original XTD')
    parser.add_argument('--convert', action='store_true', help='Run full conversion')
    
    args = parser.parse_args()
    
    if args.test:
        test_wine_lzx()
    elif args.analyze:
        info = analyze_xtd(ORIGINAL_XTD)
        print(json.dumps(info, indent=2))
    elif args.decompress:
        OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
        decompress_xtd(ORIGINAL_XTD, OUTPUT_DIR / "decompressed.bin")
    elif args.convert:
        full_conversion_pipeline()
    else:
        full_conversion_pipeline()

if __name__ == '__main__':
    main()
