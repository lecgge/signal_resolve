@echo off
setlocal enabledelayedexpansion
REM ================================================================
REM  USDE — Build Python bindings for all installed Python versions
REM
REM  Scans for Python 3.8 through 3.14, installs pybind11 in each,
REM  builds the native module, and copies .pyd to python/usde/.
REM
REM  Usage: build_all_python [--python-versions X.Y X.Y ...]
REM ================================================================

set SRC_DIR=%~dp0
set BUILD_BASE=%SRC_DIR%build_py

echo ================================================================
echo  USDE Python Build — scanning for Python installations...
echo ================================================================

REM ── Discover Python installations ────────────────────────────────

set PYLIST=
if "%~1"=="--python-versions" (
    shift
    :next_ver
    if not "%~1"=="" (
        set PYLIST=!PYLIST! %~1
        shift
        goto next_ver
    )
) else (
    REM Auto-scan: check common install locations
    for %%v in (8 9 10 11 12 13 14) do (
        for %%d in (
            "C:\Python3%%v\python.exe"
            "C:\Program Files\Python3%%v\python.exe"
            "%LOCALAPPDATA%\Programs\Python\Python3%%v\python.exe"
            "%APPDATA%\Python\Python3%%v\python.exe"
        ) do (
            if exist %%d (
                for /f "tokens=2 delims=. " %%a in ('%%d -c "import sys; print(sys.version)" 2^>nul') do (
                    set PYLIST=!PYLIST! %%a
                )
            )
        )
    )
)

if "%PYLIST%"=="" (
    echo ERROR: No Python installations found.
    echo Install Python 3.8-3.14 and try again, or specify versions:
    echo   build_all_python --python-versions 3.11 3.14
    exit /b 1
)

echo Found Python versions:%PYLIST%

REM ── Build for each version ───────────────────────────────────────

for %%v in (%PYLIST%) do (
    echo.
    echo --- Building for Python %%v ---

    REM Find python executable
    set PY=
    for %%d in (
        "C:\Python%%v\python.exe"
        "C:\Program Files\Python%%v\python.exe"
        "%LOCALAPPDATA%\Programs\Python\Python%%v\python.exe"
        "%APPDATA%\Python\Python%%v\python.exe"
    ) do (
        if exist %%d set PY=%%d
    )
    if "!PY!"=="" (
        echo   WARNING: Cannot find python.exe for %%v — skipping
        goto :skip
    )

    REM Install pybind11
    echo   Installing pybind11...
    "!PY!" -m pip install pybind11 -q 2>nul
    if errorlevel 1 (
        echo   WARNING: Failed to install pybind11 for %%v — skipping
        goto :skip
    )

    REM Get pybind11 cmake directory
    for /f "delims=" %%d in ('"!PY!" -m pybind11 --cmakedir 2^>nul') do set PB11_DIR=%%d
    if "!PB11_DIR!"=="" (
        echo   WARNING: pybind11 not found for %%v — skipping
        goto :skip
    )

    REM Configure and build
    set BUILD_DIR=%BUILD_BASE%_%%v
    echo   Configuring CMake...
    cmake -S "%SRC_DIR%" -B "!BUILD_DIR!" -G "Visual Studio 17 2022" -A x64 ^
        -DPYTHON_EXECUTABLE="!PY!" -Dpybind11_DIR="!PB11_DIR!" ^
        -DPYBIND11_FINDPYTHON=ON >nul 2>&1
    if errorlevel 1 (
        echo   ERROR: CMake configure failed for %%v
        goto :skip
    )

    echo   Building...
    cmake --build "!BUILD_DIR!" --config Release --target usde_python >nul 2>&1
    if errorlevel 1 (
        echo   ERROR: Build failed for %%v
        goto :skip
    )

    REM Copy .pyd to package
    for %%f in ("!BUILD_DIR!\Release\usde_python*.pyd") do (
        copy /Y "%%f" "%SRC_DIR%python\usde\" >nul
        echo   Done: %%f -^> python/usde/
    )

    :skip
)

echo.
echo ================================================================
echo  All builds complete.
echo  Package ready at: %SRC_DIR%python\usde\
echo.
echo  To use: copy the python/usde/ directory to your project.
echo ================================================================
