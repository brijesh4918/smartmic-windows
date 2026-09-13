@echo off
REM ---------------------------------------------------------------------------
REM  Installs the SmartMic virtual microphone.
REM
REM  Run as ADMINISTRATOR, from the folder containing smartmic.sys / .inf / .cat
REM  / SmartMicTestCert.cer  (that is the folder you unzipped from the GitHub
REM  Actions artifact).
REM ---------------------------------------------------------------------------
setlocal

net session >nul 2>&1
if errorlevel 1 (
    echo This script must be run as Administrator.
    echo Right-click it and choose "Run as administrator".
    pause
    exit /b 1
)

cd /d "%~dp0"

if not exist smartmic.inf (
    echo smartmic.inf not found next to this script.
    echo Run it from the unzipped artifact folder.
    pause
    exit /b 1
)
if not exist smartmic.sys ( echo smartmic.sys is missing. & pause & exit /b 1 )
if not exist smartmic.cat ( echo smartmic.cat is missing -- the package is unsigned. & pause & exit /b 1 )

echo.
echo === 1/4  Trusting the test certificate ======================================
if not exist SmartMicTestCert.cer (
    echo SmartMicTestCert.cer is missing from this folder.
    echo Windows will refuse to load a driver it cannot verify.
    pause
    exit /b 1
)
REM  The driver is signed with a certificate this machine has never seen. Both
REM  stores are needed: Root so the signature chain validates, TrustedPublisher
REM  so the install does not stop to ask.
certutil -addstore Root SmartMicTestCert.cer
certutil -addstore TrustedPublisher SmartMicTestCert.cer

echo.
echo === 2/4  Test signing =======================================================
set NEEDREBOOT=
bcdedit /enum {current} | findstr /i "testsigning" | findstr /i "Yes" >nul
if errorlevel 1 (
    echo     Test signing is OFF. A test-signed driver cannot load without it.
    bcdedit /set testsigning on
    if errorlevel 1 (
        echo.
        echo     Could not enable test signing. If this machine has Secure Boot
        echo     enabled, turn Secure Boot off in the firmware first -- Windows
        echo     will not allow test signing while it is on.
        pause
        exit /b 1
    )
    set NEEDREBOOT=1
    echo     Enabled. A REBOOT is required before the driver can load.
) else (
    echo     already on
)

echo.
echo === 3/4  Installing the driver package ======================================
pnputil /add-driver smartmic.inf /install
if errorlevel 1 goto :failed

echo.
echo === 4/4  Creating the device ================================================
REM  devcon is a WDK tool that is often simply absent, and when it is absent
REM  the old version of this script reported success having created nothing.
REM  The PowerShell helper makes the same SetupAPI calls devcon does, so there
REM  is nothing to be missing.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-device.ps1" -InfPath "%~dp0smartmic.inf"
if errorlevel 1 (
    echo.
    echo     Creating the device node failed. As a last resort you can do it by
    echo     hand: Device Manager -^> Action -^> Add legacy hardware -^> Next
    echo     -^> "Install the hardware that I manually select" -^> Show All Devices
    echo     -^> Have Disk -^> point it at smartmic.inf
    goto :failed
)

echo.
echo ============================================================================
if defined NEEDREBOOT (
    echo  REBOOT NOW -- test signing was just switched on, and the driver cannot
    echo  load until you do. After rebooting, run this script again.
) else (
    echo  Installed. Check it:
    echo    - Settings -^> System -^> Sound -^> All sound devices -^> "Smart Microphone"
    echo    - Device Manager -^> Sound, video and game controllers
    echo.
    echo  Then confirm the service can see it:
    echo    smartmic-service.exe --diagnose
)
echo ============================================================================
pause
exit /b 0

:failed
echo.
echo *** INSTALL FAILED.
echo.
echo Send me these two things and I can tell you exactly what went wrong:
echo   1. everything printed above
echo   2. the end of this file:  %SystemRoot%\INF\setupapi.dev.log
echo.
echo Also useful: Device Manager, find "SmartMic Virtual Audio Device",
echo double-click it, and copy the text under "Device status".
pause
exit /b 1
