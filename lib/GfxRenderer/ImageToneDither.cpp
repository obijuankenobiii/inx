#include "ImageToneDither.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace {
int clamp255(const int v) {
  return std::max(0, std::min(255, v));
}

int perceptualTone(const int gray) {
  if (gray < 24) {
    return clamp255(gray);
  }
  return clamp255(gray + 6);
}

}  // namespace

FourToneImageDitherer::FourToneImageDitherer(const int width) : width_(width) {
  if (width_ <= 0) {
    width_ = 0;
    return;
  }
  for (int plane = 0; plane < 3; plane++) {
    for (int row = 0; row < 3; row++) {
      errorRows_[plane][row] = static_cast<int16_t*>(std::calloc(static_cast<size_t>(width_) + 4u, sizeof(int16_t)));
    }
  }
  if (!ok()) {
    for (int plane = 0; plane < 3; plane++) {
      for (int row = 0; row < 3; row++) {
        std::free(errorRows_[plane][row]);
        errorRows_[plane][row] = nullptr;
      }
    }
    width_ = 0;
  }
}

FourToneImageDitherer::~FourToneImageDitherer() {
  for (int plane = 0; plane < 3; plane++) {
    for (int row = 0; row < 3; row++) {
      std::free(errorRows_[plane][row]);
    }
  }
}

bool FourToneImageDitherer::ok() const {
  if (width_ <= 0) return false;
  for (int plane = 0; plane < 3; plane++) {
    for (int row = 0; row < 3; row++) {
      if (!errorRows_[plane][row]) return false;
    }
  }
  return true;
}

ImageToneSample FourToneImageDitherer::quantize(const int gray) {
  const int g = clamp255(gray);
  ImageToneSample sample;
  if (g < 43) {
    sample.level = 3;
    sample.value = 0;
  } else if (g < 128) {
    sample.level = 1;
    sample.value = 85;
  } else if (g < 245) {
    sample.level = 2;
    sample.value = 170;
  } else {
    sample.level = 0;
    sample.value = 255;
  }
  return sample;
}

uint8_t FourToneImageDitherer::levelFromValue(const int value) {
  if (value <= 42) return 3;
  if (value <= 144) return 1;
  if (value <= 229) return 2;
  return 0;
}

bool FourToneImageDitherer::bwInkForLevel(const uint8_t level, const int x, const int y) {
  if (level == 3) return true;
  if (level == 1) return ((x + y) & 1) == 0;
  if (level == 2) return ((x & 1) == 0) && ((y & 1) == 0);
  return false;
}

bool FourToneImageDitherer::bwPreviewInkForLevel(const uint8_t level, const int x, const int y) {
  (void)x;
  (void)y;
  return level > 0;
}

ImageToneSample FourToneImageDitherer::process(const int gray, const int x) {
  if (!ok() || x < 0 || x >= width_) {
    return quantize(perceptualTone(gray));
  }

  const int adjusted = clamp255(perceptualTone(gray) + errorRows_[0][0][x + 2]);
  const ImageToneSample out = quantize(adjusted);
  const int spread = (adjusted - static_cast<int>(out.value)) >> 3;

  if (x + 1 < width_) errorRows_[0][0][x + 3] += static_cast<int16_t>(spread);
  if (x + 2 < width_) errorRows_[0][0][x + 4] += static_cast<int16_t>(spread);
  if (x > 0) errorRows_[0][1][x + 1] += static_cast<int16_t>(spread);
  errorRows_[0][1][x + 2] += static_cast<int16_t>(spread);
  if (x + 1 < width_) errorRows_[0][1][x + 3] += static_cast<int16_t>(spread);
  errorRows_[0][2][x + 2] += static_cast<int16_t>(spread);

  return out;
}

void FourToneImageDitherer::nextRow() {
  for (int plane = 0; plane < 3; plane++) {
    int16_t* temp = errorRows_[plane][0];
    errorRows_[plane][0] = errorRows_[plane][1];
    errorRows_[plane][1] = errorRows_[plane][2];
    errorRows_[plane][2] = temp;
    if (errorRows_[plane][2]) {
      std::memset(errorRows_[plane][2], 0, (static_cast<size_t>(width_) + 4u) * sizeof(int16_t));
    }
  }
  row_++;
}

void FourToneImageDitherer::reset() {
  for (int plane = 0; plane < 3; plane++) {
    for (int row = 0; row < 3; row++) {
      if (errorRows_[plane][row]) {
        std::memset(errorRows_[plane][row], 0, (static_cast<size_t>(width_) + 4u) * sizeof(int16_t));
      }
    }
  }
  row_ = 0;
}
