# Prebuilt native dependencies for Android

Built from source against NDK r27, `arm64-v8a`, `android-26`:

| Library | Version | How |
|---|---|---|
| libsodium | 1.0.20 | `LIBSODIUM_FULL_BUILD=1 ./dist-build/android-armv8-a.sh` |
| libopus | 1.5.2 | CMake + `android.toolchain.cmake` |

`LIBSODIUM_FULL_BUILD=1` is not optional: the default Android build is
`--enable-minimal`, which omits ristretto255, and CPace pairing needs it
(ADR-012). If pairing ever fails to link, that flag is the first thing to check.

libsodium ships as a shared object rather than a static archive because macOS's
host `ranlib` empties the `.a` during the autotools build (it cannot read
AArch64 ELF objects). The `.so` is a normal AArch64 ELF and is bundled into the
APK under `jniLibs`.

To add another ABI, run the same two builds with `ANDROID_ABI=armeabi-v7a` /
`x86_64` and drop the results into a sibling folder. Only `arm64-v8a` is built
today, which covers essentially every phone from 2017 onward.
