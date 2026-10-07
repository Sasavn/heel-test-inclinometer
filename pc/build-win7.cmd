@echo off
chcp 65001 >nul
rem Сборка krenomer.exe (программа для ПК к регистратору крена) для Windows 7+:
rem тулсет v143 (MSVC 14.44), статический один .exe, проверка импортов после сборки.
rem Результат: build-win7\krenomer.exe (64 бита); build-win7-x86.cmd — 32-битный build-win7-x86\krenomer.exe.
rem Параметры передаются в configure, например:
rem   build-win7.cmd -DKRENOMER_BUILD_TESTS=ON      юнит-тесты и снимки экрана (tests\)
rem (Скрипт по образцу ShagomerPCModule\build-win7.cmd.)
setlocal
set VSLANG=1033
set ARCH=x64
set PRESET=default
set OUTDIR=build-win7
if /i "%KRENOMER_ARCH%"=="x86" (
    set ARCH=x86
    set PRESET=win7-x86
    set OUTDIR=build-win7-x86
)

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if not exist "%VSWHERE%" (
    echo Не найден vswhere.exe — установите Visual Studio 2022/2026 с компонентом MSVC v143 ^(14.44^).
    exit /b 1
)
set VSDIR=
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if "%VSDIR%"=="" (
    echo Не найдена Visual Studio с C++.
    exit /b 1
)

call "%VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=%ARCH% -host_arch=x64 -vcvars_ver=14.44 >nul
if errorlevel 1 (
    echo Нет тулсета MSVC 14.44 ^(v143^) — доустановите компонент "MSVC v143 - VS 2022 C++ x64/x86 build tools".
    exit /b 1
)
if "%VCPKG_ROOT%"=="" set VCPKG_ROOT=C:\vcpkg

cd /d "%~dp0"
cmake --preset %PRESET% %*
if errorlevel 1 exit /b 1
cmake --build --preset %PRESET%
if errorlevel 1 exit /b 1
echo.
echo Готово: %~dp0%OUTDIR%\krenomer.exe
endlocal
