#pragma once

#include <cstdint>

class GfxRenderer;

namespace SleepClockRenderer {

struct DateTimeView {
  uint16_t year = 2026;
  uint8_t month = 6;
  uint8_t day = 2;
  uint8_t hour = 10;
  uint8_t minute = 24;
  uint8_t weekday = 2;
};

const char* styleName(uint8_t style);
uint8_t styleCount();
void render(GfxRenderer& renderer, uint8_t style, const DateTimeView& dateTime, bool available, int x, int y, int w,
            int h);

}  // namespace SleepClockRenderer
