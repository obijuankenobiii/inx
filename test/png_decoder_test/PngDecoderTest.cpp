// Native unit tests for PngToBmpConverter IHDR validation.
// Compiled with c++ via test/run_png_decoder_test.sh — no PlatformIO required.

#include "arduino_compat.h"
HardwareSerialStub Serial;

// FsFile and Print are defined in the stub headers (SdFat.h, Print.h).
// They must be visible before PngToBmpConverter.h is included.
#include <cstdio>
#include <vector>

#include "PngToBmpConverter.h"
#include "Print.h"
#include "SdFat.h"

// ---- test harness ----

static int gPassed = 0;
static int gFailed = 0;

static void check(const char* name, bool condition) {
  if (condition) {
    fprintf(stdout, "PASS: %s\n", name);
    gPassed++;
  } else {
    fprintf(stdout, "FAIL: %s\n", name);
    gFailed++;
  }
}

// Build a minimal in-memory PNG: valid signature + IHDR chunk, no IDAT/IEND.
// The decoder rejects bad IHDR fields early; for valid IHDR it fails when
// it cannot find an IDAT chunk. Either way returns false without crashing.
static std::vector<uint8_t> makePng(uint32_t width, uint32_t height, uint8_t bitDepth, uint8_t colorType,
                                    uint8_t compression = 0, uint8_t filter = 0, uint8_t interlace = 0) {
  std::vector<uint8_t> png;
  const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  png.insert(png.end(), sig, sig + 8);
  // IHDR chunk length = 13
  png.push_back(0);
  png.push_back(0);
  png.push_back(0);
  png.push_back(13);
  png.push_back('I');
  png.push_back('H');
  png.push_back('D');
  png.push_back('R');
  png.push_back((width >> 24) & 0xFF);
  png.push_back((width >> 16) & 0xFF);
  png.push_back((width >> 8) & 0xFF);
  png.push_back(width & 0xFF);
  png.push_back((height >> 24) & 0xFF);
  png.push_back((height >> 16) & 0xFF);
  png.push_back((height >> 8) & 0xFF);
  png.push_back(height & 0xFF);
  png.push_back(bitDepth);
  png.push_back(colorType);
  png.push_back(compression);
  png.push_back(filter);
  png.push_back(interlace);
  // CRC placeholder (4 bytes, not validated by the decoder)
  png.push_back(0);
  png.push_back(0);
  png.push_back(0);
  png.push_back(0);
  return png;
}

static bool convert(const std::vector<uint8_t>& data) {
  FsFile f(data.data(), data.size());
  Print out;
  return PngToBmpConverter::pngFileTo1BitBmpStream(f, out);
}

int main() {
  // Dimension checks (pre-existing validation)
  check("reject_width_over_limit", !convert(makePng(2049, 100, 8, 0)));
  check("reject_height_over_limit", !convert(makePng(100, 3073, 8, 0)));
  check("reject_zero_width", !convert(makePng(0, 100, 8, 0)));
  check("reject_zero_height", !convert(makePng(100, 0, 8, 0)));

  // Invalid bitDepth/colorType combos — caught by new IHDR validation
  check("reject_rgb_bitdepth4", !convert(makePng(10, 10, 4, 2)));         // RGB allows only 8/16
  check("reject_rgba_bitdepth1", !convert(makePng(10, 10, 1, 6)));        // RGBA allows only 8/16
  check("reject_gray_alpha_bitdepth4", !convert(makePng(10, 10, 4, 4)));  // GrayAlpha allows only 8/16
  check("reject_palette_bitdepth16", !convert(makePng(10, 10, 16, 3)));   // Palette allows only 1/2/4/8

  // Valid IHDR combos — should fail at IDAT (truncated data), not at IHDR validation
  check("valid_gray8_no_idat_returns_false", !convert(makePng(10, 10, 8, 0)));
  check("valid_rgb8_no_idat_returns_false", !convert(makePng(10, 10, 8, 2)));
  check("valid_palette4_no_idat_returns_false", !convert(makePng(10, 10, 4, 3)));
  check("valid_gray16_no_idat_returns_false", !convert(makePng(10, 10, 16, 0)));

  // Other structural rejections
  check("reject_interlaced", !convert(makePng(10, 10, 8, 0, 0, 0, 1)));

  // Bad / empty input
  {
    const uint8_t bad[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    FsFile f(bad, 8);
    Print out;
    check("reject_bad_signature", !PngToBmpConverter::pngFileTo1BitBmpStream(f, out));
  }
  {
    FsFile f;
    Print out;
    check("reject_empty_file", !PngToBmpConverter::pngFileTo1BitBmpStream(f, out));
  }

  fprintf(stdout, "\n%d passed, %d failed\n", gPassed, gFailed);
  return (gFailed == 0) ? 0 : 1;
}
