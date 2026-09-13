# Installing the SmartMic app on your phone

**File:** `SmartMic-0.3.0-arm64.apk` (19 MB)

**Requirements:** Android 9 or newer, 64-bit (any phone from about 2018 onward).

## Install

1. Copy the APK to your phone — USB cable, Google Drive, or email it to yourself.
2. Tap it. Android will say the app is from an unknown source; choose
   **Settings → Allow from this source**, then go back and tap **Install**.
   This is normal for an app that is not from the Play Store.
3. Open **SmartMic**. It will ask for microphone permission. Say yes — without
   it the app has nothing to send.

## Pair it with your PC

1. On the PC, run the service:

```bash
smartmic-service --port 47820
```

   It prints something like:

```
  pairing code  731482   (expires in 5 minutes)
  session       a3f9c21e4b7d8e05
  qr            smartmic://pair?v=1&host=192.168.1.20&port=47820&...
```

2. **Easiest way:** copy that whole `smartmic://...` line, send it to your phone
   (any messaging app), copy it there, and tap **Paste pairing link** in the
   app. Everything fills in, including the PC's fingerprint.

3. **Or type it in:** the PC address (its LAN IP, e.g. `192.168.1.20`), the
   session, and the six-digit code.

4. Tap **Pair**. Within a second you should see **Connected** and a green dot.

Both devices must be on the same Wi-Fi network.

## Use it

Hold the big button. It goes amber (**CONNECTING**) while the PC gets ready,
then green (**LIVE**) with a pulsing ring the moment the PC actually accepts
your microphone — and the phone buzzes. Let go and it hands straight back to
the PC's own microphone.

The button never says LIVE until the PC has confirmed it. If it stays on
CONNECTING and then says FAILED, the PC did not take the microphone — the
service is probably not running, or the Wi-Fi dropped.

The four buttons at the bottom control the PC's microphone from across the room:

| | |
|---|---|
| **Auto** | normal — PC mic, phone takes over while you hold the button |
| **Phone** | the phone is the microphone all the time |
| **PC only** | the PC's own mic, and the hold button does nothing |
| **Mute** | silence |

## If something goes wrong

**"Could not start the SmartMic engine"** — the phone could not reach the PC.
Check both are on the same Wi-Fi, and that the PC's firewall allows UDP 47820.

**Pairing fails** — the code lasts five minutes and works once. Restart the
service on the PC to get a new one. After five wrong attempts it stops
accepting guesses until you generate a new code; that is deliberate.

**The app installs but crashes on open** — send me the log:

```bash
adb logcat -d | grep -i -E "smartmic|AndroidRuntime"
```

**It connects but the PC hears nothing** — check the microphone permission in
Android Settings → Apps → SmartMic → Permissions, and send me the service's
console output from the PC.

## What is not in this build yet

- **QR scanning.** Pasting the link does the same job; the camera scanner is
  next.
- **Multiple PCs.** One PC at a time for now.
- **Reconnect without re-pairing.** The keys are already saved on both sides;
  wiring up the no-code reconnect is a small, self-contained piece of work.
- **Holding PTT with the screen off.** The foreground-service permissions are
  declared but the service itself is not implemented yet.
