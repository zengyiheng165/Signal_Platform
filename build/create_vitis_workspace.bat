@echo off
setlocal EnableExtensions
rem ============================================================
rem  Create / update the Vitis workspace of this project.
rem  Usage:  double click this file, or run it from cmd:
rem      create_vitis_workspace.bat
rem      create_vitis_workspace.bat --clean
rem      create_vitis_workspace.bat --vitis "D:\2026.1\Vitis"
rem ============================================================

set "HERE=%~dp0"
set "PROJ=%HERE%.."
pushd "%PROJ%" || (echo [ERR] cannot enter project folder & pause & exit /b 1)

title Vitis workspace - %~nx0
echo ============================================================
echo  Vitis workspace generator
echo  Project: %CD%
echo ============================================================
echo.

rem ---------- 1) try a real Python ------------------------------
set "PY="
python -c "import sys" >nul 2>&1
if not errorlevel 1 set "PY=python"
if not defined PY (
    py -3 -c "import sys" >nul 2>&1
    if not errorlevel 1 set "PY=py -3"
)
if not defined PY (
    python3 -c "import sys" >nul 2>&1
    if not errorlevel 1 set "PY=python3"
)

if not defined PY goto no_python

echo [info] using Python: %PY%
%PY% "%HERE%create_vitis_workspace.py" %*
set "RC=%ERRORLEVEL%"
goto done

rem ---------- 2) fall back to Vitis bundled python --------------
:no_python
echo [info] no usable Python found, switching to the Python inside Vitis ...

set "VITIS_BAT="
if defined XILINX_VITIS if exist "%XILINX_VITIS%\bin\vitis.bat" set "VITIS_BAT=%XILINX_VITIS%\bin\vitis.bat"
if not defined VITIS_BAT if defined VITIS_ROOT if exist "%VITIS_ROOT%\bin\vitis.bat" set "VITIS_BAT=%VITIS_ROOT%\bin\vitis.bat"
if not defined VITIS_BAT if exist "D:\2026.1\Vitis\bin\vitis.bat" set "VITIS_BAT=D:\2026.1\Vitis\bin\vitis.bat"
if not defined VITIS_BAT if exist "C:\2026.1\Vitis\bin\vitis.bat" set "VITIS_BAT=C:\2026.1\Vitis\bin\vitis.bat"
if not defined VITIS_BAT if exist "E:\2026.1\Vitis\bin\vitis.bat" set "VITIS_BAT=E:\2026.1\Vitis\bin\vitis.bat"
if not defined VITIS_BAT if exist "C:\Xilinx\Vitis\2026.1\bin\vitis.bat" set "VITIS_BAT=C:\Xilinx\Vitis\2026.1\bin\vitis.bat"
if not defined VITIS_BAT if exist "D:\Xilinx\Vitis\2026.1\bin\vitis.bat" set "VITIS_BAT=D:\Xilinx\Vitis\2026.1\bin\vitis.bat"
if not defined VITIS_BAT if exist "D:\Xilinx\2026.1\Vitis\bin\vitis.bat" set "VITIS_BAT=D:\Xilinx\2026.1\Vitis\bin\vitis.bat"
if not defined VITIS_BAT if exist "C:\Xilinx\2026.1\Vitis\bin\vitis.bat" set "VITIS_BAT=C:\Xilinx\2026.1\Vitis\bin\vitis.bat"
if not defined VITIS_BAT if exist "D:\AMDDesignTools\2026.1\Vitis\bin\vitis.bat" set "VITIS_BAT=D:\AMDDesignTools\2026.1\Vitis\bin\vitis.bat"
if not defined VITIS_BAT if exist "E:\AMDDesignTools\2026.1\Vitis\bin\vitis.bat" set "VITIS_BAT=E:\AMDDesignTools\2026.1\Vitis\bin\vitis.bat"
if not defined VITIS_BAT if exist "C:\Program Files\AMD\Vitis\2026.1\bin\vitis.bat" set "VITIS_BAT=C:\Program Files\AMD\Vitis\2026.1\bin\vitis.bat"

if not defined VITIS_BAT goto noenv

echo [info] using Vitis: %VITIS_BAT%
echo.
rem  relative script path: keeps the Vitis launcher happy even if the project path has spaces
call "%VITIS_BAT%" -s build\create_vitis_workspace.py %*
set "RC=%ERRORLEVEL%"
goto done

rem ---------- done ----------------------------------------------
:done
echo.
if "%RC%"=="0" goto done_ok
echo ------------------------------------------------------------
echo  [FAILED] exit code %RC%
echo  read the messages above, and check board\logs\ for details
echo ------------------------------------------------------------
goto done_end

:done_ok
echo ------------------------------------------------------------
echo  [OK] done
echo ------------------------------------------------------------

:done_end
popd
pause
exit /b %RC%

:noenv
echo ------------------------------------------------------------
echo  [ERR] Python not found and Vitis not found automatically.
echo.
echo  Do one of the following:
echo    1^) run it inside a Vitis command prompt:
echo         vitis -s build\create_vitis_workspace.py
echo    2^) tell this script where Vitis is, for example:
echo         set VITIS_ROOT=D:\2026.1\Vitis
echo         create_vitis_workspace.bat
echo    3^) install Python 3 and make sure "python" works in cmd
echo ------------------------------------------------------------
popd
pause
exit /b 2
