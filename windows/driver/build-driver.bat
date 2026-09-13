@echo off
REM ---------------------------------------------------------------------------
REM  Builds SmartMic.Driver and produces a test-signed, installable package.
REM
REM  Works both on a developer machine (elevated "x64 Native Tools Command
REM  Prompt for VS 2022") and on a GitHub Actions windows runner.
REM
REM  Two things here are load-bearing and were previously wrong:
REM
REM    1. Signing must happen in CI too. An UNSIGNED 64-bit kernel driver
REM       cannot load, ever -- test signing allows a self-signed certificate
REM       you trust, it does not allow no signature at all. Skipping this step
REM       in CI produced a package that installed and then silently never
REM       loaded.
REM
REM    2. The .sys must be signed BEFORE inf2cat runs. The catalog stores a
REM       hash of every file in the package; signing the .sys afterwards
REM       changes it and the catalog no longer matches.
REM ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

echo.
echo === 1/6  Building the driver =================================================
msbuild SmartMicDriver.vcxproj /p:Configuration=Release /p:Platform=x64 /m
if errorlevel 1 goto :failed

set OUT=x64\Release\smartmic
if not exist "%OUT%\smartmic.sys" set OUT=x64\Release
if not exist "%OUT%\smartmic.sys" (
    echo Could not find smartmic.sys under x64\Release. Check the build output above.
    goto :failed
)
echo     output folder: %OUT%

echo.
echo === 2/6  Locating the WDK tools ==============================================
set INF2CAT=
set SIGNTOOL=
set STAMPINF=
for /f "delims=" %%i in ('dir /s /b "C:\Program Files (x86)\Windows Kits\10\bin\inf2cat.exe" 2^>nul') do set INF2CAT="%%i"
for /f "delims=" %%i in ('dir /s /b "C:\Program Files (x86)\Windows Kits\10\bin\x64\signtool.exe" 2^>nul') do set SIGNTOOL="%%i"
if not defined SIGNTOOL for /f "delims=" %%i in ('dir /s /b "C:\Program Files (x86)\Windows Kits\10\bin\signtool.exe" 2^>nul') do set SIGNTOOL="%%i"
for /f "delims=" %%i in ('dir /s /b "C:\Program Files (x86)\Windows Kits\10\bin\stampinf.exe" 2^>nul') do set STAMPINF="%%i"

if not defined INF2CAT ( echo inf2cat.exe not found -- is the WDK installed? & goto :failed )
if not defined SIGNTOOL ( echo signtool.exe not found -- is the Windows SDK installed? & goto :failed )
echo     inf2cat  : %INF2CAT%
echo     signtool : %SIGNTOOL%

echo.
echo === 3/6  Staging the INF =====================================================
copy /y smartmic.inf "%OUT%\smartmic.inf" >nul
if defined STAMPINF %STAMPINF% -d "*" -a "amd64" -v "*" -k "1.15" -f "%OUT%\smartmic.inf"

echo.
echo === 4/6  Test certificate ====================================================
REM  Development only. Production uses the EV certificate and Microsoft
REM  attestation signing -- see docs/adr/ADR-010-driver-signing-and-release.md.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make-test-cert.ps1" -OutDir "%OUT%"
if errorlevel 1 goto :failed
if not exist "%OUT%\SmartMicTestCert.cer" ( echo failed to export the test certificate & goto :failed )

REM  Trust it locally so a developer machine can install straight away. On a CI
REM  runner this is harmless and throws the certificate away with the VM; the
REM  .cer travels in the artifact and install-driver.bat imports it there.
certutil -addstore Root "%OUT%\SmartMicTestCert.cer" >nul 2>&1
certutil -addstore TrustedPublisher "%OUT%\SmartMicTestCert.cer" >nul 2>&1

pushd "%OUT%"

echo.
echo === 5/6  Signing the driver binary ==========================================
REM  Before the catalog, not after: the catalog hashes this file.
call :sign smartmic.sys
if errorlevel 1 (popd & goto :failed)

echo.
echo === 6/6  Catalog =============================================================
%INF2CAT% /driver:. /os:10_X64 /verbose
if errorlevel 1 (popd & goto :failed)
call :sign smartmic.cat
if errorlevel 1 (popd & goto :failed)

echo.
echo     verifying...
%SIGNTOOL% verify /pa /v smartmic.cat
if errorlevel 1 echo     NOTE: verify reported a problem; on a machine that has not imported
if errorlevel 1 echo           SmartMicTestCert.cer that is expected.
popd

copy /y install-driver.bat "%OUT%\" >nul 2>&1
copy /y README-INSTALL.txt "%OUT%\" >nul 2>&1
copy /y uninstall-driver.bat "%OUT%\" >nul 2>&1

REM  devcon creates the root-enumerated device node. It ships with the WDK and
REM  is not on PATH on a clean machine, so it travels with the package -- there
REM  is no built-in Windows command that does this job.
set DEVCON=
for /f "delims=" %%i in ('dir /s /b "C:\Program Files (x86)\Windows Kits\10\Tools\*\x64\devcon.exe" 2^>nul') do set DEVCON="%%i"
if not defined DEVCON for /f "delims=" %%i in ('dir /s /b "C:\Program Files (x86)\Windows Kits\10\Tools\x64\devcon.exe" 2^>nul') do set DEVCON="%%i"
if defined DEVCON (
    copy /y %DEVCON% "%OUT%\devcon.exe" >nul 2>&1
    echo     bundled devcon.exe
) else (
    echo     WARNING: devcon.exe not found; install-driver.bat will fall back to
    echo              the "Add legacy hardware" wizard.
)

echo.
echo ============================================================================
echo  Done. The installable package is in:  %OUT%
echo.
dir /b "%OUT%\smartmic.sys" "%OUT%\smartmic.inf" "%OUT%\smartmic.cat" "%OUT%\SmartMicTestCert.cer"
echo.
echo  Next: on the target PC, run install-driver.bat as Administrator.
echo ============================================================================
exit /b 0

REM ---------------------------------------------------------------------------
:sign
REM  %1 = file to sign. Timestamping needs the network; a test signature is
REM  perfectly usable without one, so a timestamp failure is not fatal.
%SIGNTOOL% sign /v /sm /s My /n SmartMicTestCert /fd sha256 ^
    /tr http://timestamp.digicert.com /td sha256 %1
if not errorlevel 1 exit /b 0
echo     timestamping failed; signing without a timestamp
%SIGNTOOL% sign /v /sm /s My /n SmartMicTestCert /fd sha256 %1
exit /b %errorlevel%

:failed
echo.
echo *** BUILD FAILED -- see the errors above.
exit /b 1
