# Which file goes where

> **Run this on your Mac first.** While I was working, macOS's Gatekeeper
> daemon (`syspolicyd`) got stuck at 100% CPU. While it is stuck, **no
> newly-compiled program can start** — it hangs before it prints anything.
> That is a macOS problem, not a SmartMic one, and it will block every build
> step below until you clear it:
>
> ```bash
> sudo killall -9 syspolicyd
> ```
>
> It restarts itself immediately. A reboot works too. You can confirm it is
> fixed with `ps aux | grep syspolicyd` — it should be near 0% CPU.


You have a Mac and an iPhone. Here is the honest layout.

| Device | What runs there | Which file |
|---|---|---|
| **iPhone** | The SmartMic app | No file to copy. iPhone apps cannot be installed from a file — you build it from your Mac with Xcode. See `INSTALL-IPHONE.md`. |
| **Mac** | (a) Builds the iPhone app. (b) Can run the PC service in **test mode**. | The project folder. Nothing to install. |
| **Windows** | The driver, and the service that makes SmartMic a *real* microphone for Teams/Zoom. | `windows/driver/` → build there. See `INSTALL-DRIVER.md`. |

The Android APK I sent earlier is not useful to you — ignore it.

---

## The one thing to understand before you start

SmartMic's whole point is that Teams and Zoom see a microphone called
**Smart Microphone** that never changes, while the audio behind it switches
between your PC mic and your phone.

That microphone is created by a **Windows kernel driver**. There is no macOS
equivalent in this project — macOS would need its own, completely different
driver, which does not exist.

So:

- **On your Mac**, you can prove the whole pipeline works: the phone pairs, your
  voice is encrypted, sent, decoded and written to a `.wav` file you can play
  back. That is a real test of everything except the last step.
- **On Windows**, you get the actual product: your phone becomes the microphone
  inside a real Teams or Zoom call.

You need a Windows machine — or a Windows VM on your Mac — for the real thing.

---

# Path A — Test it today, Mac + iPhone only (about 40 minutes)

No Windows needed. This proves pairing, encryption, audio and push-to-talk all
work, and it is the right thing to do first: if something is broken, you find
out here rather than while also fighting a kernel driver.

### A1. Build the Mac side (5 minutes)

Open Terminal:

```bash
cd "<project folder>"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

Check it works:

```bash
./build/windows/audio-service/smartmic_tests
```

You should see `72 tests, 0 failed`.

### A2. Find your Mac's Wi-Fi address

```bash
ipconfig getifaddr en0
```

Write that down — something like `192.168.1.20`. If it prints nothing, try
`en1`.

### A3. Start the service on the Mac

```bash
./build/apps/smartmic-service --port 47820 --out ~/Desktop/smartmic-test.wav --duration 120
```

It prints a pairing code and a link:

```
  pairing code  731482   (expires in 5 minutes)
  session       a3f9c21e4b7d8e05
  qr            smartmic://pair?v=1&host=...&code=731482&...
```

Leave this running. It stops by itself after 2 minutes (`--duration 120`).

### A4. Put the app on your iPhone

Follow `INSTALL-IPHONE.md`. First time takes about 20 minutes, mostly Xcode.

### A5. Pair and talk

1. Copy the whole `smartmic://...` line from the Terminal and send it to
   yourself (Messages, Notes, anything). On the phone, copy it.
2. Open SmartMic on the iPhone → **Paste pairing link** → **Pair**.
3. Allow the microphone and local network prompts.
4. Hold the big button and talk. It should turn green and say **LIVE**, and
   buzz.

### A6. Listen to the result

When the service stops, open `~/Desktop/smartmic-test.wav`.

You will hear a steady tone (that is the stand-in for "the PC's own
microphone"), and **your own voice** in the stretches where you held the button.
The switch between them should be smooth, with no click.

That is the entire product working, except for the part that hands the audio to
Teams.

---

# Path B — The real thing, on Windows

You need Windows 11. A **virtual machine on your Mac is strongly recommended**
— a kernel driver bug crashes the whole machine, and in a VM that costs you a
VM restart and nothing else.

For Apple Silicon Macs: Parallels Desktop or VMware Fusion running
**Windows 11 ARM**. The driver project builds for ARM64 as well as x64.

### B1. Set up Windows and the tools

In the VM: install Visual Studio 2022 Community (with "Desktop development with
C++"), the Windows 11 SDK, and the WDK. Full detail in `INSTALL-DRIVER.md`.

### B2. Copy the project into Windows

Shared folder, USB, or `git clone` — whatever is easiest.

### B3. Build and install the driver

```
cd windows\driver
build-driver.bat
cd x64\Release
..\..\install-driver.bat
```

Reboot when it asks (it turns on test signing the first time), then run
`install-driver.bat` again.

Then check: **Settings → System → Sound → All sound devices**. You should see
**Smart Microphone**.

> This driver has never been compiled — there is no Windows compiler on the
> machine it was written on. Expect errors on the first build. `INSTALL-DRIVER.md`
> lists exactly which four logs to send me; with those I can fix it quickly.

### B4. Build and run the service on Windows

```
cmake -S . -B build -A x64
cmake --build build --config Release
build\apps\Release\smartmic-service.exe --port 47820
```

### B5. Pair the phone and use it

Same as A5, but pointed at the Windows machine's IP.

Then in Teams or Zoom, choose **Smart Microphone** as your microphone. Hold the
button on your phone and your voice goes into the meeting; let go and it is back
to the PC's own mic — with the meeting never noticing a device change.

---

# What to send me when something breaks

Logs beat descriptions every time.

| Where it broke | What to send |
|---|---|
| Mac build | The full output of `cmake --build build -j8` |
| Mac service | Everything it printed in Terminal |
| iPhone build | Xcode's error, from the red banner and the console pane |
| iPhone app crashes | `cd apps/mobile && flutter logs` while reproducing it |
| Pairing fails | Terminal output on the Mac **and** what the phone screen said |
| Windows driver build | The full output of `build-driver.bat` |
| Driver install | `%SystemRoot%\INF\setupapi.dev.log` (the bottom of it) |
| Windows blue screen | The stop code, and `C:\Windows\Minidump\*.dmp` |
