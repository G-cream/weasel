@echo off
setlocal
where cl >nul 2>&1
if errorlevel 1 (
  echo Run this script from an x64 Visual Studio Developer Command Prompt.
  exit /b 1
)
set "TEST_OUTPUT=%~dp0..\..\msbuild\game-compat-tests"
if not exist "%TEST_OUTPUT%" mkdir "%TEST_OUTPUT%"
cl /nologo /W4 /EHsc /MT /std:c++17 "%~dp0config-tests.cpp" /Fe:"%TEST_OUTPUT%\config-tests.exe" /Fo:"%TEST_OUTPUT%\config-tests.obj" user32.lib advapi32.lib
if errorlevel 1 exit /b 1
"%TEST_OUTPUT%\config-tests.exe"
if errorlevel 1 exit /b 1
rem The guard is deliberately restricted to dota2.exe; use that name for the fixture.
cl /nologo /W4 /EHsc /MT /std:c++17 "%~dp0system-ime-tests.cpp" /Fe:"%TEST_OUTPUT%\dota2.exe" /Fo:"%TEST_OUTPUT%\system-ime-tests.obj" user32.lib advapi32.lib
if errorlevel 1 exit /b 1
"%TEST_OUTPUT%\dota2.exe"
exit /b %errorlevel%
