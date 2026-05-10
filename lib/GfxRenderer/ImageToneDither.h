#pragma once

#include <cstdint>

struct ImageToneSample {
  uint8_t level = 3;
  uint8_t value = 255;
};

class FourToneImageDitherer {
 public:
  explicit FourToneImageDitherer(int width);
  ~FourToneImageDitherer();

  FourToneImageDitherer(const FourToneImageDitherer&) = delete;
  FourToneImageDitherer& operator=(const FourToneImageDitherer&) = delete;

  bool ok() const;
  ImageToneSample process(int gray, int x);
  void nextRow();
  void reset();

  static ImageToneSample quantize(int gray);
  static uint8_t levelFromValue(int value);
  static bool bwInkForLevel(uint8_t level, int x, int y);
  static bool bwPreviewInkForLevel(uint8_t level, int x, int y);

 private:
  int width_ = 0;
  int row_ = 0;
  int16_t* errorRows_[3][3] = {};
};
