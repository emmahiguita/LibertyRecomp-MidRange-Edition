#!/usr/bin/env python3
"""
Xbox 360 XTD Texture Dictionary Tool
Handles RSC v5 format with LZX compression for GTA IV Xbox 360 textures.

Based on reverse engineering from RAGE Console Texture Editor source code.
"""

import struct
import os
import sys
import json
import subprocess
from pathlib import Path
from typing import Dict, List, Tuple, Optional
from dataclasses import dataclass

# RSC Header Constants
RSC_MAGIC_V5 = 0x52534305  # "RSC\x05" (big-endian)
RSC_MAGIC_V5_LE = 0x05435352  # Little-endian variant
RESOURCE_TYPE_TEXTURE_DICT = 0x07

@dataclass
class RSCHeader:
    """RSC (RAGE Script Container) header structure."""
    magic: int
    resource_type: int
    flags: int
    cpu_size: int
    gpu_size: int
    is_big_endian: bool
    
    @classmethod
    def from_bytes(cls, data: bytes) -> 'RSCHeader':
        """Parse RSC header from bytes."""
        if len(data) < 12:
            raise ValueError("Data too short for RSC header")
        
        magic = struct.unpack('>I', data[0:4])[0]
        
        # Check endianness - RSC v5 files are always big-endian for Xbox 360
        if magic == RSC_MAGIC_V5:
            is_big_endian = True
            # Resource type is stored as big-endian, swap to get actual value
            resource_type_raw = struct.unpack('>I', data[4:8])[0]
            resource_type = resource_type_raw >> 24  # Upper byte is the type
            flags = struct.unpack('>I', data[8:12])[0]
        elif magic == RSC_MAGIC_V5_LE:
            is_big_endian = False
            magic = RSC_MAGIC_V5  # Normalize
            resource_type = struct.unpack('<I', data[4:8])[0] & 0xFF
            flags = struct.unpack('<I', data[8:12])[0]
        else:
            raise ValueError(f"Invalid RSC magic: 0x{magic:08X}")
        
        # Calculate CPU and GPU segment sizes from flags
        # From source: dwCPUSize := (dwFlags AND $7FF) shl (((dwFlags shr 11) AND $F) + 8);
        cpu_size = (flags & 0x7FF) << (((flags >> 11) & 0xF) + 8)
        # From source: dwGPUSize := ((dwFlags shr 15) AND $7FF) shl (((dwFlags shr 26) AND $F)+8);
        gpu_size = ((flags >> 15) & 0x7FF) << (((flags >> 26) & 0xF) + 8)
        
        return cls(
            magic=magic,
            resource_type=resource_type,
            flags=flags,
            cpu_size=cpu_size,
            gpu_size=gpu_size,
            is_big_endian=is_big_endian
        )

@dataclass
class XTDHeader:
    """Texture Dictionary header (after decompression)."""
    vmt: int
    offset_map_offset: int
    hash_table_offset: int
    texture_count: int
    texture_list_offset: int

@dataclass  
class TextureInfo:
    """Information about a texture in the XTD."""
    name: str
    width: int
    height: int
    format: int  # GPU texture format ID
    offset: int
    data_size: int

def swap_endian_word(data: bytes) -> bytes:
    """Swap bytes in each 16-bit word (GPUENDIAN_8IN16)."""
    result = bytearray(len(data))
    for i in range(0, len(data) - 1, 2):
        result[i] = data[i + 1]
        result[i + 1] = data[i]
    return bytes(result)

def swap_endian_dword(data: bytes) -> bytes:
    """Swap bytes in each 32-bit dword (GPUENDIAN_8IN32)."""
    result = bytearray(len(data))
    for i in range(0, len(data) - 3, 4):
        result[i] = data[i + 3]
        result[i + 1] = data[i + 2]
        result[i + 2] = data[i + 1]
        result[i + 3] = data[i]
    return bytes(result)

