# Prebuilt native dependencies for iOS

`arm64`, device only (`iphoneos`), deployment target 13.0.

| Library | Version | How |
|---|---|---|
| libsodium | 1.0.20 | autotools cross-compile, `LIBSODIUM_FULL_BUILD=1` |
| libopus | 1.5.2 | CMake, `Unix Makefiles` generator |

Two traps worth writing down, because both cost real time:

1. **Build each target from a freshly extracted source tree.** Reusing a tree
   that was configured for another platform leaves object files behind that
   `make` considers up to date, and you get an archive full of the wrong
   architecture with no error anywhere.
2. **libtool's install step runs a `ranlib` that empties the static archive.**
   The archive here is assembled directly from the objects afterwards. If
   `libsodium.a` is ever 96 bytes, that is what happened.

`LIBSODIUM_FULL_BUILD=1` is required for ristretto255, which CPace pairing needs
(ADR-012).

The simulator slice is not built. Running in the Simulator would need an
`iphonesimulator` build of both libraries and a `lipo`/xcframework merge; on a
device — which is where a microphone app has to be tested anyway — this is
enough.
