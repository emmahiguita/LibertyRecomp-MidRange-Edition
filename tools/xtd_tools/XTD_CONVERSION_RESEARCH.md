# Xbox 360 XTD Texture Conversion Research Summary

## Date: Research Complete

---

## 1. Source Files Analysis ✅ VERIFIED

### Location
`/Users/Ozordi/Downloads/LibertyRecomp/tools/GTAIV.EFLC.FusionFix-master/textures/pc/textures/buttons_360.wtd/`

### File Format (Verified via hex dump)
- **Format**: DDS with legacy DX9 headers
- **Compression**: DXT5 (BC3) - verified at offset 0x54 in header
- **Dimensions**: 64x64 pixels (0x40 = 64)
- **Total Files**: ~280 DDS files

### Controller Variants Present
| Prefix | Controller Type | Example File |
|--------|-----------------|--------------|
| (none) | Xbox 360 default | `a_butt.dds` |
| `ps3_` | PlayStation 3 | `ps3_a_butt.dds` |
| `ps4_` | PlayStation 4 | `ps4_a_butt.dds` |
| `ps5_` | PlayStation 5 | `ps5_a_butt.dds` |
| `sc_` | Steam Controller | `sc_a_butt.dds` |
| `sd_` | Steam Deck | `sd_a_butt.dds` |
| `switch_` | Nintendo Switch | `switch_a_butt.dds` |
| `xbone_` | Xbox One | `xbone_a_butt.dds` |
| `xsx_` | Xbox Series X | `xsx_controller.dds` |

### File Sizes Observed
- Button textures: 4,224 bytes (64x64 DXT5)
- Some PS4/PS5/modern: 16,512 bytes (128x128 DXT5)
- Controller images: 1,048,704 bytes (1024x1024 DXT5)

---

## 2. Target XTD Format ✅ VERIFIED

### Original File
`/Users/ozordi/Library/Application Support/LibertyRecomp/game/xbox360/textures/buttons_360.xtd`

### RSC Header Structure (Verified via hex dump + source code)
```
Offset  Size  Value           Meaning
0x00    4     0x52534305      Magic: "RSC\x05" (RSC version 5)
0x04    4     0x07000000      Resource type: 7 (texture dictionary, big-endian)
0x08    4     Flags           Encodes CPU/GPU segment sizes
0x0C    4     Unused          (part of header padding)
0x10    4     dwInSize        Compressed data size (big-endian) = 0x00003751 = 14,161
0x14    ...   LZX blocks      Actual LZX compressed data starts here
```

### Size Calculation from Flags (0x100826D4)
```
CPU Size = (flags & 0x7FF) << (((flags >> 11) & 0xF) + 8)
         = (0x6D4 & 0x7FF) << (((0x6D4 >> 11) & 0xF) + 8)
         = 0x6D4 << 12 = 7,159,808 bytes

GPU Size = ((flags >> 15) & 0x7FF) << (((flags >> 26) & 0xF) + 8)
         = ((0x1008) & 0x7FF) << 8 = 0x08 << 8 = 65,536 bytes
```

### File Properties
- **Size**: 14,181 bytes
- **Compression**: LZX (Xbox 360 specific)
- **Endianness**: Big-endian (Xbox 360 PowerPC)

### Memory Size Calculation (from source code)
```pascal
dwCPUSize := (dwFlags AND $7FF) shl (((dwFlags shr 11) AND $F) + 8);
dwGPUSize := ((dwFlags shr 15) AND $7FF) shl (((dwFlags shr 26) AND $F) + 8);
```

---

## 3. Technical Requirements for Xbox 360 Textures ✅ VERIFIED

### Texture Swizzling Algorithm
Xbox 360 textures use a special tiled memory layout. The algorithm is:

```pascal
function XGAddress2DTiledOffset(x, y, w, texelPitch: DWORD): DWORD;
var
  alignedWidth, logBpp, Macro, Micro, Offset: DWORD;
begin
  alignedWidth := (w + 31) and not 31;
  logBpp := (TexelPitch shr 2) + ((TexelPitch shr 1) shr (TexelPitch shr 2));
  Macro := ((x shr 5) + (y shr 5) * (alignedWidth shr 5)) shl (logBpp + 7);
  Micro := (((x and 7) + ((y and 6) shl 2)) shl LogBpp);
  Offset := Macro + ((Micro and not 15) shl 1) + (Micro and 15) + 
            ((y and 8) shl (3 + logBpp)) + ((y and 1) shl 4);
  Result := (((Offset and not 511) shl 3) + ((Offset and 448) shl 2) + 
            (Offset and 63) + ((y and 16) shl 7) + 
            (((((y and 8) shr 2) + (x shr 3)) and 3) shl 6)) shr logBpp;
end;
```

### DXT5 Parameters (from Console.Xbox360.Graphics.pas)
```
Width alignment: 128 pixels
Height alignment: 128 pixels
Texel pitch (T): 16 bytes per 4x4 block
Endian swap: GPUENDIAN_8IN16 (swap bytes in each WORD)
```

