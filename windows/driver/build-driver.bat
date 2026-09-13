@echo off
REM ---------------------------------------------------------------------------
REM  Builds SmartMic.Driver and produces a test-signed, installable package.
REM
REM  Run this from an ELEVATED "x64 Native Tools Command Prompt for VS 2022"
REM  (Start menu -> Visual Studio 2022 -> right-click -> Run as administrator).
REM ---------------------------------------------------------------------------
setlocal

echo.
echo === 1/4  Building the driver =================================================
msbuild SmartMicDriver.vcxproj /p:Configuration=Release /p:Platform=x64 /m
if errorlevel 1 goto :failed

set OUT=x64\Release\smartmic
if not exist "%OUT%\smartmic.sys" set OUT=x64\Release
if not exist "%OUT%\smartmic.sys" (
    echo Could not find smartmic.sys under x64\Release. Check the build output above.
    goto :failed
)

echo.
echo === 2/4  Creating a test certificate ========================================
REM  Development only. Production uses the EV certificate and Microsoft
REM  attestation signing -- see docs/adr/ADR-010-driver-signing-and-release.md.
certutil -store Root SmartMicTestCert >nul 2>&1
if errorlevel 1 (
    powershell -Command "$cert = New-SelfSignedCertificate -Subject 'CN=SmartMicTestCert' -CertStoreLocation 'Cert:\CurrentUser\My' -Type CodeSigningCert; Export-Certificate -Cert $cert -FilePath 'SmartMicTestCert.cer'"
    certutil -addstore Root SmartMicTestCert.cer
    certutil -addstore TrustedPublisher SmartMicTestCert.cer
) else (
    echo     certificate already present, reusing it
)

echo.
echo === 3/4  Building the catalog ===============================================

REM Find inf2cat, signtool, and stampinf since they are not in PATH on the CI runner
set INF2CAT=inf2cat
set SIGNTOOL=signtool
for /f "delims=" %%i in ('dir /s /b "C:\Program Files (x86)\Windows Kits\10\bin\inf2cat.exe" 2^>nul') do set INF2CAT="%%i"
for /f "delims=" %%i in ('dir /s /b "C:\Program Files (x86)\Windows Kits\10\bin\signtool.exe" 2^>nul') do set SIGNTOOL="%%i"
for /f "delims=" %%i in ('dir /s /b "C:\Program Files (x86)\Windows Kits\10\bin\stampinf.exe" 2^>nul') do set STAMPINF="%%i"

echo.
echo === 2.5/4 Stamping INF file =================================================
copy /y smartmic.inf "%OUT%\smartmic.inf"
%STAMPINF% -d "*" -a "amd64" -v "*" -k "1.15" -f "%OUT%\smartmic.inf"

pushd "%OUT%"
%INF2CAT% /driver:. /os:10_X64 /verbose
if errorlevel 1 (popd & goto :failed)

echo.
echo === 4/4  Signing ============================================================
%SIGNTOOL% sign /a /v /s PrivateCertStore /n SmartMicTestCert /fd sha256 /t http://timestamp.digicert.com smartmic.cat
if errorlevel 1 (popd & goto :failed)
%SIGNTOOL% sign /a /v /s PrivateCertStore /n SmartMicTestCert /fd sha256 /t http://timestamp.digicert.com smartmic.sys
if errorlevel 1 (popd & goto :failed)
popd

echo.
echo ============================================================================
echo  Done. The installable package is in:  %OUT%
echo    smartmic.sys   smartmic.inf   smartmic.cat
echo.
echo  Next: run  install-driver.bat  from that folder, as administrator.
echo ============================================================================
exit /b 0

:failed
echo.
echo *** BUILD FAILED -- see the errors above.
exit /b 1
