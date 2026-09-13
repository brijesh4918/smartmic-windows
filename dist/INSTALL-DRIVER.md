# Installing the SmartMic driver

Read this once before starting. It takes about 40 minutes, most of which is
downloading Visual Studio.

> **Please read this first.** This driver has never been compiled. It was
> written on a Mac, where no Windows kernel compiler exists. The first build
> will almost certainly produce compiler errors, and possibly a crash on first
> load. That is normal for bring-up and it is fixable quickly — but do not run
> it on a machine you cannot afford to reboot, and do not run it on a machine
> holding work you have not saved.
>
> **Use a virtual machine if you can.** A Windows 11 VM (Parallels, VMware,
> VirtualBox, or Hyper-V on another PC) is the right place for this. A kernel
> driver bug bluescreens the whole machine; in a VM that costs you a restart
> of the VM and nothing else.

---

## Step 0 — What you need

| | |
|---|---|
| A Windows 11 machine or VM | x64 or ARM64, version 1809 or newer |
| Visual Studio 2022 | Community edition is fine and free |
| Windows Driver Kit (WDK) | Must match your Visual Studio and SDK version |
| About 15 GB of disk | Mostly Visual Studio |

You do **not** need the EV certificate yet. That is only for shipping to other
people; for your own machine a test certificate works and the scripts make one
for you.

## Step 1 — Install the tools

1. Install **Visual Studio 2022 Community** from
   `https://visualstudio.microsoft.com/downloads/`.
   In the installer, tick **Desktop development with C++**.
2. Install the **Windows 11 SDK** (the VS installer offers it under
   "Individual components" — take the latest).
3. Install the **WDK** from
   `https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk`.
   Let it install the Visual Studio extension when it offers to. **The WDK
   version must match the SDK version** — this is the single most common cause
   of a failed driver build.

Restart Windows after the WDK install.

## Step 2 — Get the code onto the Windows machine

Copy the whole project folder across (USB, network share, or `git clone`).
You need at least the `windows\driver` folder.

## Step 3 — Build

1. Open the Start menu and find **"x64 Native Tools Command Prompt for VS 2022"**.
2. **Right-click it → Run as administrator.** (This matters; the certificate
   steps need it.)
3. Navigate to the driver folder and run the build script:

```
cd C:\path\to\Tap To Speak\windows\driver
build-driver.bat
```

The script compiles the driver, creates a test certificate, builds the catalog
and signs everything. If it fails, jump to **If it does not work** below and
send me the output — that is exactly the log I need.

When it succeeds you will have three files in `x64\Release`:

```
smartmic.sys      the driver itself
smartmic.inf      tells Windows how to install it
smartmic.cat      the signature
```

## Step 4 — Install

1. In the **same administrator command prompt**, go to the output folder and run:

```
cd x64\Release
..\..\install-driver.bat
```

2. The first time, it will turn on **test signing** and tell you to reboot.
   Test signing lets Windows load a driver signed with our own test
   certificate instead of a Microsoft-issued one. **Reboot when it asks.**
3. After the reboot, run `install-driver.bat` again.

> Test signing is a development setting. It slightly weakens a Windows security
> boundary, and you will see a "Test Mode" watermark in the corner of the
> desktop. To turn it back off later:
> `bcdedit /set testsigning off` (as administrator), then reboot.

## Step 5 — Check it worked

Open **Settings → System → Sound → All sound devices**. You should see:

```
Smart Microphone
```

If it is there, the driver loaded and the endpoint is published. It will be
silent until the SmartMic service is running and feeding it — that is expected.

Also check **Device Manager → Sound, video and game controllers**; you should
see **SmartMic Virtual Audio Device** with no warning triangle.

## Step 6 — Run the service against it

Build the user-mode service (from the project root, in the same VS command
prompt):

```
cmake -S . -B build -A x64
cmake --build build --config Release
build\apps\Release\smartmic-service.exe --port 47820
```

It will print a six-digit pairing code and a QR link. Point the phone app at it.

---

## If it does not work

Send me whichever of these applies — each one tells me something different.

**The build failed.** Copy the whole output of `build-driver.bat`. The first
error is the one that matters; the rest are usually knock-on effects.

**The driver did not install.** Get the setup log:

```
notepad %SystemRoot%\INF\setupapi.dev.log
```

Scroll to the bottom — the last install attempt is what I need.

**"Smart Microphone" does not appear in Sound settings.** Check Device Manager
for a yellow warning triangle on **SmartMic Virtual Audio Device**, then
double-click it and send me the text under **Device status**. Code 52 means
signing; code 39 or 31 usually means the driver failed to load.

**Windows bluechecked / bluescreened.** Send me the stop code from the blue
screen (for example `IRQL_NOT_LESS_OR_EQUAL`) and, if you can, the crash dump
from `C:\Windows\Minidump\`. This is the most useful log of all — it names the
exact line.

**The endpoint appears but records silence.** That is likely the service, not
the driver. Run `smartmic-service.exe` and send me its console output, plus:

```
pnputil /enum-drivers | findstr /i smartmic
```

### Getting the driver's own trace output

The driver prints a running commentary that is invisible unless you ask for it.

1. Download **DebugView** from
   `https://learn.microsoft.com/sysinternals/downloads/debugview`.
2. Run it **as administrator**.
3. In the menu: **Capture → Capture Kernel**, and **Capture → Enable Verbose
   Kernel Output**.
4. Reproduce the problem. Lines beginning `SmartMic:` are ours.

That trace plus the setupapi log is usually enough for me to find the bug
without guessing.

---

## Removing it

```
uninstall-driver.bat
```

as administrator. Then, if you want Windows back to normal:

```
bcdedit /set testsigning off
```

and reboot.

---

## What happens when the EV certificate arrives

Nothing in the code changes. What changes is Step 3: instead of `makecert` and
a self-signed certificate, the `.cat` is submitted to Microsoft through the
Hardware Developer Center and comes back signed by Microsoft. Then no test
signing is needed, no watermark appears, and it installs on anyone's machine.
See `docs/release/EV-CERTIFICATE-CHECKLIST.md`.
