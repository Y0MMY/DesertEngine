@echo off
setlocal
REM Launch the Desert Editor built by BuildWindows.bat + CompileWindows.bat.
REM
REM Usage: scripts\Windows\RunEditor.bat [Debug^|Release] [editor args...]
REM        (everything after the config is forwarded to the Editor, e.g. --project <path>)

cd /d "%~dp0..\.."

set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Debug"
if not "%~1"=="" shift

set "EDITOR=%CD%\build\Bin\%CONFIG%\Editor.exe"

if not exist "%EDITOR%" (
    echo %EDITOR% not found — build first: scripts\Windows\CompileWindows.bat 1>&2
    exit /b 1
)

REM The engine resolves Resources/... relative to the working directory.
cd Editor

REM The editor REQUIRES a project (--project <.deproj>); picking projects is the Project Hub's job.
REM With no extra args, fall back to the built-in sandbox project.
if "%~1"=="" (
    "%EDITOR%" --project Desert.deproj
) else (
    "%EDITOR%" %*
)
exit /b %ERRORLEVEL%
