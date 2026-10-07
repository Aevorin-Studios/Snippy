@echo off
setlocal
cd /d "%~dp0"

echo ========================================================
echo  Building Snippy Native C++ Installer...
echo ========================================================

set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"

if not exist "%ISCC%" (
    echo ========================================================
    echo  Error: Inno Setup 6 compiler ISCC.exe was not found.
    echo  Please install Inno Setup 6 from: https://jrsoftware.org/isdl.php
    echo ========================================================
    pause
    exit /b 1
)

echo.
echo ========================================================
echo  Compiling Snippy_Setup.exe Installer...
echo ========================================================
"%ISCC%" "%~dp0Snippy_Installer.iss"

if %ERRORLEVEL% EQU 0 (
    echo.
    echo =========================================
    echo  Installer Build Succeeded!
    echo  Output: InstallerOutput\Snippy_Setup.exe
    echo =========================================
) else (
    echo.
    echo  Installer Build Failed!
)
pause
