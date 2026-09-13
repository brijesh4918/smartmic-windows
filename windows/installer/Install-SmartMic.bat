@echo off
REM ===========================================================================
REM  SmartMic -- complete installer.
REM
REM  Installs the virtual microphone driver and the SmartMic service in one go.
REM  Run this as Administrator. Nothing else needs installing.
REM ===========================================================================
setlocal enabledelayedexpansion
cd /d "%~dp0"

set TARGET=%ProgramFiles%\SmartMic

echo.
echo  ============================================================
echo   SmartMic installer
echo  ============================================================
echo.

net session >nul 2>&1
if errorlevel 1 (
    echo  This installer must run as Administrator.
    echo  Right-click Install-SmartMic.bat and choose "Run as administrator".
    echo.
    pause
    exit /b 1
)

if not exist "driver\smartmic.inf" (
    echo  driver\smartmic.inf is missing. Unzip the whole download and run this
    echo  from inside the unzipped folder.
    pause
    exit /b 1
)
if not exist "service\smartmic-service.exe" (
    echo  service\smartmic-service.exe is missing. Unzip the whole download.
    pause
    exit /b 1
)

REM -------------------------------------------------------------------------
echo  [1/5] Trusting the driver signature
REM  The driver is signed with a certificate built alongside it, which this PC
REM  has never seen. Without both stores Windows refuses to load the driver.
certutil -addstore Root "driver\SmartMicTestCert.cer" >nul
if errorlevel 1 ( echo        FAILED to import the certificate & goto :failed )
certutil -addstore TrustedPublisher "driver\SmartMicTestCert.cer" >nul
echo        done

REM -------------------------------------------------------------------------
echo  [2/5] Checking test signing
set NEEDREBOOT=
bcdedit /enum {current} | findstr /i "testsigning" | findstr /i "Yes" >nul
if errorlevel 1 (
    bcdedit /set testsigning on >nul
    if errorlevel 1 (
        echo.
        echo        FAILED. Secure Boot is almost certainly on -- Windows will not
        echo        allow test signing while it is. Turn Secure Boot off in the
        echo        firmware setup ^(usually F2 or Del at boot^), then run this
        echo        installer again.
        goto :failed
    )
    set NEEDREBOOT=1
    echo        enabled -- a reboot is needed
) else (
    echo        already on
)

REM -------------------------------------------------------------------------
echo  [3/5] Installing the driver
pnputil /add-driver "driver\smartmic.inf" /install >nul
if errorlevel 1 ( echo        FAILED to stage the driver package & goto :failed )

powershell -NoProfile -ExecutionPolicy Bypass -File "driver\install-device.ps1" -InfPath "%~dp0driver\smartmic.inf" >nul
if errorlevel 1 (
    if defined NEEDREBOOT (
        echo        the device will be created after the reboot
    ) else (
        echo        FAILED to create the device
        goto :failed
    )
) else (
    echo        done
)

REM -------------------------------------------------------------------------
echo  [4/5] Installing the SmartMic service
if not exist "%TARGET%" mkdir "%TARGET%" >nul 2>&1
xcopy /y /q /e "service\*" "%TARGET%\" >nul
if errorlevel 1 ( echo        FAILED to copy files to "%TARGET%" & goto :failed )

REM  A Start Menu entry, so it does not have to be launched from a path nobody
REM  can remember.
set SM_MENU=%ProgramData%\Microsoft\Windows\Start Menu\Programs\SmartMic
if not exist "%SM_MENU%" mkdir "%SM_MENU%" >nul 2>&1
powershell -NoProfile -Command ^
  "$s=(New-Object -ComObject WScript.Shell).CreateShortcut('%SM_MENU%\SmartMic.lnk');" ^
  "$s.TargetPath='%TARGET%\smartmic-service.exe';" ^
  "$s.WorkingDirectory='%TARGET%';" ^
  "$s.Description='SmartMic - use your phone as this PC''s microphone';" ^
  "$s.Save()" >nul 2>&1
echo        installed to "%TARGET%"

REM -------------------------------------------------------------------------
echo  [5/5] Checking the result
if defined NEEDREBOOT (
    echo.
    echo  ============================================================
    echo   REBOOT REQUIRED
    echo.
    echo   Test signing was just switched on. Windows cannot load the
    echo   driver until you restart.
    echo.
    echo   Restart, then run this installer once more to finish.
    echo  ============================================================
    echo.
    pause
    exit /b 0
)

echo.
"%TARGET%\smartmic-service.exe" --diagnose
echo.
echo  ============================================================
echo   Installed.
echo.
echo   Start it from the Start Menu ^(search "SmartMic"^), or run:
echo     "%TARGET%\smartmic-service.exe"
echo.
echo   It prints a six-digit code and a smartmic:// link.
echo   Paste that link into the SmartMic app on your phone.
echo  ============================================================
echo.
pause
exit /b 0

:failed
echo.
echo  ============================================================
echo   INSTALL FAILED -- see the message above.
echo.
echo   Useful when reporting it:
echo     - everything printed above
echo     - the end of %SystemRoot%\INF\setupapi.dev.log
echo  ============================================================
echo.
pause
exit /b 1
