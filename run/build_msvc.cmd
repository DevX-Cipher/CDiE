@echo off
rem ---------------------------------------------------------------------------
rem Build cdie with MSVC directly (no CMake). Produces, under build_msvc\:
rem
rem   cdie.exe         the console scanner - CRT-free, imports KERNEL32 only
rem   die.dll, die.lib the die_library-compatible shared library + import lib
rem   die_static.lib   the static library (link the whole engine in)
rem
rem Usage (from any prompt):  run\build_msvc.cmd
rem The script loads the Visual Studio x64 environment itself; override with
rem   set "VCVARS=...\vcvars64.bat"
rem ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set "ROOT=%~dp0.."
set "SRC=%ROOT%\src"
set "OUT=%ROOT%\build_msvc"

rem --- Visual Studio x64 environment -----------------------------------------
if not defined VSCMD_VER (
    if not defined VCVARS (
        for %%E in (Enterprise Professional Community BuildTools) do (
            if exist "%ProgramFiles%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat" (
                set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\%%E\VC\Auxiliary\Build\vcvars64.bat"
            )
        )
    )
    if not defined VCVARS (
        echo Could not find vcvars64.bat. Set VCVARS to its path and retry.
        exit /b 1
    )
    call "!VCVARS!" >nul
)

if not exist "%OUT%" mkdir "%OUT%"

rem --- Source lists ----------------------------------------------------------
rem Console: every .c under src (core, js, format, engine, console).
rem Library: the same, minus the two entry points, plus lib\die.c.
set "EXE_SOURCES="
set "LIB_SOURCES="
for /r "%SRC%" %%f in (*.c) do (
    set "EXE_SOURCES=!EXE_SOURCES! "%%f""
    set "keep=1"
    if /i "%%~nxf"=="utils_entry.c" set "keep="
    if /i "%%~nxf"=="main_console.c" set "keep="
    if defined keep set "LIB_SOURCES=!LIB_SOURCES! "%%f""
)
set "LIB_SOURCES=!LIB_SOURCES! "%ROOT%\lib\die.c""

set "CRTDEF=-D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE"

rem --- 1. cdie.exe (CRT-free, KERNEL32 only) ---------------------------------
echo [1/3] cdie.exe (CRT-free, KERNEL32 only)...
if not exist "%OUT%\obj_exe" mkdir "%OUT%\obj_exe"
cl /nologo /O2 /W3 /GS- /DNDEBUG -DCDIE_NO_CRT %CRTDEF% -I"%SRC%" ^
   !EXE_SOURCES! /Fe:"%OUT%\cdie.exe" /Fo:"%OUT%\obj_exe\\" ^
   /link /NODEFAULTLIB /ENTRY:x_entry_point /SUBSYSTEM:CONSOLE kernel32.lib
if errorlevel 1 ( echo BUILD FAILED ^(cdie.exe^) & exit /b 1 )

rem --- 2. die_static.lib (static, /MD, DIE_STATIC) ---------------------------
echo [2/3] die_static.lib (static library)...
if not exist "%OUT%\obj_static" mkdir "%OUT%\obj_static"
cl /nologo /O2 /W3 /MD /DNDEBUG -DDIE_STATIC %CRTDEF% -I"%SRC%" -I"%ROOT%\lib" ^
   /c !LIB_SOURCES! /Fo"%OUT%\obj_static\\"
if errorlevel 1 ( echo BUILD FAILED ^(die_static compile^) & exit /b 1 )
lib /nologo /OUT:"%OUT%\die_static.lib" "%OUT%\obj_static\*.obj"
if errorlevel 1 ( echo BUILD FAILED ^(die_static.lib^) & exit /b 1 )

rem --- 3. die.dll + die.lib (shared, /MD, DIE_BUILD_SHARED) ------------------
echo [3/3] die.dll + die.lib (shared library)...
if not exist "%OUT%\obj_shared" mkdir "%OUT%\obj_shared"
cl /nologo /O2 /W3 /MD /DNDEBUG -DDIE_BUILD_SHARED %CRTDEF% -I"%SRC%" -I"%ROOT%\lib" ^
   /c !LIB_SOURCES! /Fo"%OUT%\obj_shared\\"
if errorlevel 1 ( echo BUILD FAILED ^(die_shared compile^) & exit /b 1 )
link /nologo /DLL /OUT:"%OUT%\die.dll" /IMPLIB:"%OUT%\die.lib" ^
   "%OUT%\obj_shared\*.obj" kernel32.lib
if errorlevel 1 ( echo BUILD FAILED ^(die.dll^) & exit /b 1 )

echo.
echo Built in %OUT%\ :
echo   cdie.exe  die.dll  die.lib  die_static.lib
endlocal
