@echo off
setlocal
rem Build only. This script never enumerates ports and never flashes a device.
set "IDF_TOOLS_PATH=%~dp0..\.tools\idf-tools"
set "IDF_PATH=%~dp0..\.tools\esp-idf"
if not exist "%IDF_PATH%\export.bat" (
  echo ESP-IDF 5.5.2 is missing. See tab5\README.md.
  exit /b 1
)
call "%IDF_PATH%\export.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if /i "%~1"=="rev3" goto rev3
if not "%~1"=="" (
  echo Usage: build.cmd [rev3]
  exit /b 2
)
python "%IDF_PATH%\tools\idf.py" build
if errorlevel 1 exit /b 1
python tools\package.py build
exit /b %errorlevel%
:rev3
python "%IDF_PATH%\tools\idf.py" -B build-rev3 "-DSDKCONFIG=%CD%\build-rev3\sdkconfig" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.rev3" build
if errorlevel 1 exit /b 1
python tools\package.py build-rev3
exit /b %errorlevel%
