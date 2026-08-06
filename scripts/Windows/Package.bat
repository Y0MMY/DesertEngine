@echo off
setlocal
REM Package a distributable build: binaries + content pak (+ loose Resources for the editor).
REM Output: dist\DesertEngine-<config>\ — CI archives this directory as a downloadable artifact.
REM
REM   scripts\Windows\Package.bat [Release^|Debug]
REM
REM Content ships BOTH ways on purpose (same rationale as scripts/MacOS/Package.sh):
REM   - Content.dpak (built with the same PakTool the Runtime mounts) — the packaged-game path;
REM   - loose Resources\ — the editor's dev path and the VFS's loose-file override for debugging.
REM Updates later: build a new pak and `PakTool diff old new Patch_001.dpak` — the Runtime mounts
REM Patch*.dpak on top of the base automatically.

cd /d "%~dp0..\.."
set "ROOT=%CD%"

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

set "BIN=%ROOT%\build\Bin\%CONFIG%"
set "OUT=%ROOT%\dist\DesertEngine-%CONFIG%"

if not exist "%BIN%\Runtime.exe" (
    echo Package.bat: no %CONFIG% binaries in %BIN% — build first 1>&2
    exit /b 1
)

if exist "%OUT%" rmdir /S /Q "%OUT%"
mkdir "%OUT%" || exit /b 1

for %%E in (Editor Runtime ProjectHub PakTool DShaderTool) do (
    if exist "%BIN%\%%E.exe" copy /Y "%BIN%\%%E.exe" "%OUT%\" >NUL
)

REM Assimp is the one dependency that ships as a DLL (everything else links statically).
for %%D in ("%BIN%\*.dll") do copy /Y "%%D" "%OUT%\" >NUL 2>&1

REM One content pak with everything the editor/runtime reads (keys keep the "Resources/" prefix so
REM reads relative to the package root resolve through the VFS unchanged).
"%BIN%\PakTool.exe" create "%OUT%\Content.dpak" "%ROOT%\Editor\Resources" --prefix Resources || exit /b 1

REM Loose copy for the editor + debugging override.
robocopy "%ROOT%\Editor\Resources" "%OUT%\Resources" /E /NFL /NDL /NJH /NJS /NP >NUL
REM robocopy uses exit codes 0-7 for success; anything >= 8 is a real failure.
if %ERRORLEVEL% GEQ 8 (
    echo Package.bat: copying Resources failed 1>&2
    exit /b 1
)

echo Package.bat: packaged -^> %OUT%
exit /b 0
