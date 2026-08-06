@echo off
setlocal
REM Launch the Project Hub (project picker; it starts the Editor for the chosen project).
REM
REM Usage: scripts\Windows\RunProjectHub.bat [Debug^|Release]

cd /d "%~dp0..\.."

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"

set "HUB=%CD%\build\Bin\%CONFIG%\ProjectHub.exe"

if not exist "%HUB%" (
    echo %HUB% not found — build first: scripts\Windows\CompileWindows.bat 1>&2
    exit /b 1
)

REM The hub launches the Editor itself — tell it where the repo lives and which configuration to start
REM (same contract as scripts/MacOS/RunProjectHub.sh).
set "DESERT_ROOT=%CD%"
set "DESERT_CONFIG=%CONFIG%"

"%HUB%"
exit /b %ERRORLEVEL%
