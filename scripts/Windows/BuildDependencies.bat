@echo off
setlocal EnableDelayedExpansion
REM Rebuilds the third-party pieces that ship as PREBUILT BINARIES on Windows, from source.
REM
REM   scripts\Windows\BuildDependencies.bat [assimp^|reflectcpp^|all] [Debug^|Release^|both]
REM
REM Everything else (ImGui, Jolt, Lua, Optick, MeshOptimizer, GLFW, yaml-cpp) is a premake project and
REM is built by the normal solution — nothing to do here. Only two dependencies are committed to the
REM repo as opaque .lib/.dll with no way to reproduce them:
REM
REM   assimp      Editor/ThirdParty/assimp/bin/{Debug,Release}  — no source in the tree at all
REM   reflect-cpp ThirdParty/reflect-cpp/bin/{Debug,Release}    — sources ARE in the tree (src/)
REM
REM This script is what makes those reproducible. It is NOT part of a normal build: Setup.bat does not
REM call it, because the committed binaries work. Run it to upgrade a dependency, to audit what the
REM binaries actually contain, or on a toolchain where the prebuilt ones are incompatible.
REM
REM CRT: everything is built /MD (dynamic) to match BuildScripts/PlatformWindows.lua. Mixing a static
REM CRT into the link is exactly the LNK2038 wall the node editor hit.

cd /d "%~dp0..\.."
set "ROOT=%CD%"

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=all"
set "CONFIGS=%~2"
if "%CONFIGS%"=="" set "CONFIGS=both"
if /I "%CONFIGS%"=="both" set "CONFIGS=Debug Release"

REM Pinned: an unpinned dependency turns "rebuild the deps" into an unplanned upgrade.
set "ASSIMP_TAG=v5.4.3"

where cmake >NUL 2>&1
if errorlevel 1 (
    echo [ERROR] cmake not found in PATH. Install it or open a Developer Command Prompt.
    exit /b 1
)

if /I "%TARGET%"=="all"        goto :do_all
if /I "%TARGET%"=="assimp"     goto :do_assimp
if /I "%TARGET%"=="reflectcpp" goto :do_reflectcpp
echo [ERROR] Unknown target "%TARGET%". Use: assimp ^| reflectcpp ^| all
exit /b 1

:do_all
call :build_reflectcpp || exit /b 1
call :build_assimp     || exit /b 1
goto :done

:do_assimp
call :build_assimp || exit /b 1
goto :done

:do_reflectcpp
call :build_reflectcpp || exit /b 1
goto :done

REM ---------------------------------------------------------------------------
REM reflect-cpp — sources already vendored in ThirdParty/reflect-cpp/src.
REM Produces bin/<config>/reflectcpp.lib, the exact path Desert/Dependencies.lua links.
REM ---------------------------------------------------------------------------
:build_reflectcpp
set "RCPP=%ROOT%\ThirdParty\reflect-cpp"
if not exist "%RCPP%\src\reflectcpp.cpp" (
    echo [ERROR] reflect-cpp sources missing at %RCPP%\src
    echo         They are fetched by scripts/MacOS/Setup.sh; copy src/ from reflect-cpp v0.19.0.
    exit /b 1
)

echo === Building reflect-cpp ===
set "GEN=%RCPP%\.cmake-build"
if not exist "%GEN%" mkdir "%GEN%"

