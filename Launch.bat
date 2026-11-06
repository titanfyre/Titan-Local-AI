@echo off
setlocal
cd /d "%~dp0"
if exist "%~dp0TitanLocalAI.exe" (
    start "" "%~dp0TitanLocalAI.exe" %*
    exit /b 0
)
if exist "%~dp0out\build\Release\TitanLocalAI.exe" (
    set "PATH=%~dp0.deps\Qt\6.8.3\msvc2022_64\bin;%PATH%"
    start "" "%~dp0out\build\Release\TitanLocalAI.exe" %*
    exit /b 0
)
echo Titan Local AI n'est pas encore compile.
echo Lancez PowerShell puis : .\Build.ps1 -Configuration Release -Test -Package
pause
exit /b 1
