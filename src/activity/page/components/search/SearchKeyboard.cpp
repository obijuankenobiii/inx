#include "SearchKeyboard.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "images/Delete.h"
#include "images/Enter.h"
#include "images/Shift.h"
#include "images/Space.h"
#include "system/Fonts.h"

namespace {

constexpr int kMargin = 6;
constexpr int kGap = 6;
constexpr int kRows = 4;
constexpr int kColumns = 10;
constexpr size_t kMaxQueryLength = 64;

constexpr const char* kLettersTop = "qwertyuiop";
constexpr const char* kLettersMiddle = "asdfghjkl";
constexpr const char* kLettersBottom = "zxcvbnm";
constexpr const char* kSymbolsTop = "1234567890";
constexpr const char* kSymbolsMiddle = "-/:;()$&@\"";
constexpr const char* kSymbolsBottom = ".,?!'";

struct Geometry {
  int left = 0;
  int top = 0;
  int width = 0;
  int keyWidth = 0;
  int keyHeight = 0;
};

Geometry geometry(const GfxRenderer& renderer, const int top, const int bottom) {
  Geometry value;
  value.left = kMargin;
  value.width = renderer.getScreenWidth() - kMargin * 2;
  value.keyWidth = std::max(1, (value.width - kGap * (kColumns - 1)) / kColumns);
  value.keyHeight = value.keyWidth + 12;
  const int keyboardHeight = value.keyHeight * kRows + kGap * (kRows - 1);
  value.top = std::max(top, bottom - keyboardHeight);
  return value;
}

int rowY(const Geometry& value, const int row) { return value.top + row * (value.keyHeight + kGap); }

int rowCount(const int row, const int mode) {
  if (row == 0) return 10;
  if (row == 1) return mode == 0 ? 9 : 10;
  if (row == 2) return mode == 0 ? 9 : 7;
  return mode == 0 ? 5 : 4;
}

const char* rowKeys(const int row, const int mode) {
  if (row == 0) return mode == 0 ? kLettersTop : kSymbolsTop;
  if (row == 1) return mode == 0 ? kLettersMiddle : kSymbolsMiddle;
  return mode == 0 ? kLettersBottom : kSymbolsBottom;
}

char displayCharacter(const bool caps, const char value) {
  return caps ? static_cast<char>(std::toupper(static_cast<unsigned char>(value))) : value;
}

int rowWidth(const int keyWidth, const int count) { return keyWidth * count + kGap * (count - 1); }

void drawKey(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
             const char* label, const bool selected) {
  renderer.rectangle.fill(x, y, width, height, selected, false);
  renderer.rectangle.render(x, y, width, height, true);
  if (!label || label[0] == '\0') return;

  constexpr int font = MONTSERRAT_16_FONT_ID;
  const int textWidth = renderer.text.getWidth(font, label);
  const int textY = y + (height - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, x + (width - textWidth) / 2, textY, label, !selected);
}

void drawCharacterRow(const GfxRenderer& renderer, const Geometry& layout, const int row, const char* keys,
                      const bool caps, const int selectedColumn) {
  const int count = static_cast<int>(std::strlen(keys));
  const int width = rowWidth(layout.keyWidth, count);
  int x = (renderer.getScreenWidth() - width) / 2;
  for (int index = 0; index < count; ++index) {
    char label[2] = {displayCharacter(caps, keys[index]), '\0'};
    drawKey(renderer, x, rowY(layout, row), layout.keyWidth, layout.keyHeight, label, index == selectedColumn);
    x += layout.keyWidth + kGap;
  }
}

void drawActionKey(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                  const char* label, const bool selected) {
  drawKey(renderer, x, y, width, height, label, selected);
}

void drawDelete(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                const bool selected) {
  drawKey(renderer, x, y, width, height, "", selected);
  constexpr int size = 34;
  renderer.bitmap.iconScaled(Delete, x + (width - size) / 2, y + (height - size) / 2, 30, 30, size, size,
                             BitmapRender::Orientation::None, selected);
}

void drawShift(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
               const bool selected) {
  drawKey(renderer, x, y, width, height, "", selected);
  constexpr int size = 30;
  renderer.bitmap.icon(Shift, x + (width - size) / 2, y + (height - size) / 2, size, size,
                       BitmapRender::Orientation::None, selected);
}

void drawSpace(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
               const bool selected) {
  drawKey(renderer, x, y, width, height, "", selected);
  constexpr int size = 40;
  renderer.bitmap.icon(Space, x + (width - size) / 2, y + (height - size) / 2, size, size,
                       BitmapRender::Orientation::None, selected);
}

void drawCollapse(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                  const bool selected) {
  drawKey(renderer, x, y, width, height, "", selected);
  constexpr int size = 30;
  renderer.bitmap.icon(Shift, x + (width - size) / 2, y + (height - size) / 2, size, size,
                       BitmapRender::Orientation::Rotate180, selected);
}

void drawEnter(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
               const bool selected) {
  drawKey(renderer, x, y, width, height, "", selected);
  constexpr int size = 40;
  renderer.bitmap.icon(Enter, x + (width - size) / 2, y + (height - size) / 2, size, size,
                       BitmapRender::Orientation::None, selected);
}

void append(std::string& value, const char character) {
  if (value.size() < kMaxQueryLength) value.push_back(character);
}

}  // namespace

int SearchKeyboard::height(const GfxRenderer& renderer) const {
  const int width = renderer.getScreenWidth() - kMargin * 2;
  const int keyWidth = std::max(1, (width - kGap * (kColumns - 1)) / kColumns);
  return (keyWidth + 12) * kRows + kGap * (kRows - 1);
}

