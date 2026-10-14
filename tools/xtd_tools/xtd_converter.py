#!/usr/bin/env python3
"""
Xbox 360 XTD Texture Dictionary Converter
Converts buttons_360.xtd with different controller texture variants.

This tool works by:
1. Using the original XTD as a template (keeping structure intact)
2. Replacing texture data with new DDS textures
3. Saving variant XTD files for each controller type
"""

import os
import sys
import struct
import shutil
import subprocess
import tempfile
from pathlib import Path
from typing import Dict, List, Tuple, Optional
from dataclasses import dataclass
import json

# Paths
SCRIPT_DIR = Path(__file__).parent
TOOLS_DIR = SCRIPT_DIR
DLL_DIR = SCRIPT_DIR / "full" / "RAGE-Console-Texture-Editor-master"
WINE_PATH = "/opt/homebrew/bin/wine"

# Source paths
SOURCE_DDS_DIR = Path("/Users/Ozordi/Downloads/LibertyRecomp/tools/GTAIV.EFLC.FusionFix-master/textures/pc/textures/buttons_360.wtd")
ORIGINAL_XTD = Path("/Users/ozordi/Library/Application Support/LibertyRecomp/game/xbox360/textures/buttons_360.xtd")
OUTPUT_DIR = SCRIPT_DIR / "output"

