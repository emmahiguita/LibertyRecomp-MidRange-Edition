# XTD Texture Conversion - Usage Instructions

## Option 1: RAGE Console Texture Editor via Wine (Interactive)

### Prerequisites
- Wine 10.0 installed at `/opt/homebrew/bin/wine`
- RAGE Console Texture Editor at `full/GTA V Console Texture Editor 1.3.1.exe`

### Steps to Run

1. **Open Terminal** and navigate to the tools directory:
   ```bash
   cd /Users/Ozordi/Downloads/LibertyRecomp/tools/xtd_tools/full
   ```

2. **Launch the editor**:
   ```bash
   wine "GTA V Console Texture Editor 1.3.1.exe"
   ```

3. **Open the original XTD**:
   - File → Open
   - Navigate to: `/Users/ozordi/Library/Application Support/LibertyRecomp/game/xbox360/textures/buttons_360.xtd`
   - This will show all texture names in the XTD

4. **Export texture list** (for reference):
   - Note down all texture names visible in the editor

5. **Replace a texture**:
   - Right-click a texture → Import (Replace)
   - Select matching DDS from `textures/pc/textures/buttons_360.wtd/`
   - Note: Dimensions must match exactly

6. **Save the modified XTD**:
   - File → Save As
   - Save to a new filename for each controller variant

### Workflow for Each Controller Variant

For PS3 textures:
1. Open original `buttons_360.xtd`
2. Replace each texture with `ps3_` prefixed version
3. Save as `buttons_360_ps3.xtd`

Repeat for: ps4_, ps5_, sc_, sd_, switch_, xbone_, xsx_

---

## Option 2: Python Tool (Automated - Requires LZX Library)

### Current Status
The Python tool at `xtd_tool.py` can:
- ✅ Parse RSC v5 headers
- ✅ Calculate segment sizes
- ✅ Analyze XTD structure
- ❌ Decompress LZX (needs library)
- ❌ Extract/replace textures (pending)

### Usage
```bash
python3 xtd_tool.py analyze "/path/to/buttons_360.xtd"
```

### To Enable Full Functionality
Need to install Python LZX decompression or use Wine to call xcompress.dll

---

## File Locations

| Item | Path |
|------|------|
| Original XTD | `/Users/ozordi/Library/Application Support/LibertyRecomp/game/xbox360/textures/buttons_360.xtd` |
| Source DDS files | `/Users/Ozordi/Downloads/LibertyRecomp/tools/GTAIV.EFLC.FusionFix-master/textures/pc/textures/buttons_360.wtd/` |
| RAGE Editor | `/Users/Ozordi/Downloads/LibertyRecomp/tools/xtd_tools/full/GTA V Console Texture Editor 1.3.1.exe` |
| Texture mapping | `/Users/Ozordi/Downloads/LibertyRecomp/tools/xtd_tools/texture_mapping.json` |
| Research summary | `/Users/Ozordi/Downloads/LibertyRecomp/tools/xtd_tools/XTD_CONVERSION_RESEARCH.md` |

---

## Troubleshooting

### Wine GUI doesn't open
- Try: `export DISPLAY=:0` before running wine
- Or run in XQuartz if on macOS

### Texture import fails
- Check texture dimensions match original
- Ensure DDS uses DXT5 compression
- Verify legacy DX9 header (no DX10 extension)

### File not found
- XTD path may have spaces - use quotes
- Check file permissions
