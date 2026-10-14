#!/usr/bin/env python3
"""
Batch XTD Texture Converter
Converts all controller variant XTD files using xtd_cli.exe via Wine
"""

import os
import sys
import subprocess
import shutil
from pathlib import Path

# Configuration
WINE = "/opt/homebrew/bin/wine"
XTD_CLI = "xtd_cli.exe"
ORIGINAL_XTD = "/Users/ozordi/Library/Application Support/LibertyRecomp/game/xbox360/textures/buttons_360.xtd"
OUTPUT_DIR = Path("/Users/Ozordi/Downloads/LibertyRecomp/tools/xtd_tools/output")
XTD_TOOLS_DIR = Path("/Users/Ozordi/Downloads/LibertyRecomp/tools/xtd_tools")
DDS_SOURCE = Path("/Users/Ozordi/Downloads/LibertyRecomp/tools/xtd_tools/processed_dds")

# Variant configurations: (folder_name, prefix, display_name)
VARIANTS = [
    ("ps3", "ps3_", "PlayStation 3"),
    ("ps4", "ps4_", "PlayStation 4"),
    ("ps5", "ps5_", "PlayStation 5"),
    ("switch", "switch_", "Nintendo Switch"),
    ("steam_controller", "sc_", "Steam Controller"),
    ("steam_deck", "sd_", "Steam Deck"),
    ("xbox_one", "xbone_", "Xbox One"),
    ("xbox_series_x", "xsx_", "Xbox Series X"),
    ("xbox360", "", "Xbox 360"),  # Base textures
]

# Textures in the original XTD
XTD_TEXTURES = [
    "a_butt", "b_butt", "x_butt", "y_butt",
    "lb_butt", "rb_butt", "lt_butt", "rt_butt",
    "back_butt", "start_butt",
    "dpad_all", "dpad_up", "dpad_down", "dpad_left", "dpad_right",
    "dpad_updown", "dpad_leftright", "dpad_none",
    "lstick_all", "lstick_up", "lstick_down", "lstick_left", "lstick_right",
    "lstick_updown", "lstick_leftright", "lstick_none",
    "rstick_all", "rstick_up", "rstick_down", "rstick_left", "rstick_right",
    "rstick_updown", "rstick_leftright", "rstick_none",
    "up_arrow", "down_arrow", "left_arrow", "right_arrow",
]

def wine_path(unix_path: str) -> str:
    """Convert Unix path to Wine-compatible Z: drive path"""
    converted = unix_path.replace('/', '\\\\')
    return f"Z:{converted}"

def run_xtd_cli(args: list) -> tuple:
    """Run xtd_cli.exe via Wine and return (success, output)"""
    cmd = [WINE, str(XTD_TOOLS_DIR / XTD_CLI)] + args
    try:
        result = subprocess.run(
            cmd,
            cwd=str(XTD_TOOLS_DIR),
            capture_output=True,
            text=True,
            timeout=60
        )
        # Filter out Wine noise
        output_lines = []
        for line in (result.stdout + result.stderr).split('\n'):
            if not any(x in line for x in ['err:hid', 'mvk-info', 'fixme:', 'err:environ', 'wow64']):
                output_lines.append(line)
        return result.returncode == 0, '\n'.join(output_lines)
    except subprocess.TimeoutExpired:
        return False, "Command timed out"
    except Exception as e:
        return False, str(e)

def convert_variant(folder_name: str, prefix: str, display_name: str) -> dict:
    """Convert a single variant"""
    variant_dir = OUTPUT_DIR / folder_name
    variant_dir.mkdir(parents=True, exist_ok=True)
    
    result = {
        "variant": display_name,
        "folder": folder_name,
        "status": "pending",
        "textures_replaced": 0,
        "errors": []
    }
    
    print(f"\n{'='*60}")
    print(f"Converting: {display_name}")
    print(f"{'='*60}")
    
    # Start with a copy of the original XTD
    working_xtd = variant_dir / "working.xtd"
    output_xtd = variant_dir / "buttons_360.xtd"
    shutil.copy(ORIGINAL_XTD, working_xtd)
    
    for tex_name in XTD_TEXTURES:
        # Find source DDS
        if prefix:
            dds_name = f"{prefix}{tex_name}.dds"
        else:
            dds_name = f"{tex_name}.dds"
        
        dds_path = DDS_SOURCE / dds_name
        
        if not dds_path.exists():
            # Fallback to base texture
            dds_path = DDS_SOURCE / f"{tex_name}.dds"
            if not dds_path.exists():
                print(f"  ⚠ No DDS for {tex_name}")
                continue
        
        temp_output = variant_dir / "temp_output.xtd"
        
        args = [
            "replace",
            wine_path(str(working_xtd)),
            wine_path(str(temp_output)),
            tex_name,
            wine_path(str(dds_path))
        ]
        
        success, output = run_xtd_cli(args)
        
        if success and temp_output.exists():
            shutil.move(temp_output, working_xtd)
            result["textures_replaced"] += 1
            print(f"  ✓ {tex_name}", flush=True)
        else:
            # Clean up temp if exists, but don't crash if it doesn't
            try:
                if temp_output.exists():
                    temp_output.unlink()
            except:
                pass
            result["errors"].append(f"{tex_name}: failed")
            print(f"  ✗ {tex_name}", flush=True)
    
    # Move final result
    if working_xtd.exists():
        shutil.move(working_xtd, output_xtd)
        result["status"] = "success" if result["textures_replaced"] > 0 else "partial"
    else:
        result["status"] = "failed"
    
    print(f"  Result: {result['textures_replaced']}/{len(XTD_TEXTURES)} textures")
    return result

def main():
    print("="*60)
    print("XTD Batch Texture Converter")
    print("="*60)
    
    # Verify prerequisites
    if not os.path.exists(ORIGINAL_XTD):
        print(f"ERROR: Original XTD not found: {ORIGINAL_XTD}")
        return 1
    
    if not (XTD_TOOLS_DIR / XTD_CLI).exists():
        print(f"ERROR: {XTD_CLI} not found")
        return 1
    
    results = []
    for folder_name, prefix, display_name in VARIANTS:
        result = convert_variant(folder_name, prefix, display_name)
        results.append(result)
    
    # Summary
    print("\n" + "="*60)
    print("CONVERSION SUMMARY")
    print("="*60)
    
    for result in results:
        icon = "✓" if result["status"] == "success" else ("⚠" if result["status"] == "partial" else "✗")
        print(f"  {icon} {result['variant']}: {result['textures_replaced']} textures")
    
    # Output location
    print(f"\nOUTPUT LOCATION: {OUTPUT_DIR}")
    print("="*60)
    
    for folder_name, _, display_name in VARIANTS:
        xtd_path = OUTPUT_DIR / folder_name / "buttons_360.xtd"
        if xtd_path.exists():
            size = xtd_path.stat().st_size
            print(f"  {folder_name}/buttons_360.xtd ({size:,} bytes)")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
