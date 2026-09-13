@echo off
REM  Removes SmartMic: the service files, the Start Menu entry, the device and
REM  the driver package. Run as Administrator.
setlocal
cd /d "%~dp0"

net session >nul 2>&1
if errorlevel 1 ( echo Run as Administrator. & pause & exit /b 1 )

set TARGET=%ProgramFiles%\SmartMic

echo Removing the device and driver package...
powershell -NoProfile -Command ^
  "Get-PnpDevice -FriendlyName '*SmartMic*' -ErrorAction SilentlyContinue | ForEach-Object { pnputil /remove-device $_.InstanceId }" >nul 2>&1

for /f "tokens=2 delims=: " %%i in ('pnputil /enum-drivers ^| findstr /i /c:"smartmic.inf" ^| findstr /i "oem"') do (
    pnputil /delete-driver %%i /uninstall /force >nul 2>&1
)

echo Removing the application...
rmdir /s /q "%TARGET%" >nul 2>&1
rmdir /s /q "%ProgramData%\Microsoft\Windows\Start Menu\Programs\SmartMic" >nul 2>&1

echo.
echo Done.
echo.
echo Test signing was left on. To turn it off (and lose the driver until you
echo reinstall):  bcdedit /set testsigning off
echo.
pause
