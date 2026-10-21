@echo off
setlocal
rem Build and test this fork in a new local directory; never retarget another cache.
rem Usage: build-tab5.cmd [x64^|x86] [existing-vcpkg-installed-root]
set "TAB5_ARCH=%~1"
if "%TAB5_ARCH%"=="" set "TAB5_ARCH=x64"
if not "%TAB5_ARCH%"=="x64" if not "%TAB5_ARCH%"=="x86" (
  echo Expected architecture x64 or x86.
  exit /b 2
)
set "TAB5_DEPS=%~2"
if not "%~2"=="" for %%i in ("%~2") do set "TAB5_DEPS=%%~fi"
set "TAB5_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%TAB5_VSWHERE%" (
  echo Visual Studio with MSVC v143 is required.
  exit /b 1
)
set "TAB5_VSDIR="
for /f "usebackq delims=" %%i in (`"%TAB5_VSWHERE%" -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TAB5_VSDIR=%%i"
if "%TAB5_VSDIR%"=="" exit /b 1
call "%TAB5_VSDIR%\Common7\Tools\VsDevCmd.bat" -arch=%TAB5_ARCH% -host_arch=x64 -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "TAB5_VCPKG=%VCPKG_ROOT%"
if "%TAB5_VCPKG%"=="" set "TAB5_VCPKG=%TAB5_VSDIR%\VC\vcpkg"
if not exist "%TAB5_VCPKG%\scripts\buildsystems\vcpkg.cmake" (
  echo Set VCPKG_ROOT to your vcpkg installation.
  exit /b 1
)
set "TAB5_TRIPLET=%TAB5_ARCH%-windows-static-v143"
set "TAB5_OUTPUT=%~dp0build-tab5-%TAB5_ARCH%"
if "%TAB5_DEPS%"=="" (
  cmake -S "%~dp0." -B "%TAB5_OUTPUT%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DKRENOMER_BUILD_TESTS=ON -DKRENOMER_WIN7=ON -DCMAKE_TOOLCHAIN_FILE="%TAB5_VCPKG%/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=%TAB5_TRIPLET% -DVCPKG_OVERLAY_TRIPLETS="%~dp0triplets"
) else (
  if not exist "%TAB5_DEPS%\%TAB5_TRIPLET%\share" (
    echo Existing installation does not contain %TAB5_TRIPLET%.
    exit /b 1
  )
  cmake -S "%~dp0." -B "%TAB5_OUTPUT%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DKRENOMER_BUILD_TESTS=ON -DKRENOMER_WIN7=ON -DCMAKE_TOOLCHAIN_FILE="%TAB5_VCPKG%/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=%TAB5_TRIPLET% -DVCPKG_MANIFEST_MODE=OFF -DVCPKG_INSTALLED_DIR="%TAB5_DEPS%"
)
if errorlevel 1 exit /b 1
cmake --build "%TAB5_OUTPUT%"
if errorlevel 1 exit /b 1
ctest --test-dir "%TAB5_OUTPUT%" --output-on-failure
if errorlevel 1 exit /b 1
echo Built and tested: %TAB5_OUTPUT%\krenomer-esp32.exe