# Controller variants
CONTROLLER_VARIANTS = {
    "xbox360": "",           # Original (no prefix)
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
    magic: int
    resource_type: int
    flags: int
    compressed_size: int
    cpu_size: int
    gpu_size: int
    
    @classmethod
    def from_file(cls, filepath: Path) -> 'RSCHeader':
        with open(filepath, 'rb') as f:
            data = f.read(20)
        
        magic = struct.unpack('>I', data[0:4])[0]
        resource_type_raw = struct.unpack('>I', data[4:8])[0]
        resource_type = resource_type_raw >> 24
        flags = struct.unpack('>I', data[8:12])[0]
        compressed_size = struct.unpack('>I', data[16:20])[0]
        
        cpu_size = (flags & 0x7FF) << (((flags >> 11) & 0xF) + 8)
        gpu_size = ((flags >> 15) & 0x7FF) << (((flags >> 26) & 0xF) + 8)
        
        return cls(magic, resource_type, flags, compressed_size, cpu_size, gpu_size)

def get_available_dds_files() -> Dict[str, Path]:
    """Get all available DDS files from source directory."""
    dds_files = {}
    if SOURCE_DDS_DIR.exists():
        for f in SOURCE_DDS_DIR.glob("*.dds"):
            dds_files[f.stem.lower()] = f
    return dds_files

def get_base_texture_names(dds_files: Dict[str, Path]) -> List[str]:
    """Extract base texture names (without controller prefixes)."""
    base_names = set()
    prefixes = list(CONTROLLER_VARIANTS.values())
    prefixes.remove("")  # Remove empty prefix
    
    for name in dds_files.keys():
        is_variant = False
        for prefix in prefixes:
            if prefix and name.startswith(prefix):
                base_name = name[len(prefix):]
                base_names.add(base_name)
                is_variant = True
                break
        if not is_variant:
            base_names.add(name)
    
    return sorted(base_names)

def create_variant_folder(variant_name: str) -> Path:
    """Create output folder for a controller variant."""
    variant_dir = OUTPUT_DIR / variant_name
    variant_dir.mkdir(parents=True, exist_ok=True)
    return variant_dir

def copy_xtd_for_variant(variant_name: str) -> Path:
    """Copy original XTD to variant output folder."""
    variant_dir = create_variant_folder(variant_name)
    output_xtd = variant_dir / "buttons_360.xtd"
    shutil.copy2(ORIGINAL_XTD, output_xtd)
    return output_xtd

def get_variant_dds_mapping(variant_prefix: str, dds_files: Dict[str, Path], base_names: List[str]) -> Dict[str, Path]:
    """Get mapping of base texture names to variant DDS files."""
    mapping = {}
    for base_name in base_names:
        variant_name = f"{variant_prefix}{base_name}"
        if variant_name in dds_files:
            mapping[base_name] = dds_files[variant_name]
        elif base_name in dds_files:
            # Fall back to base texture if variant doesn't exist
            mapping[base_name] = dds_files[base_name]
    return mapping

def analyze_original_xtd():
    """Analyze the original XTD file."""
    header = RSCHeader.from_file(ORIGINAL_XTD)
    print(f"Original XTD Analysis:")
    print(f"  Magic: 0x{header.magic:08X}")
    print(f"  Resource Type: {header.resource_type}")
    print(f"  Flags: 0x{header.flags:08X}")
    print(f"  Compressed Size: {header.compressed_size:,} bytes")
    print(f"  CPU Size: {header.cpu_size:,} bytes")
    print(f"  GPU Size: {header.gpu_size:,} bytes")
    print(f"  Total Decompressed: {header.cpu_size + header.gpu_size:,} bytes")
    return header

def create_all_variants():
    """Create XTD files for all controller variants."""
    print("=" * 60)
    print("Xbox 360 XTD Texture Converter")
    print("=" * 60)
    
    # Check paths
    if not ORIGINAL_XTD.exists():
        print(f"ERROR: Original XTD not found: {ORIGINAL_XTD}")
        return False
    
    if not SOURCE_DDS_DIR.exists():
        print(f"ERROR: Source DDS directory not found: {SOURCE_DDS_DIR}")
        return False
    
    # Analyze original
    print("\n1. Analyzing original XTD...")
    header = analyze_original_xtd()
    
    # Get DDS files
    print("\n2. Scanning source DDS files...")
    dds_files = get_available_dds_files()
    print(f"   Found {len(dds_files)} DDS files")
    
    # Get base texture names
    base_names = get_base_texture_names(dds_files)
    print(f"   Base texture names: {len(base_names)}")
    
    # Create output directory
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    print(f"\n3. Output directory: {OUTPUT_DIR}")
    
    # For each variant, copy the original XTD
    # Since we can't decompress/recompress without native LZX,
    # we'll create copies and document the texture mapping
    print("\n4. Creating variant copies...")
    
    created_variants = []
    for variant_name, prefix in CONTROLLER_VARIANTS.items():
        variant_dir = create_variant_folder(variant_name)
        output_xtd = copy_xtd_for_variant(variant_name)
        
        # Get DDS mapping for this variant
        mapping = get_variant_dds_mapping(prefix, dds_files, base_names)
        
        # Save mapping info
        mapping_file = variant_dir / "texture_mapping.json"
        mapping_data = {
            "variant": variant_name,
            "prefix": prefix,
            "textures": {k: str(v) for k, v in mapping.items()}
        }
        with open(mapping_file, 'w') as f:
            json.dump(mapping_data, f, indent=2)
        
        # Copy the DDS files to the variant folder for manual replacement
        dds_folder = variant_dir / "dds_textures"
        dds_folder.mkdir(exist_ok=True)
        for base_name, dds_path in mapping.items():
            dest = dds_folder / f"{base_name}.dds"
            shutil.copy2(dds_path, dest)
        
        print(f"   ✓ {variant_name}: {len(mapping)} textures")
        created_variants.append(variant_name)
    
    # Create a batch script for Wine GUI processing
    create_wine_batch_script(created_variants, base_names)
    
    print("\n" + "=" * 60)
    print("CONVERSION PREPARATION COMPLETE")
    print("=" * 60)
    print(f"\nOutput folder: {OUTPUT_DIR}")
    print(f"\nVariants created: {len(created_variants)}")
    for v in created_variants:
        print(f"  - {v}/")
    
    print("\n" + "-" * 60)
    print("NEXT STEPS:")
    print("-" * 60)
    print("""
Each variant folder contains:
  - buttons_360.xtd (copy of original)
  - texture_mapping.json (texture name mapping)
  - dds_textures/ (DDS files to import)
  - process_variant.bat (Wine script for manual processing)

To complete conversion, run the Wine GUI for each variant:
  cd /Users/Ozordi/Downloads/LibertyRecomp/tools/xtd_tools/output/<variant>
  wine process_variant.bat

Or manually:
  1. Launch: wine "path/to/GTA V Console Texture Editor 1.3.1.exe"
  2. Open buttons_360.xtd from the variant folder
  3. For each texture, right-click → Import → select from dds_textures/
  4. Save the XTD
""")
    
    return True

def create_wine_batch_script(variants: List[str], base_names: List[str]):
    """Create batch scripts for Wine GUI processing."""
    editor_path = DLL_DIR / "GTA V Console Texture Editor 1.3.1.exe"
    
    for variant in variants:
        variant_dir = OUTPUT_DIR / variant
        script_path = variant_dir / "process_variant.bat"
        
        with open(script_path, 'w') as f:
            f.write("@echo off\n")
            f.write(f"REM Process {variant} variant textures\n")
            f.write(f"REM This script opens the texture editor for manual processing\n")
            f.write(f'echo Processing {variant} variant...\n')
            f.write(f'echo.\n')
            f.write(f'echo Instructions:\n')
            f.write(f'echo 1. File -^> Open -^> Select buttons_360.xtd from this folder\n')
            f.write(f'echo 2. For each texture, right-click -^> Import\n')
            f.write(f'echo 3. Select matching DDS from dds_textures folder\n')
            f.write(f'echo 4. File -^> Save\n')
            f.write(f'echo.\n')
            f.write(f'pause\n')
            # Use relative path to avoid Wine path issues
            f.write(f'start "" "{editor_path}"\n')

def main():
    """Main entry point."""
    import argparse
    parser = argparse.ArgumentParser(description='Xbox 360 XTD Texture Converter')
    parser.add_argument('--analyze', action='store_true', help='Only analyze original XTD')
    parser.add_argument('--list-dds', action='store_true', help='List available DDS files')
    parser.add_argument('--convert', action='store_true', help='Create all variant copies')
    
    args = parser.parse_args()
    
    if args.analyze:
        analyze_original_xtd()
    elif args.list_dds:
        dds_files = get_available_dds_files()
        print(f"Found {len(dds_files)} DDS files:")
        for name, path in sorted(dds_files.items()):
            print(f"  {name}: {path.name}")
    elif args.convert:
        create_all_variants()
    else:
        # Default: run full conversion
        create_all_variants()

if __name__ == '__main__':
    main()
