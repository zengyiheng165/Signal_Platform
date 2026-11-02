@echo off
setlocal EnableExtensions
rem ============================================================
rem  Create / open the Vivado project of this project.
rem  Usage:
rem      create_vivado_project.bat                 (create or open, GUI)
rem      create_vivado_project.bat -recreate       (rebuild from src\pl)
rem      create_vivado_project.bat --batch         (no GUI, headless)
rem
rem  Everything Vivado generates stays under board\prj ; logs go to
rem  board\logs. The project root is never touched.
rem ============================================================

set "HERE=%~dp0"
set "PROJ=%HERE%.."
for %%I in ("%PROJ%") do set "PROJ=%%~fI"
set "WORK=%PROJ%\board\prj"
set "LOGS=%PROJ%\board\logs"

rem ---------- -recreate: delete the old project BEFORE entering it ----
rem (Windows refuses to delete a directory that is some process's cwd,
rem  so this must happen while we are still outside board\prj)
set "RECREATE="
if not "%~1"=="" for %%A in (%*) do if /i "%%~A"=="-recreate" set "RECREATE=1"
if defined RECREATE if exist "%WORK%" (
    echo [info] removing old project: %WORK%
    rmdir /s /q "%WORK%"
)

if not exist "%WORK%" mkdir "%WORK%"
if not exist "%LOGS%" mkdir "%LOGS%"
rem launch Vivado *inside* the project dir so cwd-relative files stay there
pushd "%WORK%" || (echo [ERR] cannot enter "%WORK%" & pause & exit /b 1)

title Vivado project - %~nx0
echo ============================================================
echo  Vivado project generator
echo  Root   : %PROJ%
echo  Project: %WORK%
echo ============================================================
echo.

rem ---------- batch mode? --------------------------------------
set "MODE=gui"
if /i "%~1"=="--batch" set "MODE=batch"
if /i "%~1"=="-batch"  set "MODE=batch"

rem ---------- find Vivado --------------------------------------
rem   1) environment variables
rem   2) find_vivado.ps1: registry install records + common layouts (any drive)
rem   3) hard-coded fallback list
set "VIVADO="
if defined XILINX_VIVADO if exist "%XILINX_VIVADO%\bin\vivado.bat" set "VIVADO=%XILINX_VIVADO%\bin\vivado.bat"
if not defined VIVADO if defined VIVADO_ROOT if exist "%VIVADO_ROOT%\bin\vivado.bat" set "VIVADO=%VIVADO_ROOT%\bin\vivado.bat"
if not defined VIVADO if exist "%HERE%find_vivado.ps1" for /f "delims=" %%P in ('powershell -NoProfile -ExecutionPolicy Bypass -File "%HERE%find_vivado.ps1" 2^>nul') do set "VIVADO=%%P"
if not defined VIVADO if exist "D:\2026.1\Vivado\bin\vivado.bat" set "VIVADO=D:\2026.1\Vivado\bin\vivado.bat"
if not defined VIVADO if exist "C:\2026.1\Vivado\bin\vivado.bat" set "VIVADO=C:\2026.1\Vivado\bin\vivado.bat"
if not defined VIVADO if exist "E:\2026.1\Vivado\bin\vivado.bat" set "VIVADO=E:\2026.1\Vivado\bin\vivado.bat"
if not defined VIVADO if exist "C:\Xilinx\Vivado\2026.1\bin\vivado.bat" set "VIVADO=C:\Xilinx\Vivado\2026.1\bin\vivado.bat"
if not defined VIVADO if exist "D:\Xilinx\Vivado\2026.1\bin\vivado.bat" set "VIVADO=D:\Xilinx\Vivado\2026.1\bin\vivado.bat"
if not defined VIVADO if exist "D:\AMDDesignTools\2026.1\Vivado\bin\vivado.bat" set "VIVADO=D:\AMDDesignTools\2026.1\Vivado\bin\vivado.bat"
if not defined VIVADO if exist "C:\Program Files\AMD\Vivado\2026.1\bin\vivado.bat" set "VIVADO=C:\Program Files\AMD\Vivado\2026.1\bin\vivado.bat"
if not defined VIVADO goto novivado

echo [info] using Vivado: %VIVADO%
echo [info] mode: %MODE%
echo.

set "EXTRA="
if /i "%MODE%"=="batch" set "EXTRA=-batch"

call "%VIVADO%" -mode %MODE% -log "%LOGS%\vivado.log" -journal "%LOGS%\vivado.jou" -source "%HERE%create_vivado_project.tcl" -tclargs %EXTRA% %*
set "RC=%ERRORLEVEL%"

echo.
if "%RC%"=="0" goto done_ok
echo ------------------------------------------------------------
echo  [FAILED] exit code %RC%  (see %LOGS%\vivado.log)
echo ------------------------------------------------------------
goto done_end

:done_ok
echo ------------------------------------------------------------
echo  [OK] done   (project: %WORK%\prj.xpr)
echo ------------------------------------------------------------

:done_end
popd
pause
exit /b %RC%

:novivado
echo ------------------------------------------------------------
echo  [ERR] Vivado not found.
echo.
echo  Already searched: XILINX_VIVADO / VIVADO_ROOT, PATH,
echo  Windows install registry (find_vivado.ps1) and common folders.
echo  The folder you give must contain bin\vivado.bat.
echo.
echo  Do one of the following:
echo    1^) tell this script where Vivado is:
echo         set VIVADO_ROOT=D:\2026.1\Vivado
echo         create_vivado_project.bat
echo    2^) run it inside a Vivado command prompt (XILINX_VIVADO is set):
echo         vivado -mode gui -source build\create_vivado_project.tcl
echo    3^) install AMD Vivado 2026.1
echo ------------------------------------------------------------
popd
pause
exit /b 2