def xg_address_2d_tiled_offset(x: int, y: int, w: int, texel_pitch: int) -> int:
    """
    Xbox 360 texture tiling/swizzling offset calculation.
    Direct port from Console.Xbox360.Swizzling.pas
    """
    aligned_width = (w + 31) & ~31
    log_bpp = (texel_pitch >> 2) + ((texel_pitch >> 1) >> (texel_pitch >> 2))
    
    macro = ((x >> 5) + (y >> 5) * (aligned_width >> 5)) << (log_bpp + 7)
    micro = (((x & 7) + ((y & 6) << 2)) << log_bpp)
    
    offset = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4)
    
    result = (((offset & ~511) << 3) + ((offset & 448) << 2) + (offset & 63) + 
              ((y & 16) << 7) + (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >> log_bpp
    
    return result

def untile_texture(tiled_data: bytes, width: int, height: int, texel_pitch: int) -> bytes:
    """Untile (unswizzle) Xbox 360 texture data."""
    # For DXT compressed textures, work with 4x4 blocks
    block_width = width // 4
    block_height = height // 4
    
    untiled = bytearray(len(tiled_data))
    
    for y in range(block_height):
        for x in range(block_width):
            src_offset = xg_address_2d_tiled_offset(x, y, block_width, texel_pitch)
            dst_offset = (x + y * block_width) * texel_pitch
            
            if src_offset * texel_pitch + texel_pitch <= len(tiled_data):
                untiled[dst_offset:dst_offset + texel_pitch] = \
                    tiled_data[src_offset * texel_pitch:(src_offset + 1) * texel_pitch]
    
    return bytes(untiled)

def tile_texture(untiled_data: bytes, width: int, height: int, texel_pitch: int) -> bytes:
    """Tile (swizzle) texture data for Xbox 360."""
    block_width = width // 4
    block_height = height // 4
    
    tiled = bytearray(len(untiled_data))
    
    for y in range(block_height):
        for x in range(block_width):
            src_offset = (x + y * block_width) * texel_pitch
            dst_offset = xg_address_2d_tiled_offset(x, y, block_width, texel_pitch)
            
            if dst_offset * texel_pitch + texel_pitch <= len(tiled):
                tiled[dst_offset * texel_pitch:(dst_offset + 1) * texel_pitch] = \
                    untiled_data[src_offset:src_offset + texel_pitch]
    
    return bytes(tiled)

def parse_dds_header(data: bytes) -> Dict:
    """Parse DDS file header."""
    if data[0:4] != b'DDS ':
        raise ValueError("Not a DDS file")
    
    # DDS header is 128 bytes
    header_size = struct.unpack('<I', data[4:8])[0]
    flags = struct.unpack('<I', data[8:12])[0]
    height = struct.unpack('<I', data[12:16])[0]
    width = struct.unpack('<I', data[16:20])[0]
    pitch_or_linear_size = struct.unpack('<I', data[20:24])[0]
    depth = struct.unpack('<I', data[24:28])[0]
    mip_map_count = struct.unpack('<I', data[28:32])[0]
    
    # Pixel format at offset 76
    pf_size = struct.unpack('<I', data[76:80])[0]
    pf_flags = struct.unpack('<I', data[80:84])[0]
    fourcc = data[84:88]
    
    return {
        'width': width,
        'height': height,
        'mip_map_count': mip_map_count,
        'fourcc': fourcc,
        'data_offset': 128,
        'data_size': len(data) - 128
    }

def get_gpu_texture_format(fourcc: bytes) -> int:
    """Convert DDS FourCC to Xbox 360 GPU texture format ID."""
    format_map = {
        b'DXT1': 18,  # GPUTEXTUREFORMAT_DXT1
        b'DXT2': 19,  # GPUTEXTUREFORMAT_DXT2_3
        b'DXT3': 19,  # GPUTEXTUREFORMAT_DXT2_3
        b'DXT4': 20,  # GPUTEXTUREFORMAT_DXT4_5
        b'DXT5': 20,  # GPUTEXTUREFORMAT_DXT4_5
    }
    return format_map.get(fourcc, 0)

def analyze_xtd(filepath: str) -> Dict:
    """Analyze an XTD file and return information about its structure."""
    with open(filepath, 'rb') as f:
        data = f.read()
    
    header = RSCHeader.from_bytes(data)
    
    # Compressed size is file size minus 16-byte RSC header
    # The DWORD at offset 12 is part of the LZX stream header, not the total size
    compressed_size = len(data) - 16
    
    return {
        'file_size': len(data),
        'magic': f"0x{header.magic:08X}",
        'resource_type': header.resource_type,
        'flags': f"0x{header.flags:08X}",
        'cpu_size': header.cpu_size,
        'gpu_size': header.gpu_size,
        'total_decompressed_size': header.cpu_size + header.gpu_size,
        'is_big_endian': header.is_big_endian,
        'compressed_size': compressed_size,
        'header_size': 16
    }

def print_analysis(info: Dict):
    """Print XTD analysis results."""
    print("=" * 60)
    print("XTD File Analysis")
    print("=" * 60)
    print(f"File Size:              {info['file_size']:,} bytes")
    print(f"Magic:                  {info['magic']}")
    print(f"Resource Type:          {info['resource_type']}")
    print(f"Flags:                  {info['flags']}")
    print(f"CPU Segment Size:       {info['cpu_size']:,} bytes")
    print(f"GPU Segment Size:       {info['gpu_size']:,} bytes")
    print(f"Total Decompressed:     {info['total_decompressed_size']:,} bytes")
    print(f"Compressed Size:        {info['compressed_size']:,} bytes")
    print(f"Big Endian:             {info['is_big_endian']}")
    print("=" * 60)

def main():
    """Main entry point."""
    import argparse
    
    parser = argparse.ArgumentParser(description='Xbox 360 XTD Texture Tool')
    subparsers = parser.add_subparsers(dest='command', help='Commands')
    
    # Analyze command
    analyze_parser = subparsers.add_parser('analyze', help='Analyze XTD file')
    analyze_parser.add_argument('file', help='XTD file to analyze')
    
    # List command (placeholder for future)
    list_parser = subparsers.add_parser('list', help='List textures in XTD')
    list_parser.add_argument('file', help='XTD file')
    
    args = parser.parse_args()
    
    if args.command == 'analyze':
        info = analyze_xtd(args.file)
        print_analysis(info)
    elif args.command == 'list':
        print("Note: Listing textures requires LZX decompression.")
        print("The XTD file is LZX compressed - need to decompress first.")
        info = analyze_xtd(args.file)
        print_analysis(info)
    else:
        parser.print_help()

if __name__ == '__main__':
    main()
