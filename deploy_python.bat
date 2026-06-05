@echo off
REM USDE Python Package — Copy native module into usde/ directory
REM Run this AFTER building: cmake --build build --config Release --target usde_python

setlocal enabledelayedexpansion

set SRC_DIR=build\Release
set DST_DIR=python\usde

echo Looking for usde_python*.pyd in %SRC_DIR%...
set FOUND=0
for %%f in ("%SRC_DIR%\usde_python*.pyd") do (
    echo Found: %%f
    copy /Y "%%f" "%DST_DIR%\" >nul
    set FOUND=1
)

if %FOUND%==0 (
    echo ERROR: No usde_python.pyd found. Build first:
    echo   cmake --build build --config Release --target usde_python
    exit /b 1
)

echo Done. Python package is ready in %DST_DIR%\
echo.
echo To use in another project:
echo   1. Copy the %DST_DIR% folder
echo   2. Put it in your project and: import usde
echo.
echo To install globally:
echo   pip install .
