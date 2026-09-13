# Getting SmartMic onto your iPhone

There is no `.ipa` file to download, and there cannot be one yet. Apple does not
allow installing an iPhone app from a file the way Android does. An app reaches
an iPhone in exactly three ways:

1. The App Store — needs a $99/year developer account and review.
2. TestFlight — same account, plus a review pass.
3. **Xcode on your Mac, with your iPhone plugged in** — free, works today.

Option 3 is what these steps do. A free Apple ID works; the app stays valid for
7 days, then you plug in and press Run again. With a paid account it lasts a
year.

---

## What you need

- Your Mac (you have it)
- Your iPhone and its cable
- Xcode — already installed (26.6)
- An Apple ID — your normal one is fine

## Step 1 — Trust the Mac from the phone

Plug the iPhone into the Mac. On the phone, tap **Trust** and enter your
passcode.

## Step 2 — Turn on Developer Mode on the iPhone

iPhone: **Settings → Privacy & Security → Developer Mode → On**. The phone
restarts and asks you to confirm after it boots.

(If you do not see Developer Mode, plug the phone in and open the project in
Xcode once — the entry appears after Xcode has talked to the phone.)

## Step 3 — Open the project

In Terminal on the Mac:

```bash
cd "<project folder>/apps/mobile"
open ios/Runner.xcworkspace
```

Open `Runner.xcworkspace`, **not** `Runner.xcodeproj`. The workspace is the one
that includes the SmartMic native library.

## Step 4 — Tell Xcode who you are

In Xcode:

1. Click **Runner** at the top of the left sidebar.
2. Select the **Runner** target → **Signing & Capabilities** tab.
3. Tick **Automatically manage signing**.
4. **Team** → *Add an Account…* → sign in with your Apple ID → pick your name
   (it will say "Personal Team").
5. **Bundle Identifier** — change `com.smartmic.smartmic` to something unique to
   you, for example `com.brijesh.smartmic`. A free account cannot reuse an
   identifier someone else has registered.

## Step 5 — Run it

1. At the top of the Xcode window, choose your iPhone from the device dropdown
   (next to the Run button).
2. Press the **▶ Run** button (or `Cmd+R`).
3. The first build takes a few minutes.

The app installs and launches. It will fail to open the first time with
"Untrusted Developer" — that is expected:

**iPhone: Settings → General → VPN & Device Management → your Apple ID → Trust.**

Then open SmartMic from the home screen.

## Step 6 — Permissions

The app asks for two things on first use. Both are required:

- **Microphone** — obviously.
- **Local Network** — iOS asks before an app may talk to other devices on your
  Wi-Fi. SmartMic connects directly to your computer and sends nothing to the
  internet.

If you tapped "Don't Allow" by accident:
**Settings → SmartMic → Local Network / Microphone → on.**

---

## Rebuilding later

Any time you want the app again after the 7 days expire, plug in the phone,
open the workspace, press Run. Nothing else changes.

## If something goes wrong

**"Failed to register bundle identifier"** — the identifier is taken. Change it
to something more unique in Step 4.

**"Unable to install… device is locked"** — unlock the phone and press Run again.

**The app builds but crashes immediately** — send me the output from Xcode's
console pane (the bottom half of the window), or run:

```bash
cd "<project folder>/apps/mobile" && flutter logs
```

**"Could not start the SmartMic engine"** — the phone could not reach the
computer. Both must be on the same Wi-Fi, and the computer's firewall must
allow UDP on port 47820.
