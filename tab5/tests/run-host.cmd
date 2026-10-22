@echo off
setlocal
cd /d "%~dp0.."
if not exist build-host mkdir build-host
where gcc >nul 2>nul
if errorlevel 1 (
  echo GCC is required on PATH for portable core tests.
  exit /b 1
)
gcc -std=c11 -Wall -Wextra -Werror -O2 main/tab5_measure.c tests/test_measure.c -lm -o build-host/test_measure.exe
if errorlevel 1 exit /b 1
build-host\test_measure.exe
if errorlevel 1 exit /b 1
gcc -std=c11 -Wall -Wextra -Werror -O2 main/tab5_time.c tests/test_time.c -o build-host/test_time.exe
if errorlevel 1 exit /b 1
build-host\test_time.exe
if errorlevel 1 exit /b 1

gcc -std=c11 -Wall -Wextra -Werror -O2 -Imain tests/test_resources.c -o build-host/test_resources.exe
if errorlevel 1 exit /b 1
build-host\test_resources.exe
if errorlevel 1 exit /b 1
python -m unittest discover -s tests -p test_package.py -v
exit /b %errorlevel%
