# Building the SmartMic app for your iPhone

There is no file I can send you for this. Apple does not allow installing an
iPhone app from a download the way Android does — it has to come from the App
Store, from TestFlight, or from Xcode on your own Mac with the phone plugged
in. The third one is free and takes a few minutes.

## One-time setup on the Mac

**1. CocoaPods** — Flutter's iOS build needs it:

```bash
brew install cocoapods
```

**2. Clear Gatekeeper if it is stuck.** If a build ever hangs with no output at
all, check this first:

```bash
ps aux | grep syspolicyd
```

If it is near 100% CPU, no newly compiled program on the machine can start,
including the build's own helper tools:

```bash
sudo killall -9 syspolicyd
```

It restarts itself immediately.

## Every time you want the latest code

```bash
cd <project folder>
git pull
cd apps/mobile
flutter pub get
open ios/Runner.xcworkspace
```

Open `Runner.xcworkspace`, **not** `Runner.xcodeproj` — the workspace is the one
that includes the SmartMic native library.

In Xcode:

1. **Runner** in the sidebar → **Runner** target → **Signing & Capabilities**
2. Tick **Automatically manage signing**, pick your Apple ID under **Team**
   (a free account works — the app then lasts 7 days before you re-run it)
3. Change the **Bundle Identifier** to something unique to you, e.g.
   `com.brijesh.smartmic`
4. Pick your iPhone in the device dropdown, press **▶ Run**

First launch will say "Untrusted Developer". Fix it on the phone at
**Settings → General → VPN & Device Management → your Apple ID → Trust**.

## What is already done for you

The native core is prebuilt and committed — `libsmartmic_phone.a`,
`libsodium.a` and `libopus.a` under `apps/mobile/ios/smartmic_core/lib/`. You
do not need CMake, autotools or the NDK. Xcode links them directly.

If you change the C++ under `apps/phone-core/` or `shared/`, rebuild that
static library before running Xcode again:

```bash
bash apps/phone-core/ios/build-ios-lib.sh
```

## Permissions to grant on first run

- **Microphone** — the whole point.
- **Local Network** — iOS blocks an app from reaching other devices on your
  Wi-Fi until you allow it, and the prompt is easy to dismiss by accident. If
  pairing hangs, check **Settings → SmartMic → Local Network** first. If
  SmartMic is not listed there at all, the app has never attempted a local
  connection, which is itself a useful clue.

## Reading the pairing screen

While pairing has not completed, the app shows one line about its own network
counters. It is the fastest way to tell what is wrong:

| It says | Meaning |
|---|---|
| `not sending yet` | the app never got as far as transmitting |
| `N sends refused by the phone` | iOS is rejecting the send — VPN routing or Local Network permission |
| `sent N, nothing back` | the phone is transmitting and the packets are being lost between here and the PC |
| `sent N, received M` | two-way traffic; this is pairing, not the network |
