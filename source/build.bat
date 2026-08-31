@echo off
setlocal
rem INNO_PRIMARY_BUILD
set "ROOT=%~dp0.."
cmake -S "%ROOT%" -B "%ROOT%\build\inno-obj" -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1
cmake --build "%ROOT%\build\inno-obj" --config Release --target RizomUVChineseLauncher RizomUVChineseRuntime
if errorlevel 1 exit /b 1
if /I "%~1"=="--runtime-only" exit /b 0
call "%~dp0build_inno.bat" --package-only
exit /b %errorlevel%
