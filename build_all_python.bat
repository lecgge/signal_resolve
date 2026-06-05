@echo off
setlocal enabledelayedexpansion
REM ================================================================
REM  USDE - Build .pyd for Python 3.8 to 3.14
REM
REM  Usage:
REM    build_all_python               auto-scan installed versions
REM    build_all_python 3.11 3.12     specific versions
REM    build_all_python --clean       remove build dirs
REM ================================================================

set SRC=%~dp0
set DST=%SRC%python\usde
set BDIR=%SRC%build_py

if "%~1"=="--clean" (
    for /d %%d in (%BDIR%_*) do rmdir /s /q "%%d" 2>nul
    del "%DST%\usde_python.*.pyd" 2>nul
    echo Cleaned.
    goto :EOF
)

echo === USDE Python Build: 3.8 - 3.14 ===
echo.

REM --- Collect versions ---
set VL=
if "%~1"=="" (
    for /L %%v in (8,1,14) do (
        for %%d in ("%LOCALAPPDATA%\Programs\Python\Python3%%v\python.exe") do (
            if exist %%d call :ADD %%v
        )
    )
) else (
    :LOOP
    if not "%~1"=="" (
        call :ADD %~1
        shift
        goto LOOP
    )
)

if "%VL%"=="" (
    echo No Python found. Install from python.org or:
    echo   winget install Python.Python.3.11
    exit /b 1
)
echo Targets: %VL%
echo.

set EC=0
for %%v in (%VL%) do call :BUILD %%v

echo === Done: %EC% errors ===
exit /b %EC%

REM ====== ADD ======================================================
:ADD
    set v=%1
    if %v% geq 8 if %v% leq 14 (
        for %%x in (%VL%) do if "%%x"=="%v%" exit /b 0
        set VL=!VL! %v%
    )
    exit /b 0

REM ====== BUILD ====================================================
:BUILD
    set PV=%1
    echo --- Python 3.%PV% ---

    set PY=
    for %%d in ("%LOCALAPPDATA%\Programs\Python\Python3%PV%\python.exe") do (
        if exist %%d set "PY=%%d"
    )
    if "!PY!"=="" (
        echo   SKIP: not found at %LOCALAPPDATA%\Programs\Python\Python3%PV%
        set /a EC+=1
        goto :EOF
    )

    "!PY!" -m pip install pybind11 -q 2>nul
    if errorlevel 1 (
        echo   SKIP: pip install pybind11 failed
        set /a EC+=1
        goto :EOF
    )

    for /f %%d in ('"!PY!" -m pybind11 --cmakedir 2^>nul') do set PB=%%d
    if "!PB!"=="" (
        echo   SKIP: pybind11 cmake dir not found
        set /a EC+=1
        goto :EOF
    )

    set BD=%BDIR%_%PV%
    if exist "!BD!\CMakeCache.txt" del "!BD!\CMakeCache.txt" 2>nul

    cmake -S "%SRC%" -B "!BD!" -G "Visual Studio 17 2022" -A x64 ^
        -DPYTHON_EXECUTABLE="!PY!" -Dpybind11_DIR="!PB!" >nul 2>&1
    if errorlevel 1 (
        echo   FAIL: cmake configure
        set /a EC+=1
        goto :EOF
    )

    cmake --build "!BD!" --config Release --target usde_python >nul 2>&1
    if errorlevel 1 (
        echo   FAIL: build
        set /a EC+=1
        goto :EOF
    )

    for %%f in ("!BD!\Release\usde_python*.pyd") do (
        copy /Y "%%f" "%DST%\" >nul 2>nul
        echo   OK: %%~nxf
    )
    goto :EOF
