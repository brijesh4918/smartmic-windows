@echo off
REM  Removes the SmartMic virtual microphone. Run as ADMINISTRATOR.
setlocal

net session >nul 2>&1
if errorlevel 1 (echo Run as Administrator. & pause & exit /b 1)

where devcon >nul 2>&1
if not errorlevel 1 devcon remove root\smartmic

echo Removing the driver package from the Driver Store...
for /f "tokens=2 delims=: " %%i in ('pnputil /enum-drivers ^| findstr /i /c:"smartmic.inf" ^| findstr /i "oem"') do (
    pnputil /delete-driver %%i /uninstall /force
)

echo.
echo Done. Test signing was left as-is; to turn it off:  bcdedit /set testsigning off
pause
