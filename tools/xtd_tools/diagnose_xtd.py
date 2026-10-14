#!/usr/bin/env python3
"""
Diagnose XTD conversion issues by comparing original vs created files.
"""

import os
import struct
from pathlib import Path

def read_xtd_header(filepath):
    """Read and parse XTD (RSC5) header."""
    with open(filepath, 'rb') as f:
        data = f.read(24)
    
    magic = struct.unpack('<I', data[0:4])[0]
    
    # Determine endianness
    if magic == 0x52534305:  # 'RSC\x05' big-endian
        is_be = True
        resource_type = struct.unpack('>I', data[4:8])[0] >> 24
        flags = struct.unpack('>I', data[8:12])[0]
    else:
        is_be = False
        resource_type = struct.unpack('<I', data[4:8])[0] & 0xFF
        flags = struct.unpack('<I', data[8:12])[0]
    
    # Calculate segment sizes
    cpu_size = (flags & 0x7FF) << (((flags >> 11) & 0xF) + 8)
    gpu_size = ((flags >> 15) & 0x7FF) << (((flags >> 26) & 0xF) + 8)
    
    file_size = os.path.getsize(filepath)
    
    return {
        'path': str(filepath),
        'file_size': file_size,
        'magic': hex(magic),
        'big_endian': is_be,
        'resource_type': resource_type,
        'flags': hex(flags),
        'cpu_size': cpu_size,
        'gpu_size': gpu_size,
        'total_decompressed': cpu_size + gpu_size,
        'compressed_data_size': file_size - 20,
    }

def read_dds_header(filepath):
    """Read DDS file header."""
    with open(filepath, 'rb') as f:
        data = f.read(128)
    
    if data[0:4] != b'DDS ':
        return {'error': 'Not a valid DDS file'}
    
    height = struct.unpack('<I', data[12:16])[0]
    width = struct.unpack('<I', data[16:20])[0]
    linear_size = struct.unpack('<I', data[20:24])[0]
    mip_count = struct.unpack('<I', data[28:32])[0]
    fourcc = data[84:88].decode('ascii', errors='replace')
    
    file_size = os.path.getsize(filepath)
    data_size = file_size - 128
    
    return {
        'path': str(filepath),
        'file_size': file_size,
        'data_size': data_size,
        'width': width,
        'height': height,
        'fourcc': fourcc,
        'mip_count': mip_count,
        'linear_size': linear_size,
    }

def main():
    print("=" * 80)
    print("XTD DIAGNOSTIC REPORT")
    print("=" * 80)
    
    # Compare XTD files
    original_xtd = Path("/Users/ozordi/Library/Application Support/LibertyRecomp/game/xbox360/textures/buttons_360.xtd")
    ps4_xtd = Path("/Users/Ozordi/Downloads/LibertyRecomp/tools/xtd_tools/output/ps4/buttons_360.xtd")
    
    print("\n### XTD FILE COMPARISON ###")
    print("-" * 40)
    
    if original_xtd.exists():
        info = read_xtd_header(original_xtd)
        print(f"\nORIGINAL: {info['path']}")
        print(f"  File Size: {info['file_size']:,} bytes")
        print(f"  Magic: {info['magic']}")
        print(f"  Big Endian: {info['big_endian']}")
        print(f"  Resource Type: {info['resource_type']}")
        print(f"  Flags: {info['flags']}")
        print(f"  CPU Size: {info['cpu_size']:,} bytes")
        print(f"  GPU Size: {info['gpu_size']:,} bytes")
        print(f"  Total Decompressed: {info['total_decompressed']:,} bytes")
        print(f"  Compressed Data: {info['compressed_data_size']:,} bytes")
    
    if ps4_xtd.exists():
        info = read_xtd_header(ps4_xtd)
        print(f"\nPS4 CREATED: {info['path']}")
        print(f"  File Size: {info['file_size']:,} bytes")
        print(f"  Magic: {info['magic']}")
        print(f"  Big Endian: {info['big_endian']}")
        print(f"  Resource Type: {info['resource_type']}")
        print(f"  Flags: {info['flags']}")
        print(f"  CPU Size: {info['cpu_size']:,} bytes")
        print(f"  GPU Size: {info['gpu_size']:,} bytes")
        print(f"  Total Decompressed: {info['total_decompressed']:,} bytes")
        print(f"  Compressed Data: {info['compressed_data_size']:,} bytes")
    else:
        print(f"\nPS4 CREATED: NOT FOUND at {ps4_xtd}")
    
    # Compare DDS files
    print("\n\n### DDS FILE COMPARISON ###")
    print("-" * 40)
    
    source_dir = Path("/Users/Ozordi/Downloads/LibertyRecomp/tools/GTAIV.EFLC.FusionFix-master/textures/pc/textures/buttons_360.wtd")
    extracted_dir = Path("/Users/Ozordi/Downloads/LibertyRecomp/tools/xtd_tools/output/ps4_extracted")
    
    test_files = ["rt_butt", "start_butt", "y_butt", "a_butt", "lstick_none"]
    
    for name in test_files:
        print(f"\n### {name} ###")
        
        # Source (PS4 variant)
        source_path = source_dir / f"ps4_{name}.dds"
        if source_path.exists():
            info = read_dds_header(source_path)
            print(f"  SOURCE (ps4_{name}.dds):")
            print(f"    Size: {info['file_size']} bytes, Data: {info['data_size']} bytes")
            print(f"    Dimensions: {info['width']}x{info['height']}, FourCC: {info['fourcc']}")
        else:
            # Try base name
            source_path = source_dir / f"{name}.dds"
            if source_path.exists():
                info = read_dds_header(source_path)
                print(f"  SOURCE ({name}.dds):")
                print(f"    Size: {info['file_size']} bytes, Data: {info['data_size']} bytes")
                print(f"    Dimensions: {info['width']}x{info['height']}, FourCC: {info['fourcc']}")
        
        # Extracted
        extracted_path = extracted_dir / f"{name}.dds"
        if extracted_path.exists():
            info = read_dds_header(extracted_path)
            print(f"  EXTRACTED ({name}.dds):")
            print(f"    Size: {info['file_size']} bytes, Data: {info['data_size']} bytes")
            print(f"    Dimensions: {info['width']}x{info['height']}, FourCC: {info['fourcc']}")
        else:
            print(f"  EXTRACTED: NOT FOUND")
    
    # Check if DDS data sizes match expectations
    print("\n\n### DDS SIZE VALIDATION ###")
    print("-" * 40)
    print("Expected DXT5 sizes for common dimensions:")
    for w, h in [(64, 64), (128, 128), (32, 32)]:
        expected = (w // 4) * (h // 4) * 16
        print(f"  {w}x{h} DXT5: {expected} bytes data + 128 header = {expected + 128} bytes total")
    
    print("\n" + "=" * 80)
    print("DIAGNOSIS COMPLETE")
    print("=" * 80)

if __name__ == '__main__':
    main()
