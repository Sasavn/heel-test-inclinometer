@echo off
rem 32-битный krenomer-esp32.exe (Windows 7 x86 и x64): build-win7-x86\krenomer-esp32.exe. Параметры — как у build-win7.cmd.
setlocal
set KRENOMER_ARCH=x86
call "%~dp0build-win7.cmd" %*
exit /b %ERRORLEVEL%
