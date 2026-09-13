#!/usr/bin/env bash
# Rebuilds the vendored iOS static library. Run from the repository root:
#   bash apps/phone-core/ios/build-ios-lib.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="$ROOT/apps/mobile/ios/smartmic_core/lib"
mkdir -p "$OUT"

build() {
  local sdk="$1" name="$2" extra="$3"
  cmake -S "$ROOT/apps/phone-core/ios" -B "/tmp/smartmic-ios-$name" -G Xcode \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 \
    $extra > /dev/null
  cmake --build "/tmp/smartmic-ios-$name" --config Release -- -sdk "$sdk" > /dev/null
  find "/tmp/smartmic-ios-$name" -name "libsmartmic_phone.a" -exec cp {} "$OUT/libsmartmic_phone-$name.a" \;
}

build iphoneos device ""
build iphonesimulator sim "-DCMAKE_OSX_SYSROOT=iphonesimulator"

# One archive covering device and simulator, so Xcode is happy either way.
lipo -create "$OUT/libsmartmic_phone-device.a" "$OUT/libsmartmic_phone-sim.a" \
     -output "$OUT/libsmartmic_phone.a" 2>/dev/null \
  || cp "$OUT/libsmartmic_phone-device.a" "$OUT/libsmartmic_phone.a"

echo "wrote $OUT/libsmartmic_phone.a"
lipo -info "$OUT/libsmartmic_phone.a"
