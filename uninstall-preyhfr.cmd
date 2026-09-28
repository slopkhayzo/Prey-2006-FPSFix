@echo off
setlocal
cd /d "%~dp0" || exit /b 1

echo Removing PreyHFR release files from:
echo   %CD%

for %%F in (
    "PreyHFRLauncher.exe"
    "PreyHFRHook.dll"
    "PreyHFR.ini"
    "PreyHFR-release.json"
    "PreyHFR.log"
    "PreyHFR-README.txt"
    "PreyHFR-LICENSE.txt"
) do if exist "%%~F" del /f /q "%%~F"

echo PreyHFR files removed. Retail game files and user data were not touched.
(goto) 2>nul & del /f /q "%~f0"
