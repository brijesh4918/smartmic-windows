SmartMic driver -- how to install
=================================

DO NOT right-click smartmic.inf and choose "Install".

It will fail with "The INF file you selected does not support this method of
installation", and even if it appeared to work it would not finish the job.
This is a root-enumerated virtual device: there is no physical hardware for
Windows to match the driver against, so the device node has to be created
explicitly. Right-click Install does not do that.

Instead
-------

Right-click  install-driver.bat  ->  "Run as administrator".

It does the four things that have to happen, in the order they have to happen:

  1. imports SmartMicTestCert.cer into Root and TrustedPublisher
     (the driver is signed with a certificate this PC has never seen; without
     this step Windows rejects the signature and the driver never loads)
  2. turns on test signing if it is off
  3. stages the package with pnputil
  4. creates the device node (install-device.ps1, which makes the same
     SetupAPI calls devcon does -- devcon itself ships with the WDK and is
     frequently missing, and its absence used to fail silently)

If it turns test signing on, REBOOT, then run install-driver.bat again.

Secure Boot
-----------

If step 2 fails, this PC has Secure Boot enabled. Windows will not allow test
signing while Secure Boot is on. Turn Secure Boot off in the firmware setup
(usually F2 or Del during boot), then run install-driver.bat again.

Checking it worked
------------------

    Settings -> System -> Sound -> All sound devices  ->  "Smart Microphone"
    Device Manager -> Sound, video and game controllers
                      -> "SmartMic Virtual Audio Device", no warning triangle

Then, from the service folder:

    smartmic-service.exe --diagnose

That prints three separate answers -- package installed, driver loaded,
control interface present -- so you can see which one is missing.

If it does not work
-------------------

Send these:

  1. the output of  smartmic-service.exe --diagnose
  2. the bottom of  %SystemRoot%\INF\setupapi.dev.log
  3. Device Manager -> SmartMic Virtual Audio Device -> double-click
     -> the text under "Device status"    (code 52 = signature rejected)

Doing it by hand instead
------------------------

If you would rather not run the script:

    certutil -addstore Root SmartMicTestCert.cer
    certutil -addstore TrustedPublisher SmartMicTestCert.cer
    bcdedit /set testsigning on
    (reboot)
    pnputil /add-driver smartmic.inf /install
    powershell -ExecutionPolicy Bypass -File install-device.ps1 -InfPath smartmic.inf

all from an Administrator command prompt, in this folder.

Or through Device Manager, after the certutil and bcdedit steps and a reboot:
Action -> Add legacy hardware -> Next -> "Install the hardware that I manually
select" -> Show All Devices -> Have Disk -> point it at smartmic.inf.

Removing it
-----------

    uninstall-driver.bat        (as administrator)
    bcdedit /set testsigning off
