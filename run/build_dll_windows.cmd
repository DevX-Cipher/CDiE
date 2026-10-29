@echo off
setlocal
rem Builds the shared library: die.dll
rem and puts ONLY that file in a folder under release\ (no zip, no headers,
rem no docs). The applications are packaged by build_portable_windows.cmd.
rem
rem   build_dll_windows.cmd <cmake-platform> <package-suffix>
rem   build_dll_windows.cmd x64 win64
rem
rem Optional environment variables:
rem   CMAKE_GENERATOR_NAME  default "Ninja" (see build_portable_windows.cmd)

if "%~2"=="" (
    echo Usage: %~nx0 ^<cmake-platform^> ^<package-suffix^>
    echo Example: %~nx0 x64 win64
    exit /b 1
)

rem Project root = the folder holding release_version.txt: one level up
rem when this script sits in CDiE\run, two levels up if it is nested deeper.
for %%I in ("%~dp0..") do set "PROJECT_ROOT=%%~fI"
if not exist "%PROJECT_ROOT%\release_version.txt" for %%I in ("%~dp0..\..") do set "PROJECT_ROOT=%%~fI"

set "CMAKE_PLATFORM=%~1"
set "PACKAGE_SUFFIX=%~2"
if "%CMAKE_GENERATOR_NAME%"=="" set "CMAKE_GENERATOR_NAME=Ninja"

set "GEN_IS_VS="
set "GEN_IS_MULTI="
if /i "%CMAKE_GENERATOR_NAME:~0,13%"=="Visual Studio" set "GEN_IS_VS=1"
if defined GEN_IS_VS set "GEN_IS_MULTI=1"
if /i "%CMAKE_GENERATOR_NAME%"=="Ninja Multi-Config" set "GEN_IS_MULTI=1"

if defined GEN_IS_VS (
    set "GEN_ARGS=-A %CMAKE_PLATFORM%"
) else if defined GEN_IS_MULTI (
    set "GEN_ARGS="
) else (
    set "GEN_ARGS=-DCMAKE_BUILD_TYPE=Release"
)

if not defined GEN_IS_VS (
    call :setup_msvc_env
    if errorlevel 1 exit /b 1
)

set "WORK_ROOT=%TEMP%\cdie_dll_%PACKAGE_SUFFIX%"
set "BUILD_DIR=%WORK_ROOT%\build"
set "STAGE_DIR=%WORK_ROOT%\stage"

if not exist "%PROJECT_ROOT%\release_version.txt" (
    echo release_version.txt not found:
    echo   %PROJECT_ROOT%\release_version.txt
    exit /b 1
)

set /p RELEASE_VERSION=<"%PROJECT_ROOT%\release_version.txt"
set "PACKAGE_NAME=cdie_%PACKAGE_SUFFIX%_dll_%RELEASE_VERSION%"
set "RELEASE_DIR=%PROJECT_ROOT%\release"
set "PACKAGE_DIR=%RELEASE_DIR%\%PACKAGE_NAME%"

if not exist "%RELEASE_DIR%" mkdir "%RELEASE_DIR%"
if errorlevel 1 exit /b 1

if exist "%WORK_ROOT%" rmdir /s /q "%WORK_ROOT%"
mkdir "%WORK_ROOT%"
if errorlevel 1 exit /b 1

echo Configuring %PACKAGE_SUFFIX% build...
cmake -S "%PROJECT_ROOT%" -B "%BUILD_DIR%" -G "%CMAKE_GENERATOR_NAME%" %GEN_ARGS% -DCDIE_BUILD_GUI=OFF -DCDIE_BUILD_CONSOLE=OFF -DCDIE_BUILD_TUI=OFF -DCDIE_BUILD_LIBRARY=ON -DDIE_BUILD_SHARED=ON -DDIE_BUILD_STATIC=OFF -DCDIE_DATABASE_DIR=NONE
if errorlevel 1 exit /b 1

