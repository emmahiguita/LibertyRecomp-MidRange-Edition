#!/usr/bin/env python3
"""Quick script to analyze DDS file headers and compare files."""

import os
import struct
from pathlib import Path

def analyze_dds(filepath):
    """Analyze a DDS file header."""
    with open(filepath, 'rb') as f:
        data = f.read(128)  # DDS header is 128 bytes
    
    magic = data[0:4]
    if magic != b'DDS ':
        return {"error": "Not a valid DDS file", "magic": magic.hex()}
    
    # DDS_HEADER starts at offset 4
    size = struct.unpack('<I', data[4:8])[0]
    flags = struct.unpack('<I', data[8:12])[0]
    height = struct.unpack('<I', data[12:16])[0]
    width = struct.unpack('<I', data[16:20])[0]
    pitch_or_linear_size = struct.unpack('<I', data[20:24])[0]
    depth = struct.unpack('<I', data[24:28])[0]
    mip_count = struct.unpack('<I', data[28:32])[0]
    
    # DDS_PIXELFORMAT starts at offset 76 (4 + 72)
    pf_size = struct.unpack('<I', data[76:80])[0]
    pf_flags = struct.unpack('<I', data[80:84])[0]
    fourcc = data[84:88]
    rgb_bit_count = struct.unpack('<I', data[88:92])[0]
    
    file_size = os.path.getsize(filepath)
    
    return {
        "file": str(filepath),
        "file_size": file_size,
        "magic": "DDS",
        "width": width,
        "height": height,
        "mip_count": mip_count,
        "fourcc": fourcc.decode('ascii', errors='replace'),
        "pf_flags": hex(pf_flags),
        "linear_size": pitch_or_linear_size,
    }

def main():
    # Define paths
    source_dir = Path("/Users/Ozordi/Downloads/LibertyRecomp/tools/GTAIV.EFLC.FusionFix-master/textures/pc/textures/buttons_360.wtd")
    ps4_dds_dir = Path("/Users/Ozordi/Downloads/LibertyRecomp/tools/xtd_tools/output/ps4/dds_textures")
    ps4_extracted = Path("/Users/Ozordi/Downloads/LibertyRecomp/tools/xtd_tools/output/ps4_final_extracted")
    
    files_to_check = [
        ("ps4_rt_butt.dds", "rt_butt.dds"),
        ("ps4_start_butt.dds", "start_butt.dds"),
        ("ps4_y_butt.dds", "y_butt.dds"),
        ("ps4_lstick_none.dds", "lstick_none.dds"),
        ("ps4_lstick_right.dds", "lstick_right.dds"),
        ("ps4_rstick_updown.dds", "rstick_updown.dds"),
    ]
    
    print("=" * 80)
    print("DDS File Analysis - Comparing Source vs Extracted")
    print("=" * 80)
    
    for source_name, base_name in files_to_check:
        print(f"\n### {base_name}")
        print("-" * 40)
        
        # Source file (PS4 variant)
        source_path = source_dir / source_name
        if source_path.exists():
            info = analyze_dds(source_path)
            print(f"SOURCE ({source_name}):")
            print(f"  Size: {info['file_size']} bytes")
            print(f"  Dimensions: {info['width']}x{info['height']}")
            print(f"  FourCC: {info['fourcc']}")
            print(f"  Mips: {info['mip_count']}")
        else:
            print(f"SOURCE: NOT FOUND at {source_path}")
        
        # Prepared DDS (copied for import)
        prepared_path = ps4_dds_dir / base_name
        if prepared_path.exists():
            info = analyze_dds(prepared_path)
            print(f"PREPARED ({base_name}):")
            print(f"  Size: {info['file_size']} bytes")
            print(f"  Dimensions: {info['width']}x{info['height']}")
            print(f"  FourCC: {info['fourcc']}")
            print(f"  Mips: {info['mip_count']}")
        else:
            print(f"PREPARED: NOT FOUND at {prepared_path}")
        
        # Extracted from XTD
        extracted_path = ps4_extracted / base_name
        if extracted_path.exists():
            info = analyze_dds(extracted_path)
            print(f"EXTRACTED ({base_name}):")
            print(f"  Size: {info['file_size']} bytes")
            print(f"  Dimensions: {info['width']}x{info['height']}")
            print(f"  FourCC: {info['fourcc']}")
            print(f"  Mips: {info['mip_count']}")
        else:
            print(f"EXTRACTED: NOT FOUND at {extracted_path}")
    
    # Also check Xbox 360 original textures
    print("\n" + "=" * 80)
    print("Xbox 360 Original Textures for comparison")
    print("=" * 80)
    
    xbox360_names = ["rt_butt.dds", "start_butt.dds", "y_butt.dds"]
    for name in xbox360_names:
        path = source_dir / name
        if path.exists():
            info = analyze_dds(path)
            print(f"\nXBOX360 {name}:")
            print(f"  Size: {info['file_size']} bytes")
            print(f"  Dimensions: {info['width']}x{info['height']}")
            print(f"  FourCC: {info['fourcc']}")
            print(f"  Mips: {info['mip_count']}")

if __name__ == '__main__':
    main()
