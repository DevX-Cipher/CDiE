@echo off
rem ---------------------------------------------------------------------------
rem Build the cdie installer (release, x64, static CRT) with MSVC directly.
rem Lives in CDiE\src\installer\ next to its own src\, tools\, third_party\.
rem
rem Steps:
rem   1. build tools\mkpayload.c            -> build_msvc\installer\mkpayload.exe
rem   2. copy cdie_gui.exe into payload\ and pack payload\ -> payload.bin
rem   3. compile installer.rc (embeds payload.bin)
rem   4. compile + link the installer       -> build_msvc\cdie-setup.exe
rem
rem Usage:  build_installer.cmd [clean]
rem   Also called by run\build_msvc_gui.cmd when its "installer" flag is given.
rem
rem Overrides (optional):
rem   set "VCVARS=...\vcvars64.bat"
rem   set "APP_EXE=C:\full\path\cdie_gui.exe"   exe to put in the payload
rem   set "PAYLOAD_NAME=cdie_gui.exe"           file name inside payload\
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

rem ROOT = this folder (CDiE\src\installer), CDIE = the main repo root (CDiE)
for %%i in ("%~dp0.") do set "ROOT=%%~fi"
for %%i in ("%ROOT%\..\..") do set "CDIE=%%~fi"

set "OUT=%CDIE%\build_msvc"
set "IOUT=%OUT%\installer"
set "COMMON=%ROOT%\src\common"
set "INST=%ROOT%\src\installer"
set "TOOLS=%ROOT%\tools"
set "LZMA=%ROOT%\third_party\lzma"
set "PAYLOAD=%ROOT%\payload"
set "BLD=%IOUT%\build"
set "SETUP_EXE=%OUT%\cdie-setup.exe"
if not defined APP_EXE set "APP_EXE=%OUT%\cdie_gui.exe"
if not defined PAYLOAD_NAME set "PAYLOAD_NAME=cdie_gui.exe"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

rem --- Clean (no Visual Studio environment needed) ---------------------------
if /i "%~1"=="clean" (
    if exist "%IOUT%" rmdir /s /q "%IOUT%"
    if exist "%SETUP_EXE%" del /q "%SETUP_EXE%"
    echo Installer build output removed.
    exit /b 0
)

rem --- Visual Studio x64 environment -----------------------------------------
if not defined VSCMD_VER (
    if not defined VCVARS (
        if not exist "!VSWHERE!" (
            echo ERROR: vswhere.exe not found. Set VCVARS to your vcvars64.bat.
            exit /b 1
        )
        set "VSPATH="
        for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
        if not defined VSPATH (
            echo ERROR: Visual Studio with the C++ tools was not found.
            exit /b 1
        )
        set "VCVARS=!VSPATH!\VC\Auxiliary\Build\vcvars64.bat"
    )
    if not exist "!VCVARS!" (
        echo ERROR: missing !VCVARS!
        exit /b 1
    )
    call "!VCVARS!" >nul
)

rem --- Sanity checks ---------------------------------------------------------
if not exist "%APP_EXE%" (
    echo ERROR: %APP_EXE% not found. Build the app first: run\build_msvc_gui.cmd
    exit /b 1
)
if not exist "%TOOLS%\mkpayload.c" (
    echo ERROR: missing %TOOLS%\mkpayload.c
    exit /b 1
)
if not exist "%INST%\installer.c" (
    echo ERROR: missing %INST%\installer.c
    exit /b 1
)
if not exist "%LZMA%\lzma_decoder.h" (
    echo ERROR: missing %LZMA%\lzma_decoder.h
    exit /b 1
)

if not exist "%IOUT%\mkpayload" mkdir "%IOUT%\mkpayload"
if not exist "%IOUT%\obj"       mkdir "%IOUT%\obj"
if not exist "%BLD%"            mkdir "%BLD%"
if not exist "%PAYLOAD%"        mkdir "%PAYLOAD%"

rem Same flags as common.props, Release: /MT (no redistributable), /O2, UNICODE,
rem Win7 target, C11, conformance mode, no LTCG (WholeProgramOptimization=false).
set "CFLAGS=/nologo /O2 /Oi /Gy /W3 /MT /std:c11 /permissive- /DNDEBUG /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /D_CRT_SECURE_NO_WARNINGS"

rem --- 1. mkpayload.exe ------------------------------------------------------
echo [1/4] mkpayload.exe...
cl %CFLAGS% -I"%ROOT%\src" -I"%COMMON%" -I"%LZMA%" "%TOOLS%\mkpayload.c" ^
   /Fo:"%IOUT%\mkpayload\\" /Fe:"%IOUT%\mkpayload.exe" ^
   /link /SUBSYSTEM:CONSOLE /OPT:REF /OPT:ICF shell32.lib
if errorlevel 1 ( echo BUILD FAILED ^(mkpayload.exe^) & exit /b 1 )

rem --- 2. pack the payload ---------------------------------------------------
echo [2/4] payload.bin...
copy /y "%APP_EXE%" "%PAYLOAD%\%PAYLOAD_NAME%" >nul
if errorlevel 1 ( echo BUILD FAILED ^(copy to payload^) & exit /b 1 )
"%IOUT%\mkpayload.exe" "%BLD%\payload.bin" "%PAYLOAD%"
if errorlevel 1 ( echo BUILD FAILED ^(mkpayload^) & exit /b 1 )

rem --- 3. resources ----------------------------------------------------------
echo [3/4] installer.res...
rc /nologo /DUNICODE /D_UNICODE /i "%INST%" /i "%COMMON%" /i "%BLD%" /fo "%IOUT%\installer.res" "%INST%\installer.rc"
if errorlevel 1 ( echo BUILD FAILED ^(installer.res^) & exit /b 1 )

rem --- 4. cdie-setup.exe -----------------------------------------------------
echo [4/4] cdie-setup.exe...
cl %CFLAGS% -I"%ROOT%\src" -I"%COMMON%" -I"%INST%" -I"%LZMA%" ^
   "%INST%\installer.c" "%INST%\install.c" "%INST%\ui.c" "%INST%\uninstall_mode.c" ^
   "%COMMON%\payload.c" "%COMMON%\common.c" "%IOUT%\installer.res" ^
   /Fo:"%IOUT%\obj\\" /Fe:"%SETUP_EXE%" ^
   /link /SUBSYSTEM:WINDOWS /MANIFEST:NO /OPT:REF /OPT:ICF ^
   shell32.lib ole32.lib uuid.lib advapi32.lib user32.lib gdi32.lib
if errorlevel 1 ( echo BUILD FAILED ^(cdie-setup.exe^) & exit /b 1 )

echo.
echo Built in %OUT%\ :
echo   cdie-setup.exe
endlocal
exit /b 0