@echo off
REM USDE Java Library Build Script
REM Prerequisites: JDK 11+, JNA jar at java/jna-5.14.0.jar
REM Usage: build_java [jar|clean]

setlocal
set J=%dp0java

if "%1"=="clean" (
    rmdir /s /q "%J%\build" 2>nul
    del "%J%\usde.jar" 2>nul
    echo Cleaned.
    goto :eof
)

if not exist "%J%\jna-5.14.0.jar" (
    echo ERROR: %J%\jna-5.14.0.jar not found
    echo Download from Maven Central: net.java.dev.jna:jna:5.14.0
    exit /b 1
)

echo Compiling...
if not exist "%J%\build" mkdir "%J%\build"
javac -cp "%J%\jna-5.14.0.jar" -d "%J%\build" "%J%\com\usde\*.java"
if %ERRORLEVEL% neq 0 exit /b %ERRORLEVEL%

if "%1"=="jar" (
    echo Creating usde.jar...
    cd "%J%\build"
    jar cf "%J%\usde.jar" com\usde\*.class
    cd %dp0
    echo Done: %J%\usde.jar
)

echo Build successful.
