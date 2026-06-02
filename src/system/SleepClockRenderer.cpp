#include "system/SleepClockRenderer.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cstdio>

#include "state/SystemSetting.h"
#include "system/Fonts.h"

namespace {

constexpr const char* WEEKDAYS[] = {"", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
constexpr const char* WEEKDAYS_SHORT[] = {"", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
constexpr const char* MONTHS[] = {"",    "January", "February", "March", "April", "May", "June",
                                  "July", "August",  "September", "October", "November", "December"};
constexpr const char* MONTHS_SHORT[] = {"",    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

constexpr int TIME_FONT = MONTSERRAT_CLOCK_70_FONT_ID;
constexpr int LARGE_SYSTEM_FONT = ATKINSON_HYPERLEGIBLE_18_FONT_ID;
constexpr int DATE_FONT = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
constexpr int LABEL_FONT = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
constexpr int LABEL_LARGE_FONT = ATKINSON_HYPERLEGIBLE_18_FONT_ID;
constexpr int TINY_FONT = ATKINSON_HYPERLEGIBLE_8_FONT_ID;
constexpr int CLOCK_VISIBLE_HEIGHT = 106;
constexpr int CLOCK_GLYPH_TOP = 104;

const char* weekdayName(uint8_t weekday) { return weekday >= 1 && weekday <= 7 ? WEEKDAYS[weekday] : ""; }
const char* weekdayShort(uint8_t weekday) { return weekday >= 1 && weekday <= 7 ? WEEKDAYS_SHORT[weekday] : ""; }
const char* monthName(uint8_t month) { return month >= 1 && month <= 12 ? MONTHS[month] : ""; }
const char* monthShort(uint8_t month) { return month >= 1 && month <= 12 ? MONTHS_SHORT[month] : ""; }

int textW(GfxRenderer& renderer, int fontId, const char* text, EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  return renderer.text.getWidth(fontId, text, style);
}

int clampInt(int value, int low, int high) {
  if (high < low) {
    return low;
  }
  return std::max(low, std::min(value, high));
}

uint8_t displayHour(const SleepClockRenderer::DateTimeView& dt) {
  if (SETTINGS.sleepClockTimeFormat == SystemSetting::CLOCK_24_HOUR) {
    return dt.hour;
  }
  const uint8_t h = dt.hour % 12;
  return h == 0 ? 12 : h;
}

const char* meridiem(const SleepClockRenderer::DateTimeView& dt) { return dt.hour >= 12 ? "PM" : "AM"; }

void formatTime(const SleepClockRenderer::DateTimeView& dt, char* out, size_t outSize) {
  std::snprintf(out, outSize, "%02u:%02u", displayHour(dt), dt.minute);
}

void formatDateComma(const SleepClockRenderer::DateTimeView& dt, char* out, size_t outSize) {
  std::snprintf(out, outSize, "%s, %02u %s", weekdayShort(dt.weekday), dt.day, monthShort(dt.month));
}

void formatDateFull(const SleepClockRenderer::DateTimeView& dt, char* out, size_t outSize) {
  std::snprintf(out, outSize, "%02u %s, %04u", dt.day, monthShort(dt.month), dt.year);
}

void centerText(GfxRenderer& renderer, int fontId, int x, int w, int y, const char* text, bool black = true,
                EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  if (w <= 0 || !text || *text == '\0') {
    return;
  }
  const int tx = x + (w - textW(renderer, fontId, text, style)) / 2;
  renderer.text.render(fontId, std::max(0, tx), std::max(0, y), text, black, style);
}

int clockRenderY(GfxRenderer& renderer, int visibleTop) {
  return std::max(0, visibleTop - (renderer.text.getFontAscenderSize(TIME_FONT) - CLOCK_GLYPH_TOP));
}

void centerClockLine(GfxRenderer& renderer, int x, int w, int visibleTop, const char* text) {
  centerText(renderer, TIME_FONT, x, w, clockRenderY(renderer, visibleTop), text, true, EpdFontFamily::BOLD);
}

void renderClockLine(GfxRenderer& renderer, int x, int visibleTop, const char* text) {
  renderer.text.render(TIME_FONT, std::max(0, x), clockRenderY(renderer, visibleTop), text, true, EpdFontFamily::BOLD);
}

void renderClockSegment(GfxRenderer& renderer, int x, int visibleTop, const char* text, const bool black,
                        EpdFontFamily::Style style) {
  renderer.text.render(TIME_FONT, std::max(0, x), clockRenderY(renderer, visibleTop), text, black, style);
}

void renderUnavailable(GfxRenderer& renderer, int x, int y, int w, int h) {
  const int centerY = y + h / 2;
  centerText(renderer, DATE_FONT, x, w, centerY - 28, "Clock unavailable", true, EpdFontFamily::BOLD);
  centerText(renderer, TINY_FONT, x, w, centerY + 8, "Sync time in settings");
}

void renderCenteredDate(GfxRenderer& renderer, const SleepClockRenderer::DateTimeView& dt, int x, int y, int w,
                        int h);

void renderStackedCity(GfxRenderer& renderer, const SleepClockRenderer::DateTimeView& dt, int x, int y, int w, int h) {
  char hourText[4];
  char minuteText[4];
  char dateText[16];
  std::snprintf(hourText, sizeof(hourText), "%02u", displayHour(dt));
  std::snprintf(minuteText, sizeof(minuteText), "%02u", dt.minute);
  std::snprintf(dateText, sizeof(dateText), "%02u %s", dt.day, monthShort(dt.month));

  const int left = x + 20;
  const int top = y + (h - (CLOCK_VISIBLE_HEIGHT * 2 - 10)) / 2;
  renderClockLine(renderer, left, top, hourText);
  renderClockLine(renderer, left + 10, top + CLOCK_VISIBLE_HEIGHT + 10, minuteText);

  const int infoX = left + textW(renderer, TIME_FONT, "88", EpdFontFamily::BOLD) + 16;
  renderer.text.render(LARGE_SYSTEM_FONT, infoX, std::max(0, top + 18), weekdayShort(dt.weekday), true,
                       EpdFontFamily::BOLD);
  renderer.text.render(LARGE_SYSTEM_FONT, infoX, std::max(0, top + 58), dateText, true);
  if (SETTINGS.sleepClockTimeFormat == SystemSetting::CLOCK_12_HOUR) {
    renderer.text.render(LABEL_LARGE_FONT, infoX, std::max(0, top + 120), meridiem(dt), true, EpdFontFamily::BOLD);
  }
}

void renderCenteredDate(GfxRenderer& renderer, const SleepClockRenderer::DateTimeView& dt, int x, int y, int w, int h) {
  char timeText[8];
  char hourText[4];
  char minuteText[4];
  char dateText[24];
  formatTime(dt, timeText, sizeof(timeText));
  std::snprintf(hourText, sizeof(hourText), "%02u", displayHour(dt));
  std::snprintf(minuteText, sizeof(minuteText), "%02u", dt.minute);
  formatDateFull(dt, dateText, sizeof(dateText));

  const int timeW = textW(renderer, TIME_FONT, timeText, EpdFontFamily::BOLD);
  if (timeW <= w - 24) {
    const int timeTop = y + (h - CLOCK_VISIBLE_HEIGHT) / 2 - 18;
    renderClockLine(renderer, x + (w - timeW) / 2 - 20, timeTop, timeText);
    if (SETTINGS.sleepClockTimeFormat == SystemSetting::CLOCK_12_HOUR) {
      renderer.text.render(LABEL_FONT, x + (w + timeW) / 2 - 10, timeTop + 24, meridiem(dt), true, EpdFontFamily::BOLD);
    }
    centerText(renderer, DATE_FONT, x, w, timeTop + CLOCK_VISIBLE_HEIGHT + 20, dateText, true);
    return;
  }

  const int gap = 6;
  const int stackTop = y + (h - (CLOCK_VISIBLE_HEIGHT * 2 + gap)) / 2 - 10;
  centerClockLine(renderer, x, w, stackTop, hourText);
  centerClockLine(renderer, x, w, stackTop + CLOCK_VISIBLE_HEIGHT + gap, minuteText);
  if (SETTINGS.sleepClockTimeFormat == SystemSetting::CLOCK_12_HOUR) {
    centerText(renderer, LABEL_FONT, x, w, stackTop + CLOCK_VISIBLE_HEIGHT * 2 + gap + 6, meridiem(dt), true,
               EpdFontFamily::BOLD);
  }
  centerText(renderer, DATE_FONT, x, w, y + h - 42, dateText, true);
}

void renderHorizontalCard(GfxRenderer& renderer, const SleepClockRenderer::DateTimeView& dt, int x, int y, int w, int h) {
  if (w < 80 || h < 90) {
    renderCenteredDate(renderer, dt, x, y, w, h);
    return;
  }

  char dayText[4];
  char monthText[8];
  char hourText[4];
  char minuteText[4];
  std::snprintf(dayText, sizeof(dayText), "%02u", dt.day);
  std::snprintf(monthText, sizeof(monthText), "%s", monthShort(dt.month));
  std::snprintf(hourText, sizeof(hourText), "%02u", displayHour(dt));
  std::snprintf(minuteText, sizeof(minuteText), "%02u", dt.minute);

  const bool largeScreen = w >= 460 && h >= 300;
  const int cardW = largeScreen ? std::min(w - 36, 560) : w;
  const int cardH = largeScreen ? 188 : std::max(1, h > 220 ? 170 : h);
  const int cardX = largeScreen ? x + (w - cardW) / 2 : x;
  const int cardY = y + (h - cardH) / 2;
  const int dateW = largeScreen ? clampInt(cardW * 25 / 100, 104, cardW - 220)
                                : clampInt(cardW * 28 / 100, 44, cardW - 72);

  renderer.rectangle.fill(cardX, cardY, dateW, cardH, true);

  const int dateLineHeight = renderer.text.getLineHeight(LARGE_SYSTEM_FONT);
  const int dayY = cardY + (cardH - (dateLineHeight * 2 + 8)) / 2;
  const int monthY = dayY + dateLineHeight + 8;
  centerText(renderer, LARGE_SYSTEM_FONT, cardX, dateW, dayY, dayText, false, EpdFontFamily::BOLD);
  centerText(renderer, LARGE_SYSTEM_FONT, cardX, dateW, monthY, monthText, false);

  const int timeAreaX = cardX + dateW;
  const int timeAreaW = cardW - dateW;
  const int hourW = textW(renderer, TIME_FONT, hourText, EpdFontFamily::BOLD);
  const int colonW = textW(renderer, TIME_FONT, ":", EpdFontFamily::REGULAR);
  const int minuteW = textW(renderer, TIME_FONT, minuteText, EpdFontFamily::REGULAR);
  const int timeW = hourW + colonW + minuteW;
  const int timeX = timeAreaX + std::max(6, (timeAreaW - timeW) / 2);
  const int timeTop = cardY + (cardH - CLOCK_VISIBLE_HEIGHT) / 2;

  if (timeAreaW > 0 && timeW <= timeAreaW - 12) {
    renderClockSegment(renderer, timeX, timeTop, hourText, true, EpdFontFamily::BOLD);
    renderClockSegment(renderer, timeX + hourW, timeTop, ":", true, EpdFontFamily::REGULAR);
    renderClockSegment(renderer, timeX + hourW + colonW, timeTop, minuteText, true, EpdFontFamily::REGULAR);
  } else if (timeAreaW > 48) {
    const int halfW = std::max(1, timeAreaW / 2);
    const int hourX = timeAreaX + std::max(2, (halfW - hourW) / 2);
    const int minuteX = timeAreaX + halfW + std::max(2, (timeAreaW - halfW - minuteW) / 2);
    renderClockSegment(renderer, hourX, timeTop, hourText, true, EpdFontFamily::BOLD);
    renderClockSegment(renderer, minuteX, timeTop, minuteText, true, EpdFontFamily::REGULAR);
    renderer.text.render(DATE_FONT, timeAreaX + halfW - 2, std::max(0, timeTop + 42), ":", true);
  }
  if (SETTINGS.sleepClockTimeFormat == SystemSetting::CLOCK_12_HOUR) {
    renderer.text.render(TINY_FONT, timeAreaX + 18, std::max(0, cardY + cardH - 32), meridiem(dt), true,
                         EpdFontFamily::BOLD);
  }
}

}  // namespace

namespace SleepClockRenderer {

const char* styleName(uint8_t style) {
  switch (style) {
    case SystemSetting::CLOCK_STACKED_CITY:
      return "Stacked";
    case SystemSetting::CLOCK_HORIZONTAL_CARD:
      return "Card";
    case SystemSetting::CLOCK_CENTERED_DATE:
    default:
      return "Centered";
  }
}

uint8_t styleCount() { return SystemSetting::SLEEP_CLOCK_STYLE_COUNT; }

void render(GfxRenderer& renderer, uint8_t style, const DateTimeView& dateTime, bool available, int x, int y, int w,
            int h) {
  if (!available) {
    renderUnavailable(renderer, x, y, w, h);
    return;
  }

  switch (style) {
    case SystemSetting::CLOCK_STACKED_CITY:
      renderStackedCity(renderer, dateTime, x, y, w, h);
      break;
    case SystemSetting::CLOCK_HORIZONTAL_CARD:
      renderHorizontalCard(renderer, dateTime, x, y, w, h);
      break;
    case SystemSetting::CLOCK_CENTERED_DATE:
    default:
      renderCenteredDate(renderer, dateTime, x, y, w, h);
      break;
  }
}

}  // namespace SleepClockRenderer
