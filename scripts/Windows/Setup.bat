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
REM 2b. volk (Vulkan meta-loader) headers — not a submodule, and ThirdParty/volk/ is gitignored.
REM     scripts/MacOS/Setup.sh has always cloned this; the Windows script did not, on the belief —
REM     written into the comment there — that the LunarG SDK ships it on Windows. It does not matter
REM     whether the SDK has a copy: the include is <volk/volk.h> and the directory on the include path
REM     is ThirdParty (Desert/Dependencies.lua, `base = baseDir`), not the SDK. Windows therefore failed
REM     with "Cannot open include file: 'volk/volk.h'" the moment it got as far as compiling.
REM ---------------------------------------------------------------------------
if not exist "%ROOT%\ThirdParty\volk\volk.h" (
    echo --- Cloning volk into ThirdParty/volk
    if exist "%ROOT%\ThirdParty\volk" rmdir /S /Q "%ROOT%\ThirdParty\volk"
    git clone --depth 1 https://github.com/zeux/volk.git "%ROOT%\ThirdParty\volk" || exit /b 1
) else (
    echo --- volk headers present
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
REM 5. premake5 (project generator) — fetched, not vendored.
REM     This step used to run "%ROOT%\vendor\bin\premake5.exe" unconditionally. That file has never
REM     been in the repository and cannot be: .gitignore excludes *.exe, so committing it is not an
REM     option that was passed over, it is one that is refused. Every clean checkout therefore failed
REM     here with cmd's "The system cannot find the path specified" — including CI, on every commit.
REM     Fetched on demand like Optick and meshoptimizer above, and pinned to the version the macOS
REM     side installs from its package manager, so both platforms generate with the same generator.
REM ---------------------------------------------------------------------------
set "PREMAKE_VERSION=5.0.0-beta8"
set "PREMAKE=%ROOT%\vendor\bin\premake5.exe"

REM A premake5 already on PATH wins: a developer who installed one should not get a second copy.
where premake5 >NUL 2>&1
if not errorlevel 1 (
    set "PREMAKE=premake5"
    echo --- premake5 found on PATH
) else (
    if not exist "%PREMAKE%" (
        echo --- Downloading premake5 %PREMAKE_VERSION%
        if not exist "%ROOT%\vendor\bin" mkdir "%ROOT%\vendor\bin"
        powershell -NoProfile -ExecutionPolicy Bypass -Command ^
            "$ErrorActionPreference = 'Stop';" ^
            "$url = 'https://github.com/premake/premake-core/releases/download/v%PREMAKE_VERSION%/premake-%PREMAKE_VERSION%-windows.zip';" ^
            "$zip = Join-Path $env:TEMP 'premake5.zip';" ^
            "Invoke-WebRequest -Uri $url -OutFile $zip;" ^
            "Expand-Archive -Path $zip -DestinationPath '%ROOT%\vendor\bin' -Force;" ^
            "Remove-Item $zip -Force"
        if errorlevel 1 (
            echo [ERROR] could not download premake5. Install it manually and put premake5.exe on PATH.
            exit /b 1
        )
    ) else (
        echo --- premake5 present in vendor\bin
    )
)

REM ---------------------------------------------------------------------------
REM 6. Version header + project files
REM ---------------------------------------------------------------------------
call "%ROOT%\scripts\Windows\GenVersion.bat"
echo --- Generating project files
"%PREMAKE%" vs2022 || exit /b 1

echo.
echo === Setup complete ===
echo Build with:  scripts\Windows\BuildWindows.bat  ^(or open Desert.sln^)
endlocal