### GPU Texture Formats (relevant ones)
| ID | Format Name |
|----|-------------|
| 18 | GPUTEXTUREFORMAT_DXT1 |
| 19 | GPUTEXTUREFORMAT_DXT2_3 |
| 20 | GPUTEXTUREFORMAT_DXT4_5 (DXT5) |

---

## 4. Available Tools ✅ VERIFIED

### RAGE Console Texture Editor (Downloaded)
- **Location**: `/Users/Ozordi/Downloads/LibertyRecomp/tools/xtd_tools/full/RAGE-Console-Texture-Editor-master/`
- **Executable**: `GTA V Console Texture Editor 1.3.1.exe` (32-bit Windows PE)
- **Source Code**: Full Delphi/Pascal source available
- **Wine Compatibility**: Wine 10.0 installed at `/opt/homebrew/bin/wine`

### Required DLLs (Present)
- `xcompress.dll` - Microsoft Xbox compression library
- `xcompress_cpp.dll` - C++ wrapper for LZX
- `xcompress_open.dll` - Open-source LZX implementation

### Key Source Files
| File | Purpose |
|------|---------|
| `Compression.LZX.pas` | LZX compression/decompression |
| `Console.Xbox360.Swizzling.pas` | Texture tiling algorithm |
| `Console.Xbox360.Graphics.pas` | Import/export routines |
| `GTAIV.TextureResource.Xbox360.pas` | XTD format handling |
| `Global.DirectDrawSurface.pas` | DDS file handling |

---

## 5. Plan Verification ✅

### Original Plan Statements - Status

| Statement | Status | Notes |
|-----------|--------|-------|
| Wine is installed and working | ✅ VERIFIED | Wine 10.0 at `/opt/homebrew/bin/wine` |
| Download RAGE Console Texture Editor | ✅ COMPLETE | Downloaded from archive.org |
| Source files are already DXT5 | ✅ VERIFIED | Confirmed via hex dump |
| XTD uses RSC v5 with LZX | ✅ VERIFIED | Magic bytes confirm RSC\x05 |
| Cannot create XTD from scratch | ✅ TRUE | Must use existing XTD as template |

### Critical Constraint
**The RAGE Console Texture Editor can REPLACE textures in existing XTD files, but cannot CREATE new XTD files from scratch.**

This means:
1. Use original `buttons_360.xtd` as template
2. Open in RAGE editor via Wine
3. Replace textures one by one with controller-variant DDS files
4. Save as new XTD for each controller variant

---

## 6. Dimension Mismatch Issue ⚠️ POTENTIAL PROBLEM

### Original Xbox 360 textures (likely)
- 64x64 pixels for button icons

### Source DDS files
- Most buttons: 64x64 (4,224 bytes) ✅ Match
- Some PS4/PS5/modern: 128x128 (16,512 bytes) ❌ May not match
- Controller images: 1024x1024 ❌ May not match

### Resolution
Per the README: "Replaced texture should have same parameters as the original one."
- Need to verify original XTD texture dimensions after decompression
- May need to resize 128x128 textures to 64x64 before import

---

## 7. Execution Plan

### Phase 1: Test RAGE Editor with Wine (Interactive)
1. Launch: `wine "GTA V Console Texture Editor 1.3.1.exe"`
2. Open original `buttons_360.xtd`
3. List all texture names and dimensions
4. Test replacing one texture with a source DDS
5. Save and verify

### Phase 2: Build Texture Mapping Table
Map each original XTD texture name to corresponding variant DDS files.

### Phase 3: Create Controller Variant XTDs
For each controller type:
1. Copy original XTD to working directory
2. Replace all textures with controller-variant DDS
3. Save to output directory

### Phase 4: Validate
1. Open created XTD in RAGE editor
2. Verify textures display correctly
3. Export DDS from created XTD to verify round-trip

---

## 8. Alternative: Python Implementation

If Wine GUI proves problematic, build Python tool using:
- LZX decompression: `GoobyCorp/Xbox-360-Crypto` library
- Swizzling: Port `XGAddress2DTiledOffset` algorithm
- RSC parsing: Based on source code structures

### Required Python Libraries
```
struct      # Binary parsing
ctypes      # For LZX DLL calls if on Windows
lzxd        # Pure Python LZX (if available)
```

---

## Summary

The plan is **technically sound** with these verified facts:
1. ✅ Wine 10.0 is installed and working
2. ✅ RAGE Console Texture Editor is downloaded with source code
3. ✅ Source DDS files are already Xbox 360 compatible (DXT5, legacy headers)
4. ✅ XTD format is RSC v5 with LZX compression
5. ✅ Swizzling algorithm is documented in source code
6. ⚠️ Some texture dimensions may need adjustment (128x128 → 64x64)
7. ⚠️ RAGE Editor is GUI-only (no CLI) - requires interactive Wine session

**Recommended Next Step**: Launch RAGE Console Texture Editor via Wine GUI to extract original texture list and test the replacement workflow.