echo Building %PACKAGE_SUFFIX% Release...
cmake --build "%BUILD_DIR%" --config Release --clean-first --parallel
if errorlevel 1 exit /b 1

rem Install into a scratch folder so the output path does not have to be
rem guessed per generator, then copy out just the one file.
cmake --install "%BUILD_DIR%" --config Release --prefix "%STAGE_DIR%" >nul
if errorlevel 1 exit /b 1

if not exist "%STAGE_DIR%\die.dll" (
    echo die.dll was not produced:
    echo   %STAGE_DIR%\die.dll
    exit /b 1
)

if exist "%PACKAGE_DIR%" rmdir /s /q "%PACKAGE_DIR%"
mkdir "%PACKAGE_DIR%"
if errorlevel 1 exit /b 1

copy /y "%STAGE_DIR%\die.dll" "%PACKAGE_DIR%\die.dll" >nul
if errorlevel 1 exit /b 1

if exist "%WORK_ROOT%" rmdir /s /q "%WORK_ROOT%"

echo.
echo Created:
echo   %PACKAGE_DIR%\die.dll

endlocal
exit /b 0

rem ---------------------------------------------------------------------------
rem :setup_msvc_env -- make cl.exe for %CMAKE_PLATFORM% available.
rem
rem Kept out of parenthesised blocks on purpose: the vswhere path contains
rem "(x86)", and a ")" inside a block ends the block early.
:setup_msvc_env
set "VCVARS_ARCH="
set "VC_TARGET_ARCH="
if /i "%CMAKE_PLATFORM%"=="x64"   (set "VCVARS_ARCH=x64"       & set "VC_TARGET_ARCH=x64")
if /i "%CMAKE_PLATFORM%"=="Win32" (set "VCVARS_ARCH=x64_x86"   & set "VC_TARGET_ARCH=x86")
if /i "%CMAKE_PLATFORM%"=="ARM64" (set "VCVARS_ARCH=x64_arm64" & set "VC_TARGET_ARCH=arm64")
if not defined VCVARS_ARCH goto msvc_bad_platform

where cl.exe >nul 2>&1
if errorlevel 1 goto msvc_find_vcvars

if not defined VSCMD_ARG_TGT_ARCH goto msvc_ready
if /i "%VSCMD_ARG_TGT_ARCH%"=="%VC_TARGET_ARCH%" goto msvc_ready
echo The current MSVC environment targets %VSCMD_ARG_TGT_ARCH%, but %CMAKE_PLATFORM% needs %VC_TARGET_ARCH%.
echo Run from a plain command prompt to let the script set it up, or open the
echo matching Developer Command Prompt.
exit /b 1

:msvc_find_vcvars
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto msvc_no_vs
set "VS_INSTALL="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALL=%%I"
if not defined VS_INSTALL goto msvc_no_vs
set "VCVARSALL=%VS_INSTALL%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARSALL%" goto msvc_no_vs

echo MSVC environment: %VCVARS_ARCH% from %VS_INSTALL%
call "%VCVARSALL%" %VCVARS_ARCH% >nul 2>&1
where cl.exe >nul 2>&1
if errorlevel 1 goto msvc_no_vs

:msvc_ready
if /i not "%CMAKE_GENERATOR_NAME:~0,5%"=="Ninja" exit /b 0
where ninja.exe >nul 2>&1
if not errorlevel 1 exit /b 0
echo ninja.exe not found on PATH. Install the "C++ CMake tools for Windows"
echo Visual Studio component, or put ninja on PATH.
exit /b 1

:msvc_bad_platform
echo Unknown platform "%CMAKE_PLATFORM%" for %CMAKE_GENERATOR_NAME%: use x64, Win32 or ARM64.
exit /b 1

:msvc_no_vs
echo No MSVC toolset found for %CMAKE_GENERATOR_NAME%.
echo Run from a Developer Command Prompt for %CMAKE_PLATFORM%, or install
echo Visual Studio with the "Desktop development with C++" workload.
exit /b 1