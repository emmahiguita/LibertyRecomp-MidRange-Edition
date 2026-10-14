#!/usr/bin/env python3
import os

files = [
    ('/Users/ozordi/Library/Application Support/LibertyRecomp/game/xbox360/textures/buttons_360.xtd', 'Original XTD'),
    ('/Users/Ozordi/Downloads/LibertyRecomp/tools/xtd_tools/output/ps4/buttons_360.xtd', 'Generated PS4 XTD'),
    ('/Users/Ozordi/Downloads/LibertyRecomp/tools/GTAIV.EFLC.FusionFix-master/textures/pc/textures/buttons_360.wtd/ps4_rt_butt.dds', 'Source PS4 rt_butt.dds'),
    ('/Users/Ozordi/Downloads/LibertyRecomp/tools/xtd_tools/output/ps4_extracted/rt_butt.dds', 'Extracted rt_butt.dds'),
    ('/Users/Ozordi/Downloads/LibertyRecomp/tools/GTAIV.EFLC.FusionFix-master/textures/pc/textures/buttons_360.wtd/ps4_start_butt.dds', 'Source PS4 start_butt.dds'),
    ('/Users/Ozordi/Downloads/LibertyRecomp/tools/xtd_tools/output/ps4_extracted/start_butt.dds', 'Extracted start_butt.dds')
]

print('=== FILE SIZE COMPARISON ===')
print()
for i, (f, desc) in enumerate(files, 1):
    try:
        size = os.path.getsize(f)
        print(f'{i}. {desc}: {size} bytes')
    except FileNotFoundError:
        print(f'{i}. {desc}: NOT FOUND')

print()
print('=== HEADER COMPARISON ===')
print()

xtd_files = [
    ('/Users/ozordi/Library/Application Support/LibertyRecomp/game/xbox360/textures/buttons_360.xtd', 'Original XTD'),
    ('/Users/Ozordi/Downloads/LibertyRecomp/tools/xtd_tools/output/ps4/buttons_360.xtd', 'Generated PS4 XTD'),
]

for f, desc in xtd_files:
    print(f'{desc}:')
    try:
        with open(f, 'rb') as file:
            data = file.read(32)
            hex_str = ' '.join(f'{b:02x}' for b in data)
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data)
            print(f'  Hex: {hex_str}')
            print(f'  ASCII: {ascii_str}')
    except FileNotFoundError:
        print(f'  NOT FOUND')
    print()
