@echo off
setlocal EnableDelayedExpansion
REM One-time environment setup for building Desert Engine on Windows.
REM Fetches the third-party pieces that are NOT part of the repo, then generates the VS solution.
REM Safe to re-run: every step is idempotent.
REM
REM Counterpart of scripts/MacOS/Setup.sh. Two deliberate differences:
REM   - No package manager step: Windows builds against the LunarG Vulkan SDK (VULKAN_SDK) and the
REM     assimp binaries vendored in Editor/ThirdParty, so there is nothing to install here.
REM   - reflect-cpp is NOT fetched: Windows links the prebuilt ThirdParty/reflect-cpp/lib, whereas
REM     macOS has to compile the sources.

cd /d "%~dp0..\.."
set "ROOT=%CD%"

echo === Desert Engine Windows setup ===

where git >NUL 2>&1
if errorlevel 1 (
    echo [ERROR] git not found in PATH. Install Git for Windows and re-run.
    exit /b 1
)

REM ---------------------------------------------------------------------------
REM 1. Git submodules
REM ---------------------------------------------------------------------------
echo --- Initializing git submodules
git submodule update --init --recursive
if errorlevel 1 (
    echo [ERROR] submodule init failed.
    exit /b 1
)

REM ---------------------------------------------------------------------------
REM 2. Optick profiler sources (ThirdParty/optick is not a submodule)
REM ---------------------------------------------------------------------------
if not exist "%ROOT%\ThirdParty\optick\src\optick.h" (
    echo --- Cloning Optick into ThirdParty/optick
    if exist "%ROOT%\ThirdParty\optick" rmdir /S /Q "%ROOT%\ThirdParty\optick"
    git clone --depth 1 https://github.com/bombomby/optick.git "%ROOT%\ThirdParty\optick" || exit /b 1
) else (
    echo --- Optick sources present
)

REM ---------------------------------------------------------------------------
REM 3. meshoptimizer (mesh simplification / LOD generation) — not a submodule.
REM     Pinned to v0.20, the version BuildScripts/ThirdParty/MeshOptimizer.lua expects.
REM ---------------------------------------------------------------------------
if not exist "%ROOT%\ThirdParty\meshoptimizer\src\meshoptimizer.h" (
    echo --- Cloning meshoptimizer into ThirdParty/meshoptimizer
    if exist "%ROOT%\ThirdParty\meshoptimizer" rmdir /S /Q "%ROOT%\ThirdParty\meshoptimizer"
    git clone --branch v0.20 --depth 1 https://github.com/zeux/meshoptimizer.git "%ROOT%\ThirdParty\meshoptimizer" || exit /b 1
) else (
    echo --- meshoptimizer sources present
)

REM ---------------------------------------------------------------------------
REM 4. Vulkan SDK sanity check (headers, loader and glslc all come from it)
REM ---------------------------------------------------------------------------
if "%VULKAN_SDK%"=="" (
    echo [WARN] VULKAN_SDK is not set. Install the LunarG Vulkan SDK from
    echo        https://vulkan.lunarg.com/sdk/home#windows and re-open the shell.
) else (
    echo --- Vulkan SDK: %VULKAN_SDK%
)

REM ---------------------------------------------------------------------------
REM 5. Version header + project files
REM ---------------------------------------------------------------------------
call "%ROOT%\scripts\Windows\GenVersion.bat"
echo --- Generating project files
"%ROOT%\vendor\bin\premake5.exe" vs2022 || exit /b 1

echo.
echo === Setup complete ===
echo Build with:  scripts\Windows\BuildWindows.bat  ^(or open Desert.sln^)
endlocal
