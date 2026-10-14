#!/usr/bin/env python3
"""
LZX Decompression Wrapper using Wine + xcompress DLLs
Works on macOS by invoking Wine to run Windows DLLs.
"""

import os
import sys
import struct
import subprocess
import tempfile
from pathlib import Path

# Path to Wine executable
WINE_PATH = "/opt/homebrew/bin/wine"

# Path to xcompress DLLs (from RAGE Console Texture Editor)
SCRIPT_DIR = Path(__file__).parent
DLL_DIR = SCRIPT_DIR / "full" / "RAGE-Console-Texture-Editor-master"

def read_lzx_block_header(data: bytes, offset: int) -> tuple:
    """
    Read LZX block header from Xbox 360 resource.
    Returns (compressed_size, uncompressed_size, new_offset)
    
    Block format (from Compression.LZX.pas ReadBlockSize):
    - If first byte is 0xFF: 5-byte header
      - b0 = 0xFF marker
      - b1,b2 = uncompressed size (big-endian)
      - b3,b4 = compressed size (big-endian)
    - Otherwise: 2-byte header
      - b0,b1 = compressed size (big-endian)
      - uncompressed size is fixed at 0x8000 (32KB)
    """
    if offset >= len(data):
        return (0, 0, offset)
    
    first_byte = data[offset]
    
    if first_byte == 0xFF:  # 5-byte extended header
        if offset + 5 > len(data):
            return (0, 0, offset)
        b1, b2 = data[offset + 1], data[offset + 2]
        b3, b4 = data[offset + 3], data[offset + 4]
        uncompressed_size = (b1 << 8) | b2
        compressed_size = (b3 << 8) | b4
        return (compressed_size, uncompressed_size, offset + 5)
    else:  # 2-byte header, fixed 32KB uncompressed
        if offset + 2 > len(data):
            return (0, 0, offset)
        b0, b1 = data[offset], data[offset + 1]
        compressed_size = (b0 << 8) | b1
        uncompressed_size = 0x8000  # Fixed 32KB blocks
        return (compressed_size, uncompressed_size, offset + 2)

def decompress_lzx_blocks(compressed_data: bytes, expected_size: int) -> bytes:
    """
    Decompress LZX compressed data block by block.
    Xbox 360 resources use a block-based LZX format.
    
    Note: This is a simplified implementation. Full implementation
    requires the actual LZX decompression algorithm.
    """
    result = bytearray()
    offset = 0
    
    while len(result) < expected_size and offset < len(compressed_data):
        comp_size, uncomp_size, offset = read_lzx_block_header(compressed_data, offset)
        
        if comp_size == 0 or uncomp_size == 0:
            break
            
        block_data = compressed_data[offset:offset + comp_size]
        offset += comp_size
        
        # For now, we can't decompress without the DLL
        # This would require Wine subprocess call
        print(f"Block: compressed={comp_size}, uncompressed={uncomp_size}")
        
        # Placeholder - need actual decompression
        result.extend(b'\x00' * uncomp_size)
    
    return bytes(result)

def check_wine_available() -> bool:
    """Check if Wine is available."""
    try:
        result = subprocess.run([WINE_PATH, "--version"], 
                              capture_output=True, timeout=5)
        return result.returncode == 0
    except Exception:
        return False

def check_dlls_available() -> bool:
    """Check if required DLLs are present."""
    required = ["xcompress.dll", "xcompress_cpp.dll", "xcompress_open.dll"]
    for dll in required:
        if not (DLL_DIR / dll).exists():
            print(f"Missing DLL: {dll}")
            return False
    return True

def main():
    """Main entry point for testing."""
    print("LZX Wine Wrapper Status:")
    print(f"  Wine available: {check_wine_available()}")
    print(f"  DLLs available: {check_dlls_available()}")
    print(f"  DLL directory: {DLL_DIR}")
    
    if len(sys.argv) > 1:
        # Read RSC file and show LZX block structure
        filepath = sys.argv[1]
        with open(filepath, 'rb') as f:
            data = f.read()
        
        print(f"\nFile: {filepath}")
        print(f"Size: {len(data)} bytes")
        
        # Skip 16-byte RSC header
        compressed = data[16:]
        print(f"Compressed data: {len(compressed)} bytes")
        print(f"First 8 bytes: {compressed[:8].hex()}")
        
        # Try to read block headers
        offset = 0
        block_num = 0
        total_uncompressed = 0
        print("\nLZX Block Structure:")
        while offset < len(compressed):
            comp_size, uncomp_size, new_offset = read_lzx_block_header(compressed, offset)
            if comp_size == 0 or comp_size > len(compressed) - new_offset:
                break
            total_uncompressed += uncomp_size
            if block_num < 10:
                print(f"  Block {block_num}: offset=0x{offset:04X}, comp={comp_size}, uncomp={uncomp_size}")
            offset = new_offset + comp_size
            block_num += 1
        
        if block_num >= 10:
            print(f"  ... ({block_num} total blocks)")
        print(f"\nTotal blocks: {block_num}")
        print(f"Total uncompressed: {total_uncompressed:,} bytes")

if __name__ == '__main__':
    main()
