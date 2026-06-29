#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build/png_decoder_test"
BINARY="$BUILD_DIR/PngDecoderTest"

mkdir -p "$BUILD_DIR"

# On macOS the system clang++ may ship with a newer libc++ that requires Xcode
# command-line tools not present. Prefer Homebrew LLVM if available.
if command -v "$(brew --prefix 2>/dev/null)/opt/llvm/bin/clang++" &>/dev/null; then
  CXX="$(brew --prefix)/opt/llvm/bin/clang++"
else
  CXX="c++"
fi

SOURCES=(
  "$ROOT_DIR/test/png_decoder_test/PngDecoderTest.cpp"
  "$ROOT_DIR/lib/PngToBmpConverter/PngToBmpConverter.cpp"
  "$ROOT_DIR/lib/GfxRenderer/BitmapUtil.cpp"
  "$ROOT_DIR/lib/GfxRenderer/ImageToneDither.cpp"
  "$ROOT_DIR/lib/miniz/miniz.c"
)

CXXFLAGS=(
  -std=c++20
  -O2
  -Wall
  -Wextra
  # Stub headers must come first so they shadow Arduino.h, SdFat.h, etc.
  -I"$ROOT_DIR/test/png_decoder_test"
  -I"$ROOT_DIR/lib/PngToBmpConverter"
  -I"$ROOT_DIR/lib/GfxRenderer"
  -I"$ROOT_DIR/lib/hal"
  -I"$ROOT_DIR/lib/miniz"
  -I"$ROOT_DIR/open-x4-sdk/libs/hardware/BatteryMonitor/include"
  -I"$ROOT_DIR/open-x4-sdk/libs/hardware/InputManager/include"
  -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES=1
  -DCROSSPOINT_EMULATED=1
  -Wno-unused-parameter
  -Wno-unused-const-variable
)

"$CXX" "${CXXFLAGS[@]}" "${SOURCES[@]}" -o "$BINARY"

"$BINARY" "$@"
