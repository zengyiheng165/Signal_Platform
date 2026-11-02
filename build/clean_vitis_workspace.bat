@echo off
setlocal EnableExtensions
rem ============================================================
rem  One-click cleanup of the generated Vitis workspace.
rem  Runs immediately, no confirmation asked.
rem
rem  Deletes : board\Vitis_WorkSpace
rem            board\.vitis_workspace.json
rem            the short-path junction (<drive>:\vws\...) used by Vitis
rem            board\logs  and  board\.vitis_create.lock
rem  Keeps   : src\ps, src\pl, all scripts
rem
rem  Usage:
rem      clean_vitis_workspace.bat              (just run it)
rem      clean_vitis_workspace.bat --force      (even if Vitis IDE is using it)
rem ============================================================

set "HERE=%~dp0"
set "PROJ=%HERE%.."
for %%I in ("%PROJ%") do set "PROJ=%%~fI"
pushd "%PROJ%" || (echo [ERR] cannot enter project folder & pause & exit /b 1)

title Clean Vitis workspace
echo ============================================================
echo  Vitis workspace cleanup
echo  Project: %CD%
echo ============================================================
echo.
echo  Deleting:
echo    board\Vitis_WorkSpace
echo    board\.vitis_workspace.json
echo    short-path junction used by Vitis (if any)
echo    board\logs  and  board\.vitis_create.lock
echo.
echo  Keeping: src\ps, src\pl, scripts
echo.

set "EXTRA="
:scanargs
if "%~1"=="" goto scanargs_done
if /i "%~1"=="--force" set "EXTRA=--force"
shift
goto scanargs
:scanargs_done

rem ---------- locate a Python (real one, not the Store stub) ----------
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

if defined PY goto run_python

rem ---------- otherwise use the Python inside Vitis ----------
set "VITIS_BAT="
if defined XILINX_VITIS if exist "%XILINX_VITIS%\bin\vitis.bat" set "VITIS_BAT=%XILINX_VITIS%\bin\vitis.bat"
if not defined VITIS_BAT if defined VITIS_ROOT if exist "%VITIS_ROOT%\bin\vitis.bat" set "VITIS_BAT=%VITIS_ROOT%\bin\vitis.bat"
if not defined VITIS_BAT if exist "D:\2026.1\Vitis\bin\vitis.bat" set "VITIS_BAT=D:\2026.1\Vitis\bin\vitis.bat"
if not defined VITIS_BAT if exist "C:\2026.1\Vitis\bin\vitis.bat" set "VITIS_BAT=C:\2026.1\Vitis\bin\vitis.bat"
if not defined VITIS_BAT if exist "E:\2026.1\Vitis\bin\vitis.bat" set "VITIS_BAT=E:\2026.1\Vitis\bin\vitis.bat"
if not defined VITIS_BAT if exist "C:\Xilinx\Vitis\2026.1\bin\vitis.bat" set "VITIS_BAT=C:\Xilinx\Vitis\2026.1\bin\vitis.bat"
if not defined VITIS_BAT if exist "D:\Xilinx\Vitis\2026.1\bin\vitis.bat" set "VITIS_BAT=D:\Xilinx\Vitis\2026.1\bin\vitis.bat"
if not defined VITIS_BAT if exist "D:\AMDDesignTools\2026.1\Vitis\bin\vitis.bat" set "VITIS_BAT=D:\AMDDesignTools\2026.1\Vitis\bin\vitis.bat"
if not defined VITIS_BAT if exist "D:\Xilinx\Vitis\2023.2\bin\vitis.bat" set "VITIS_BAT=D:\Xilinx\Vitis\2023.2\bin\vitis.bat"
if not defined VITIS_BAT if exist "C:\Xilinx\Vitis\2023.2\bin\vitis.bat" set "VITIS_BAT=C:\Xilinx\Vitis\2023.2\bin\vitis.bat"
if not defined VITIS_BAT goto nopython

echo [info] using the Python inside Vitis: %VITIS_BAT%
rem  relative script path keeps the Vitis launcher happy even if the project path has spaces
call "%VITIS_BAT%" -s build\create_vitis_workspace.py --purge %EXTRA%
set "RC=%ERRORLEVEL%"
goto done

:run_python
echo [info] using Python: %PY%
%PY% "%HERE%create_vitis_workspace.py" --purge %EXTRA%
set "RC=%ERRORLEVEL%"
goto done

rem ---------- finish ----------
:done
echo.
if "%RC%"=="0" goto done_ok
echo ------------------------------------------------------------
echo  [FAILED] exit code %RC%
echo ------------------------------------------------------------
goto done_end

:done_ok
echo ------------------------------------------------------------
echo  [OK] workspace cleaned
echo ------------------------------------------------------------

:done_end
popd
pause
exit /b %RC%

:nopython
echo ------------------------------------------------------------
echo  [ERR] No usable Python and no Vitis found.
echo.
echo  The workspace contains very long paths, so it is safer to let the
echo  Python helper delete it. Please do one of the following:
echo    1^) install Python 3 and make sure "python" works in cmd
echo    2^) run inside a Vitis command prompt:
echo         vitis -s build\create_vitis_workspace.py --purge
echo    3^) set VITIS_ROOT and run this file again:
echo         set VITIS_ROOT=D:\2026.1\Vitis
echo ------------------------------------------------------------
popd
pause
exit /b 2
