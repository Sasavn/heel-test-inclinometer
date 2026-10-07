@echo off
rem Flash BWM427 firmware through the STM32 system DFU bootloader (USB DFU
rem 0483:DF11): write, read back and compare, then leave DFU and start the
rem application. The board enters the bootloader with BOOT0 = 1 + NRST, or by
rem the "boot" command over USB CDC (usb_flash.ps1 does that and then calls
rem this script).
rem
rem Usage: dfu_flash.cmd [image.bin]
rem   image     the argument, else %BWM427_IMAGE% (set by usb_flash.ps1),
rem             else bwm427_new.bin next to this script
rem   log       flash.log next to this script (also readback.bin, leave.bin)
rem   dfu-util  %DFU_UTIL% (full path, no quotes) if set, else dfu-util.exe on
rem             PATH, else the copy the Arduino IDE installs under
rem             AppData\Local\Arduino15 (current user first, then any user -
rem             for runs as SYSTEM)
setlocal
set "DIR=%~dp0"
set "DIR=%DIR:~0,-1%"
set "FW=%DIR%\bwm427_new.bin"
if defined BWM427_IMAGE set "FW=%BWM427_IMAGE%"
if not "%~1"=="" set "FW=%~f1"
set "LOG=%DIR%\flash.log"
call :find_dfu
del /q "%DIR%\readback.bin" "%DIR%\leave.bin" 2>nul
echo START %DATE% %TIME% > "%LOG%"
if not defined DFU goto no_dfu
echo DFU_UTIL %DFU% >> "%LOG%"
echo IMAGE %FW% >> "%LOG%"
"%DFU%" -d 0483:df11 -a 0 -s 0x08000000 -D "%FW%" >> "%LOG%" 2>&1
echo EXIT_DL %ERRORLEVEL% >> "%LOG%"
for %%A in ("%FW%") do set SIZE=%%~zA
"%DFU%" -d 0483:df11 -a 0 -s 0x08000000:%SIZE% -U "%DIR%\readback.bin" >> "%LOG%" 2>&1
echo EXIT_RB %ERRORLEVEL% >> "%LOG%"
powershell -NoProfile -Command "$a=(Get-FileHash -LiteralPath '%FW%').Hash; $b=(Get-FileHash -LiteralPath '%DIR%\readback.bin').Hash; 'NEW '+$a; 'RB  '+$b; if($a -and ($a -eq $b)){'VERIFY OK'}else{'VERIFY FAIL'}" >> "%LOG%" 2>&1
findstr /c:"VERIFY OK" "%LOG%" >nul && "%DFU%" -d 0483:df11 -a 0 -s 0x08000000:4:leave -U "%DIR%\leave.bin" >> "%LOG%" 2>&1
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
