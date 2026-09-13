# What is in this folder

| File | What it is |
|---|---|
| `START-HERE.md` | **Read this first.** Which file goes where, and the two paths forward. |
| `INSTALL-IPHONE.md` | Getting the app onto your iPhone via Xcode. |
| `SmartMic-0.3.0-arm64.apk` | The Android app. Not useful to you — you have an iPhone. Ignore it. |
| `INSTALL-ANDROID.md` | Android instructions. Ignore. |
| `INSTALL-DRIVER.md` | How to build and install the Windows driver. **Read the warning at the top.** |
| `EV-CERTIFICATE-CHECKLIST.md` | Exactly what I need from you once the EV certificate arrives. |

## The honest status of each piece

**The iPhone app** — the native core is built for iOS (arm64, 814 KB static
library, all entry points exported, linked against iOS builds of libsodium and
libopus that I compiled here). The Flutter project is wired up to it through
CocoaPods. What has **not** happened is a completed end-to-end
`flutter build ios`: macOS's Gatekeeper daemon wedged partway through (see
`START-HERE.md`), and every build tool that needs to launch a freshly compiled
helper hangs until it is cleared. Clear it, then Xcode's Run button is the next
thing to try.


**The Android app** was built here and the APK is real. The native core inside
it is the same C++ that passes 72 tests on this machine, including a full
end-to-end run of pairing, Opus, encryption and the PTT state machine. What has
*not* been verified is the app running on an actual phone — I have no phone
here. The first run may surface something.

**The Windows driver** has never been compiled. There is no Windows kernel
compiler on the machine it was written on. It is complete, careful code with
the risky parts kept deliberately small, but expect compiler errors on the
first build and send me the output.

**The Windows service** builds and runs here on macOS, and the same code targets
Windows through a platform layer (WASAPI) that has also never been compiled.

Nothing in this folder is signed for distribution to other people. That is what
the EV certificate is for.
