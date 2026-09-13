@echo off
REM ---------------------------------------------------------------------------
REM  Installs the SmartMic virtual microphone.
REM  Run as ADMINISTRATOR, from the folder containing smartmic.sys/.inf/.cat.
REM ---------------------------------------------------------------------------
setlocal

net session >nul 2>&1
if errorlevel 1 (
    echo This script must be run as Administrator.
    echo Right-click it and choose "Run as administrator".
    pause
    exit /b 1
)

if not exist smartmic.inf (
    echo smartmic.inf not found. Run this from the build output folder.
    pause
    exit /b 1
)

echo.
echo Checking test signing...
bcdedit /enum {current} | findstr /i "testsigning" | findstr /i "Yes" >nul
if errorlevel 1 (
    echo.
    echo   Test signing is OFF. A test-signed driver cannot load without it.
    echo   Enabling it now; you will need to REBOOT before the driver will load.
    echo.
    bcdedit /set testsigning on
    set NEEDREBOOT=1
)

echo.
echo Installing the driver package...
pnputil /add-driver smartmic.inf /install
if errorlevel 1 goto :failed

echo.
echo Creating the root-enumerated device node...
where devcon >nul 2>&1
if errorlevel 1 (
    echo   devcon.exe not found on PATH.
    echo   It ships with the WDK, typically at:
    echo     C:\Program Files (x86)\Windows Kits\10\Tools\10.0.*\x64\devcon.exe
    echo   Copy it next to this script and re-run, or add the device manually:
    echo     Device Manager -^> Action -^> Add legacy hardware -^> "SmartMic Virtual Audio Device"
    goto :done
)
devcon install smartmic.inf root\smartmic
if errorlevel 1 goto :failed

:done
echo.
echo ============================================================================
if defined NEEDREBOOT (
    echo  REBOOT REQUIRED -- test signing was just enabled.
    echo  After rebooting, open Sound settings and look for "Smart Microphone".
) else (
    echo  Installed. Open Sound settings; you should see "Smart Microphone".
)
echo ============================================================================
pause
exit /b 0

:failed
echo.
echo *** INSTALL FAILED. Collect the log with:
echo       type %%SystemRoot%%\INF\setupapi.dev.log
pause
exit /b 1
