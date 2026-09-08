@echo off
setlocal
REM THE ENGINE DROP — the downloadable build of the TOOLS, not of a game.
REM Output: dist\DesertEngine-<config>\ — CI archives this directory as an artifact (ci.yml).
REM
REM   scripts\Windows\Package.bat [Release^|Debug]
REM
REM WHAT THIS IS AND WHAT IT IS NOT (П5). A GAME is packaged by the editor's own PackageGame() and by
REM nothing else: it needs an OPEN PROJECT, which this script does not have and CI does not have
REM either, and its product is the player binary plus one archive carrying the project's content and
REM its descriptor. This script's product is the EDITOR plus the tools plus the loose engine resources
REM the editor reads from disk. Two disjoint jobs, and only one of them ships a game.
REM
REM WHY Content.dpak AND Content.manifest ARE NO LONGER WRITTEN HERE. Measured on the macOS twin of
REM this script, 2026-09-08, and the finding is platform-independent because the reasoning is about
REM call sites rather than about the shell: `VFS::MountPak` has exactly ONE non-test call site in this
REM repository — Runtime/Source/PackagedContent.cpp — so the editor and the tools in this directory
REM never mount an archive at all, and the pak's only possible reader was the Runtime sitting beside
REM it. That reader mounted its 144 MB and then refused, because a pak of Editor\Resources carries no
REM project descriptor and this script has no project to describe. The drop was 528 MB of which 144 MB
REM was an archive nothing could use and another 133 MB was the SAME tree loose beside it — one tree
REM shipped twice, 52 % of the artifact — and that pair is what made this directory read as a second,
REM broken way to package a game.
REM
REM The header this replaced asserted the opposite in writing ("Content.dpak … the packaged-game
REM path"), which is this project's most frequent defect shape: a comment promising a guarantee the
REM tree does not honour. It was believed for as long as nobody ran the Runtime in this folder.
REM
REM The patch workflow those two files were written for is not lost — `PakTool manifest <pak> <out>`
REM records a manifest of any archive, and the archive a release actually patches is a GAME's,
REM produced by PackageGame. Recording one for the engine drop answered a question nobody asks.

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

for %%E in (Editor Runtime PakTool DShaderTool) do (
    if exist "%BIN%\%%E.exe" copy /Y "%BIN%\%%E.exe" "%OUT%\" >NUL
)

REM Assimp is the one dependency that ships as a DLL (everything else links statically).
for %%D in ("%BIN%\*.dll") do copy /Y "%%D" "%OUT%\" >NUL 2>&1

REM The editor's resources, loose — the only form anything in this directory can read. See the header
REM for why no pak and no manifest are written here.
robocopy "%ROOT%\Editor\Resources" "%OUT%\Resources" /E /NFL /NDL /NJH /NJS /NP >NUL
REM robocopy uses exit codes 0-7 for success; anything >= 8 is a real failure.
if %ERRORLEVEL% GEQ 8 (
    echo Package.bat: copying Resources failed 1>&2
    exit /b 1
)

echo Package.bat: packaged -^> %OUT%
exit /b 0
