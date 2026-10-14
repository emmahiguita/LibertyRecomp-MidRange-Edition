#!/usr/bin/env python3
"""
XTD Complete Packer - Full Xbox 360 XTD texture replacement pipeline
Uses Wine + xcompress DLLs for LZX compression/decompression
"""

import os
import sys
import json
import struct
import subprocess
import shutil
from pathlib import Path

# Configuration
WINE_PATH = "/opt/homebrew/bin/wine"
SCRIPT_DIR = Path(__file__).parent
LZX_CLI = SCRIPT_DIR / "lzx_cli.exe"
ORIGINAL_XTD = Path("/Users/ozordi/Library/Application Support/LibertyRecomp/game/xbox360/textures/buttons_360.xtd")
OUTPUT_DIR = SCRIPT_DIR / "output"
SOURCE_DDS_DIR = Path("/Users/Ozordi/Downloads/LibertyRecomp/tools/GTAIV.EFLC.FusionFix-master/textures/pc/textures/buttons_360.wtd")

# Controller variant prefixes
VARIANTS = {
    "xbox360": "",           # Default Xbox 360 (no prefix)
    "ps3": "ps3_",
    "ps4": "ps4_",
    "ps5": "ps5_",
    "steam_controller": "sc_",
    "steam_deck": "sd_",
    "switch": "switch_",
    "xbox_one": "xbone_",
    "xbox_series_x": "xsx_"
}

def run_wine(args: list[str], cwd: Path = None) -> tuple[int, str, str]:
    """Run Wine command and return exit code, stdout, stderr"""
    cmd = [WINE_PATH] + args
    env = os.environ.copy()
    env['WINEDEBUG'] = '-all'  # Suppress Wine debug messages
    
    result = subprocess.run(
        cmd,
        cwd=str(cwd or SCRIPT_DIR),
        capture_output=True,
        text=True,
        env=env
    )
    return result.returncode, result.stdout, result.stderr

def win_path(path: Path) -> str:
    """Convert Unix path to Wine Z: path"""
    unix_path = str(path.resolve())
    win_path_str = unix_path.replace('/', '\\\\')
    return f"Z:{win_path_str}"

def decompress_xtd(xtd_path: Path, output_path: Path) -> bool:
    """Decompress XTD file using lzx_cli"""
    # Read XTD and skip RSC header (20 bytes)
    with open(xtd_path, 'rb') as f:
        data = f.read()
    
    # Save compressed data (skip 20-byte header)
    lzx_path = output_path.with_suffix('.lzx')
    with open(lzx_path, 'wb') as f:
        f.write(data[20:])
    
    # Decompress using Wine
    code, stdout, stderr = run_wine([
        str(LZX_CLI),
        "decompress",
        win_path(lzx_path),
        win_path(output_path)
    ])
    
    if code != 0:
        print(f"Decompression failed: {stderr}")
        return False
    
    print(f"Decompressed: {output_path} ({output_path.stat().st_size} bytes)")
    return True

def compress_xtd(input_path: Path, output_path: Path, original_header: bytes) -> bool:
    """Compress data to XTD format"""
    lzx_path = input_path.with_suffix('.lzx.out')
    
    code, stdout, stderr = run_wine([
        str(LZX_CLI),
        "compress",
        win_path(input_path),
        win_path(lzx_path)
    ])
    
    if code != 0:
        print(f"Compression failed: {stderr}")
        return False
    
    # Read compressed data
    with open(lzx_path, 'rb') as f:
        compressed = f.read()
    
    # Write XTD with original header
    with open(output_path, 'wb') as f:
        f.write(original_header[:16])  # RSC header
        # Write compressed size (big-endian)
        f.write(struct.pack('>I', len(compressed)))
        f.write(compressed)
    
    print(f"Created: {output_path} ({output_path.stat().st_size} bytes)")
    return True

def parse_xtd_header(data: bytes) -> dict:
    """Parse XTD (RSC v5) header"""
    magic = struct.unpack('<I', data[:4])[0]
    resource_type = struct.unpack('>I', data[4:8])[0]
    flags = struct.unpack('>I', data[8:12])[0]
    
    # Calculate segment sizes
    cpu_size = (flags & 0x7FF) << (((flags >> 11) & 0xF) + 8)
    gpu_size = ((flags >> 15) & 0x7FF) << (((flags >> 26) & 0xF) + 8)
    
    return {
        'magic': magic,
        'resource_type': resource_type,
        'flags': flags,
        'cpu_size': cpu_size,
        'gpu_size': gpu_size,
        'header': data[:20]
    }

def find_texture_offsets(data: bytes) -> list[dict]:
    """Find texture name and data offsets in decompressed XTD"""
    textures = []
    
    # Search for "pack:/" prefix to find texture names
    pos = 0
    while True:
        idx = data.find(b'pack:/', pos)
        if idx == -1:
            break
        
        # Read null-terminated string
        end = data.find(b'\x00', idx)
        if end == -1:
            break
        
        name = data[idx:end].decode('ascii')
        # Remove "pack:/" prefix and ".dds" suffix
        base_name = name[6:-4] if name.endswith('.dds') else name[6:]
        
        textures.append({
            'offset': idx,
            'full_name': name,
            'base_name': base_name
        })
        
        pos = end + 1
    
    return textures

