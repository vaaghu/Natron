@echo off
rem Runs scripts/natron-build.sh from cmd, in an MSYS2 MINGW64 shell:
rem   scripts\natron-build setup -p windows   set up this machine (installs MSYS2 if needed)
rem   scripts\natron-build [MODE] [OPTIONS]   see scripts\natron-build --help
rem
rem MSYS2 is looked for in %MSYS2_ROOT% (default: C:\msys64).

setlocal
if not defined MSYS2_ROOT set "MSYS2_ROOT=C:\msys64"
set "BASH=%MSYS2_ROOT%\usr\bin\bash.exe"

if exist "%BASH%" goto run
if /i not "%~1"=="setup" (
    echo MSYS2 was not found in %MSYS2_ROOT%: run "%~n0 setup -p windows" first.
    exit /b 1
)
if /i not "%MSYS2_ROOT%"=="C:\msys64" (
    echo MSYS2 was not found in %MSYS2_ROOT%: install it there, or unset MSYS2_ROOT to install it in C:\msys64.
    exit /b 1
)
echo Installing MSYS2 with winget...
winget install --id MSYS2.MSYS2 --exact --source winget --accept-package-agreements --accept-source-agreements
if not exist "%BASH%" (
    echo MSYS2 installation failed.
    exit /b 1
)
rem First start and update of a new installation: the first update may only
rem update the core packages and stop, the second one updates the rest.
"%BASH%" -lc "pacman -Syu --noconfirm"
"%BASH%" -lc "pacman -Syu --noconfirm"

:run
set MSYSTEM=MINGW64
set CHERE_INVOKING=1
"%BASH%" -lc "exec \"$(cygpath -u \"$0\")\" \"$@\"" "%~dp0natron-build.sh" %*
exit /b %ERRORLEVEL%
