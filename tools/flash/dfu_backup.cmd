@echo off
rem Read the whole 512 KB flash of the board (firmware + settings sector)
rem through the STM32 system DFU bootloader (USB DFU 0483:DF11) into
rem bwm427_orig.bin; log in backup.log. The board must already be in DFU mode
rem (BOOT0 = 1 + NRST, or the "boot" command over USB CDC).
rem
rem Usage: dfu_backup.cmd [out_dir]
rem   out_dir   default: the folder of this script
rem   dfu-util  %DFU_UTIL% (full path, no quotes) if set, else dfu-util.exe on
rem             PATH, else the copy the Arduino IDE installs under
rem             AppData\Local\Arduino15 (current user first, then any user)
setlocal
set "OUT=%~dp0"
set "OUT=%OUT:~0,-1%"
if not "%~1"=="" set "OUT=%~f1"
set "LOG=%OUT%\backup.log"
call :find_dfu
del /q "%OUT%\bwm427_orig.bin" "%LOG%" 2>nul
echo START %DATE% %TIME% > "%LOG%"
if not defined DFU goto no_dfu
echo DFU_UTIL %DFU% >> "%LOG%"
"%DFU%" -d 0483:df11 -a 0 -s 0x08000000:524288 -U "%OUT%\bwm427_orig.bin" >> "%LOG%" 2>&1
echo EXIT %ERRORLEVEL% >> "%LOG%"
powershell -NoProfile -Command "(Get-FileHash -LiteralPath '%OUT%\bwm427_orig.bin').Hash" >> "%LOG%" 2>&1
echo DONE %DATE% %TIME% >> "%LOG%"
exit /b 0

:no_dfu
echo ERROR: dfu-util.exe not found - set DFU_UTIL to its full path or add it to PATH >> "%LOG%"
echo DONE %DATE% %TIME% >> "%LOG%"
exit /b 1

rem Sets DFU to the full path of dfu-util.exe (empty if not found)
:find_dfu
set "DFU="
if defined DFU_UTIL if exist "%DFU_UTIL%" set "DFU=%DFU_UTIL%"
if defined DFU goto :eof
for /f "delims=" %%P in ('where dfu-util.exe 2^>nul') do if not defined DFU set "DFU=%%P"
if defined DFU goto :eof
for /d %%V in ("%LOCALAPPDATA%\Arduino15\packages\arduino\tools\dfu-util\*") do if exist "%%V\dfu-util.exe" set "DFU=%%V\dfu-util.exe"
if defined DFU goto :eof
for /d %%U in ("%SystemDrive%\Users\*") do for /d %%V in ("%%U\AppData\Local\Arduino15\packages\arduino\tools\dfu-util\*") do if exist "%%V\dfu-util.exe" set "DFU=%%V\dfu-util.exe"
goto :eof