void SearchKeyboard::reset() {
  row_ = 0;
  column_ = 0;
  caps_ = false;
  mode_ = 0;
}

void SearchKeyboard::render(const GfxRenderer& renderer, const int top, const int bottom) const {
  const Geometry layout = geometry(renderer, top, bottom);
  const int right = layout.left + layout.width;
  drawCharacterRow(renderer, layout, 0, rowKeys(0, mode_), caps_, row_ == 0 ? column_ : -1);
  drawCharacterRow(renderer, layout, 1, rowKeys(1, mode_), caps_, row_ == 1 ? column_ : -1);

  const char* lower = rowKeys(2, mode_);
  const int count = static_cast<int>(std::strlen(lower));
  const int actionWidth = std::max(layout.keyWidth,
                                   (layout.width - count * layout.keyWidth - kGap * (count + 1)) / 2);
  const int y = rowY(layout, 2);
  int x = layout.left;
  if (mode_ == 0) {
    drawShift(renderer, x, y, actionWidth, layout.keyHeight, row_ == 2 && column_ == 0);
  } else {
    drawActionKey(renderer, x, y, actionWidth, layout.keyHeight, "#+=", row_ == 2 && column_ == 0);
  }
  x += actionWidth + kGap;
  for (int index = 0; index < count; ++index) {
    char label[2] = {displayCharacter(caps_, lower[index]), '\0'};
    drawKey(renderer, x, y, layout.keyWidth, layout.keyHeight, label, row_ == 2 && column_ == index + 1);
    x += layout.keyWidth + kGap;
  }
  drawDelete(renderer, x, y, std::max(1, right - x), layout.keyHeight, row_ == 2 && column_ == count + 1);

  const int bottomY = rowY(layout, 3);
  if (mode_ == 0) {
    const int modeWidth = layout.width * 24 / 100;
    const int dotWidth = layout.keyWidth + 4;
    const int collapseWidth = layout.keyWidth + 4;
    const int goWidth = layout.width * 21 / 100;
    const int spaceWidth = layout.width - modeWidth - collapseWidth - dotWidth - goWidth - kGap * 4;
    x = layout.left;
    drawActionKey(renderer, x, bottomY, modeWidth, layout.keyHeight, "123", row_ == 3 && column_ == 0);
    x += modeWidth + kGap;
    drawSpace(renderer, x, bottomY, spaceWidth, layout.keyHeight, row_ == 3 && column_ == 1);
    x += spaceWidth + kGap;
    drawCollapse(renderer, x, bottomY, collapseWidth, layout.keyHeight, row_ == 3 && column_ == 2);
    x += collapseWidth + kGap;
    drawActionKey(renderer, x, bottomY, dotWidth, layout.keyHeight, ".", row_ == 3 && column_ == 3);
    x += dotWidth + kGap;
    drawEnter(renderer, x, bottomY, std::max(1, right - x), layout.keyHeight, row_ == 3 && column_ == 4);
  } else {
    const int modeWidth = layout.width * 26 / 100;
    const int collapseWidth = layout.keyWidth + 4;
    const int goWidth = layout.width * 28 / 100;
    const int spaceWidth = layout.width - modeWidth - collapseWidth - goWidth - kGap * 3;
    x = layout.left;
    drawActionKey(renderer, x, bottomY, modeWidth, layout.keyHeight, "ABC", row_ == 3 && column_ == 0);
    x += modeWidth + kGap;
    drawSpace(renderer, x, bottomY, spaceWidth, layout.keyHeight, row_ == 3 && column_ == 1);
    x += spaceWidth + kGap;
    drawCollapse(renderer, x, bottomY, collapseWidth, layout.keyHeight, row_ == 3 && column_ == 2);
    x += collapseWidth + kGap;
    drawEnter(renderer, x, bottomY, std::max(1, right - x), layout.keyHeight, row_ == 3 && column_ == 3);
  }
}

void SearchKeyboard::moveHorizontal(const int delta) {
  const int count = rowCount(row_, mode_);
  column_ = (column_ + delta) % count;
  if (column_ < 0) column_ += count;
}

void SearchKeyboard::moveVertical(const int delta) {
  row_ = (row_ + delta) % kRows;
  if (row_ < 0) row_ += kRows;
  column_ = std::min(column_, rowCount(row_, mode_) - 1);
}

SearchKeyboard::Action SearchKeyboard::activate(std::string& value) {
  const int count = rowCount(row_, mode_);
  if (row_ < 2) {
    const char* keys = rowKeys(row_, mode_);
    append(value, displayCharacter(caps_, keys[column_]));
    if (caps_) caps_ = false;
    return Action::None;
  }

  if (row_ == 2) {
    const char* keys = rowKeys(2, mode_);
    if (column_ == 0) {
      caps_ = !caps_;
    } else if (column_ == count - 1) {
      if (!value.empty()) value.pop_back();
    } else {
      append(value, displayCharacter(caps_, keys[column_ - 1]));
      if (caps_) caps_ = false;
    }
    return Action::None;
  }

  if (mode_ == 0) {
    switch (column_) {
      case 0:
        mode_ = 1;
        column_ = 0;
        break;
      case 1:
        append(value, ' ');
        break;
      case 2:
        return Action::Collapse;
      case 3:
        append(value, '.');
        break;
      case 4:
        return Action::Go;
    }
  } else {
    switch (column_) {
      case 0:
        mode_ = 0;
        column_ = 0;
        caps_ = false;
        break;
      case 1:
        append(value, ' ');
        break;
      case 2:
        return Action::Collapse;
      case 3:
        return Action::Go;
    }
  }
  return Action::None;
}