> "%GEN%\CMakeLists.txt" (
    echo cmake_minimum_required^(VERSION 3.20^)
    echo project^(reflectcpp CXX C^)
    echo set^(CMAKE_CXX_STANDARD 20^)
    echo set^(CMAKE_CXX_STANDARD_REQUIRED ON^)
    echo # /MD and /MDd — must match the workspace CRT ^(PlatformWindows.lua^).
    echo set^(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$^<$^<CONFIG:Debug^>:Debug^>DLL"^)
    echo add_library^(reflectcpp STATIC
    echo     "%RCPP:\=/%/src/reflectcpp.cpp"
    echo     "%RCPP:\=/%/src/reflectcpp_json.cpp"
    echo     "%RCPP:\=/%/src/yyjson.c"^)
    echo target_include_directories^(reflectcpp PRIVATE "%RCPP:\=/%/include"^)
)

for %%C in (%CONFIGS%) do (
    echo --- reflect-cpp %%C
    cmake -S "%GEN%" -B "%GEN%\%%C" -A x64 -DCMAKE_POLICY_DEFAULT_CMP0091=NEW >NUL || exit /b 1
    cmake --build "%GEN%\%%C" --config %%C || exit /b 1
    if not exist "%RCPP%\bin\%%C" mkdir "%RCPP%\bin\%%C"
    copy /Y "%GEN%\%%C\%%C\reflectcpp.lib" "%RCPP%\bin\%%C\reflectcpp.lib" >NUL || exit /b 1
    if exist "%GEN%\%%C\%%C\reflectcpp.pdb" copy /Y "%GEN%\%%C\%%C\reflectcpp.pdb" "%RCPP%\bin\%%C\" >NUL
    echo     -^> ThirdParty\reflect-cpp\bin\%%C\reflectcpp.lib
)
exit /b 0

REM ---------------------------------------------------------------------------
REM assimp — no source in the tree, so fetch the pinned tag next to the build.
REM Produces Editor/ThirdParty/assimp/bin/<config>/assimp-vc142-mt[d].{lib,dll} + include/.
REM assimp is the ONE dependency that ships as a DLL; everything else links statically.
REM ---------------------------------------------------------------------------
:build_assimp
set "ASSIMP_DST=%ROOT%\Editor\ThirdParty\assimp"
set "ASSIMP_SRC=%ROOT%\ThirdParty\.assimp-src"

echo === Building assimp %ASSIMP_TAG% ===
if not exist "%ASSIMP_SRC%\CMakeLists.txt" (
    if exist "%ASSIMP_SRC%" rmdir /S /Q "%ASSIMP_SRC%"
    git clone --branch %ASSIMP_TAG% --depth 1 https://github.com/assimp/assimp.git "%ASSIMP_SRC%" || exit /b 1
) else (
    echo --- source present ^(delete ThirdParty\.assimp-src to re-fetch^)
)

for %%C in (%CONFIGS%) do (
    echo --- assimp %%C
    REM Importers only: the engine reads models and never writes them, and the exporters roughly
    REM double the build. Tests/tools/samples off for the same reason.
    cmake -S "%ASSIMP_SRC%" -B "%ASSIMP_SRC%\build-%%C" -A x64 ^
        -DCMAKE_POLICY_DEFAULT_CMP0091=NEW ^
        -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" ^
        -DBUILD_SHARED_LIBS=ON ^
        -DASSIMP_BUILD_ASSIMP_TOOLS=OFF ^
        -DASSIMP_BUILD_TESTS=OFF ^
        -DASSIMP_BUILD_SAMPLES=OFF ^
        -DASSIMP_INSTALL=OFF ^
        -DASSIMP_NO_EXPORT=ON ^
        -DASSIMP_WARNINGS_AS_ERRORS=OFF >NUL || exit /b 1
    cmake --build "%ASSIMP_SRC%\build-%%C" --config %%C || exit /b 1

    if not exist "%ASSIMP_DST%\bin\%%C" mkdir "%ASSIMP_DST%\bin\%%C"
    for %%F in ("%ASSIMP_SRC%\build-%%C\bin\%%C\assimp-*.dll") do copy /Y "%%F" "%ASSIMP_DST%\bin\%%C\" >NUL
    for %%F in ("%ASSIMP_SRC%\build-%%C\lib\%%C\assimp-*.lib") do copy /Y "%%F" "%ASSIMP_DST%\bin\%%C\" >NUL
    echo     -^> Editor\ThirdParty\assimp\bin\%%C\
)

REM Headers: the public include tree plus the generated config.h, which only exists after configure.
echo --- assimp headers
xcopy /E /I /Y /Q "%ASSIMP_SRC%\include\assimp" "%ASSIMP_DST%\include\assimp" >NUL || exit /b 1
for %%C in (%CONFIGS%) do (
    if exist "%ASSIMP_SRC%\build-%%C\include\assimp\config.h" (
        copy /Y "%ASSIMP_SRC%\build-%%C\include\assimp\config.h" "%ASSIMP_DST%\include\assimp\config.h" >NUL
    )
)
echo     -^> Editor\ThirdParty\assimp\include\assimp
exit /b 0

:done
echo.
echo === Dependencies built ===
echo NOTE: the resulting binaries are COMMITTED to the repo. Review the diff before committing —
echo       a dependency upgrade should be a deliberate, separate change.
endlocal
