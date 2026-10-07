@echo off
rem ===========================================================================
rem  AutoClick - build script (MSVC, static CRT, single exe output)
rem  Usage:  build.bat          -> Release
rem          build.bat debug    -> Debug
rem          build.bat w4       -> Release with /W4 (strict warning sweep)
rem ===========================================================================
setlocal enabledelayedexpansion
pushd "%~dp0"

set "CFG=release"
set "WARN=/W3"
if /i "%~1"=="debug" set "CFG=debug"
if /i "%~1"=="w4"    set "WARN=/W4"

rem ---------- locate MSVC build environment ----------
set "VCVARS="
if exist "D:\App\VS-BuildTools\VC\Auxiliary\Build\vcvars64.bat" (
    set "VCVARS=D:\App\VS-BuildTools\VC\Auxiliary\Build\vcvars64.bat"
)
if not defined VCVARS (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
)
if not defined VCVARS (
    echo [ERROR] vcvars64.bat not found. Install "Visual Studio Build Tools"
    echo         with the "Desktop development with C++" workload.
    popd & endlocal & exit /b 1
)

echo [1/3] Initializing MSVC environment ...
call "%VCVARS%" >nul
if errorlevel 1 (
    echo [ERROR] vcvars64.bat failed
    popd & endlocal & exit /b 1
)

if not exist "bin" mkdir "bin"
if not exist "obj" mkdir "obj"

echo [2/3] Compiling resources ...
rc /nologo /fo "obj\app.res" /I "res" "res\app.rc"
if errorlevel 1 (
    echo [ERROR] resource compilation failed
    popd & endlocal & exit /b 1
)

echo [3/3] Compiling and linking ...
set "OPT=/O2 /MT /DNDEBUG /GS /DUNICODE /D_UNICODE"
if /i "%CFG%"=="debug" set "OPT=/Od /MTd /D_DEBUG /Zi /GS /DUNICODE /D_UNICODE"

cl /nologo /std:c++17 /EHsc /utf-8 %WARN% %OPT% ^
   /Fo"obj\\" /Fe"bin\AutoClick.exe" ^
   "src\main.cpp" "src\ui_common.cpp" "src\imaging.cpp" ^
   "src\overlay.cpp" "src\listview.cpp" "src\engine.cpp" ^
   "obj\app.res" ^
   /link /SUBSYSTEM:WINDOWS /INCREMENTAL:NO ^
   /MANIFEST:EMBED /MANIFESTUAC:"level='asInvoker' uiAccess='false'"
if errorlevel 1 (
    echo [ERROR] build failed
    popd & endlocal & exit /b 1
)

echo.
echo BUILD OK -^> %~dp0bin\AutoClick.exe
popd
endlocal
exit /b 0
