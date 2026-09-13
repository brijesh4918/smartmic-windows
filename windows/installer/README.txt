SmartMic
========

Use your phone as your PC's microphone. Hold a button on the phone and your
voice goes into Teams, Zoom, Discord or anything else, then hands straight back
to the PC's own microphone when you let go.

INSTALL  (once per PC)
----------------------

  1. Right-click  Install-SmartMic.bat  ->  "Run as administrator"
  2. If it says a reboot is needed, restart and run it once more.
  3. Start SmartMic from the Start Menu.

That is everything. The driver and the application are both installed by that
one script; there is nothing else to download.

The first install turns on Windows "test signing", because the driver is signed
with our own certificate rather than one Microsoft has countersigned. You will
see a "Test Mode" watermark in the corner of the desktop. That goes away once
the driver is signed through Microsoft's hardware programme.

If step 1 fails saying test signing could not be enabled, Secure Boot is on.
Turn it off in the firmware setup (usually F2 or Del during boot) and run the
installer again.

CONNECT THE PHONE
-----------------

Start SmartMic. It prints something like:

    pairing code  731482   (expires in 5 minutes)
    qr            smartmic://pair?v=1&host=192.168.1.20&...

Copy that smartmic:// line to your phone (any messaging app will do), then in
the SmartMic app tap "Paste pairing link" and "Pair".

Both devices need to be on the same Wi-Fi. For using it from elsewhere, see
docs/REMOTE-ACCESS.md.

CHECK IT IS WORKING
-------------------

Press Win+R, type  mmsys.cpl  , press Enter, and open the Recording tab.
Hold the button on the phone and talk: the green bar next to the SmartMic
device should move. That is the proof that audio is really crossing over.

Then pick that device as your microphone in Teams or Zoom.

IF SOMETHING IS WRONG
---------------------

    "%ProgramFiles%\SmartMic\smartmic-service.exe" --diagnose

reports four things separately -- driver package installed, driver loaded,
control interface present, microphone published -- so you can see which one is
missing rather than guessing.

REMOVE IT
---------

  Right-click  Uninstall-SmartMic.bat  ->  "Run as administrator"