def get_available_textures() -> dict[str, Path]:
    """Get all available source DDS files"""
    textures = {}
    for dds_file in SOURCE_DDS_DIR.glob("*.dds"):
        textures[dds_file.stem] = dds_file
    return textures

def create_variant_xtd(variant: str, prefix: str, work_dir: Path) -> bool:
    """Create XTD for a specific controller variant"""
    print(f"\n{'='*60}")
    print(f"Processing variant: {variant} (prefix: '{prefix or 'none'}')")
    print(f"{'='*60}")
    
    variant_dir = work_dir / variant
    variant_dir.mkdir(parents=True, exist_ok=True)
    
    # Step 1: Copy original XTD to work directory
    variant_xtd = variant_dir / "buttons_360.xtd"
    shutil.copy(ORIGINAL_XTD, variant_xtd)
    
    # Step 2: Read and parse original XTD
    with open(ORIGINAL_XTD, 'rb') as f:
        original_data = f.read()
    
    header_info = parse_xtd_header(original_data)
    print(f"  Original: {len(original_data)} bytes, CPU={header_info['cpu_size']}, GPU={header_info['gpu_size']}")
    
    # Step 3: Decompress
    decompressed_path = variant_dir / "decompressed.bin"
    if not decompress_xtd(ORIGINAL_XTD, decompressed_path):
        print(f"  ERROR: Decompression failed for {variant}")
        return False
    
    # Step 4: Find texture offsets
    with open(decompressed_path, 'rb') as f:
        decompressed_data = bytearray(f.read())
    
    textures = find_texture_offsets(decompressed_data)
    print(f"  Found {len(textures)} textures in XTD")
    
    # Step 5: Get available source textures
    source_textures = get_available_textures()
    
    # Step 6: Create mapping
    mapping = {}
    for tex in textures:
        base_name = tex['base_name']
        
        # Try to find variant-specific texture
        variant_name = f"{prefix}{base_name}" if prefix else base_name
        
        if variant_name in source_textures:
            mapping[base_name] = {
                'source': str(source_textures[variant_name]),
                'variant_name': variant_name
            }
        elif base_name in source_textures:
            # Fallback to default
            mapping[base_name] = {
                'source': str(source_textures[base_name]),
                'variant_name': base_name
            }
    
    print(f"  Mapped {len(mapping)} textures")
    
    # Step 7: Save mapping for reference
    mapping_file = variant_dir / "texture_mapping.json"
    with open(mapping_file, 'w') as f:
        json.dump({
            'variant': variant,
            'prefix': prefix,
            'textures': mapping
        }, f, indent=2)
    
    # Step 8: Copy DDS files to variant folder
    dds_dir = variant_dir / "dds_textures"
    dds_dir.mkdir(exist_ok=True)
    
    for base_name, info in mapping.items():
        src = Path(info['source'])
        dst = dds_dir / f"{base_name}.dds"
        shutil.copy(src, dst)
    
    print(f"  Copied {len(mapping)} DDS files to {dds_dir.name}/")
    
    # Note: Full texture replacement in the binary requires more complex
    # parsing of the D3DBaseTexture structures. For now, we output the
    # prepared files for manual processing with RAGE Console Texture Editor.
    
    return True

def main():
    print("="*60)
    print("Xbox 360 XTD Complete Packer")
    print("="*60)
    
    # Verify tools
    if not LZX_CLI.exists():
        print(f"ERROR: lzx_cli.exe not found at {LZX_CLI}")
        return 1
    
    if not ORIGINAL_XTD.exists():
        print(f"ERROR: Original XTD not found at {ORIGINAL_XTD}")
        return 1
    
    if not SOURCE_DDS_DIR.exists():
        print(f"ERROR: Source DDS directory not found at {SOURCE_DDS_DIR}")
        return 1
    
    # Create output directory
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    
    # Process each variant
    success_count = 0
    for variant, prefix in VARIANTS.items():
        try:
            if create_variant_xtd(variant, prefix, OUTPUT_DIR):
                success_count += 1
        except Exception as e:
            print(f"  ERROR: {e}")
    
    print(f"\n{'='*60}")
    print(f"COMPLETE: {success_count}/{len(VARIANTS)} variants processed")
    print(f"Output: {OUTPUT_DIR}")
    print(f"{'='*60}")
    
    print("""
NEXT STEPS:
-----------
The variant folders contain:
  - buttons_360.xtd (copy of original)
  - texture_mapping.json (texture mappings)
  - dds_textures/ (prepared DDS files)
  - decompressed.bin (raw texture dictionary)

To complete texture replacement, use RAGE Console Texture Editor via Wine:
  cd {output_dir}/<variant>
  wine "{script_dir}/full/RAGE-Console-Texture-Editor-master/GTA V Console Texture Editor 1.3.1.exe"
  
Then:
  1. File → Open → buttons_360.xtd
  2. For each texture, right-click → Import → select from dds_textures/
  3. File → Save
""".format(output_dir=OUTPUT_DIR, script_dir=SCRIPT_DIR))
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
