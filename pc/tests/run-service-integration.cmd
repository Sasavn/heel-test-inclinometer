@echo off
setlocal
cd /d "%~dp0.."
if not exist build-tab5-x64 mkdir build-tab5-x64
gcc -std=c11 -shared -static -O2 tests/service_bridge.c -lbcrypt -o build-tab5-x64/service_bridge.dll
if errorlevel 1 exit /b 1
cd build-tab5-x64
esp32_test.exe --service-dll "%cd%\service_bridge.dll"
exit /b %errorlevel%
