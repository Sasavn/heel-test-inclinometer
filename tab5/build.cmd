@echo off
setlocal
rem Build only. This script never enumerates ports and never flashes a device.
for %%I in ("%~dp0..\.tools\idf-tools") do set "IDF_TOOLS_PATH=%%~fI"
for %%I in ("%~dp0..\.tools\esp-idf") do set "IDF_PATH=%%~fI"
if not exist "%IDF_PATH%\export.bat" (
  echo ESP-IDF 5.5.2 is missing. See tab5\README.md.
  exit /b 1
)
call "%IDF_PATH%\export.bat"
if errorlevel 1 exit /b 1
cd /d "%~dp0"
if /i "%~1"=="rev3" goto rev3
if /i "%~1"=="controls" goto controls
if /i "%~1"=="controls-rev3" goto controlsrev3
if not "%~1"=="" (
  echo Usage: build.cmd [rev3 / controls / controls-rev3]
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
:controls
python "%IDF_PATH%\tools\idf.py" -B build-controls "-DSDKCONFIG=%CD%\build-controls\sdkconfig" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.controls" build
if errorlevel 1 exit /b 1
python tools\package.py build-controls
exit /b %errorlevel%
:controlsrev3
python "%IDF_PATH%\tools\idf.py" -B build-controls-rev3 "-DSDKCONFIG=%CD%\build-controls-rev3\sdkconfig" "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.rev3;sdkconfig.controls" build
if errorlevel 1 exit /b 1
python tools\package.py build-controls-rev3
exit /b %errorlevel%
