@echo off
REM Process ps5 variant textures
REM This script opens the texture editor for manual processing
echo Processing ps5 variant...
echo.
echo Instructions:
echo 1. File -^> Open -^> Select buttons_360.xtd from this folder
echo 2. For each texture, right-click -^> Import
echo 3. Select matching DDS from dds_textures folder
echo 4. File -^> Save
echo.
pause
start "" "/Users/Ozordi/Downloads/LibertyRecomp/tools/xtd_tools/full/RAGE-Console-Texture-Editor-master/GTA V Console Texture Editor 1.3.1.exe"
