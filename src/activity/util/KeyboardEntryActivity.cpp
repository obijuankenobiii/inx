/**
 * @file KeyboardEntryActivity.cpp
 * @brief Definitions for KeyboardEntryActivity.
 */

#include "KeyboardEntryActivity.h"

#include "system/MappedInputManager.h"
#include "system/Fonts.h"

namespace {
constexpr int KEY_HEIGHT = 34;
constexpr int KEY_SPACING = 5;
constexpr int BOTTOM_MARGIN = 52;
constexpr int PAGE_MARGIN = 18;
/** Stack size (bytes) for xTaskCreate; 2048 overflowed with render() + GfxRenderer on ESP32-C3. */
constexpr uint32_t kDisplayTaskStackBytes = 8192;
}  // namespace


const char* const KeyboardEntryActivity::keyboard[NUM_ROWS] = {
    "`1234567890-=", "qwertyuiop[]\\", "asdfghjkl;'", "zxcvbnm,./",
    "^  _____<OK"  
};


const char* const KeyboardEntryActivity::keyboardShift[NUM_ROWS] = {"~!@#$%^&*()_+", "QWERTYUIOP{}|", "ASDFGHJKL:\"",
                                                                    "ZXCVBNM<>?", "SPECIAL ROW"};

void KeyboardEntryActivity::taskTrampoline(void* param) {
  auto* self = static_cast<KeyboardEntryActivity*>(param);
  self->displayTaskLoop();
}

void KeyboardEntryActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void KeyboardEntryActivity::onEnter() {
  Activity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  
  updateRequired = true;

  xTaskCreate(&KeyboardEntryActivity::taskTrampoline, "KeyboardEntryActivity", kDisplayTaskStackBytes, this, 1,
              &displayTaskHandle);
}

void KeyboardEntryActivity::onExit() {
  Activity::onExit();

  
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

int KeyboardEntryActivity::getRowLength(const int row) const {
  if (row < 0 || row >= NUM_ROWS) return 0;

  
  switch (row) {
    case 0:
      return 13;  
    case 1:
      return 13;  
    case 2:
      return 11;  
    case 3:
      return 10;  
    case 4:
      return 10;  
    default:
      return 0;
  }
}

char KeyboardEntryActivity::getSelectedChar() const {
  const char* const* layout = shiftActive ? keyboardShift : keyboard;

  if (selectedRow < 0 || selectedRow >= NUM_ROWS) return '\0';
  if (selectedCol < 0 || selectedCol >= getRowLength(selectedRow)) return '\0';

  return layout[selectedRow][selectedCol];
}

void KeyboardEntryActivity::handleKeyPress() {
  
  if (selectedRow == SPECIAL_ROW) {
    if (selectedCol >= SHIFT_COL && selectedCol < SPACE_COL) {
      
      shiftActive = !shiftActive;
      return;
    }

    if (selectedCol >= SPACE_COL && selectedCol < BACKSPACE_COL) {
      
      if (maxLength == 0 || text.length() < maxLength) {
        text += ' ';
      }
      return;
    }

    if (selectedCol >= BACKSPACE_COL && selectedCol < DONE_COL) {
      
      if (!text.empty()) {
        text.pop_back();
      }
      return;
    }

    if (selectedCol >= DONE_COL) {
      
      if (onComplete) {
        onComplete(text);
      }
      return;
    }
  }

  
  const char c = getSelectedChar();
  if (c == '\0') {
    return;
  }

  if (maxLength == 0 || text.length() < maxLength) {
    text += c;
    
    if (shiftActive && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
      shiftActive = false;
    }
  }
}

void KeyboardEntryActivity::loop() {
  
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (selectedRow > 0) {
      selectedRow--;
      
      const int maxCol = getRowLength(selectedRow) - 1;
      if (selectedCol > maxCol) selectedCol = maxCol;
    } else {
      
      selectedRow = NUM_ROWS - 1;
      const int maxCol = getRowLength(selectedRow) - 1;
      if (selectedCol > maxCol) selectedCol = maxCol;
    }
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (selectedRow < NUM_ROWS - 1) {
      selectedRow++;
      const int maxCol = getRowLength(selectedRow) - 1;
      if (selectedCol > maxCol) selectedCol = maxCol;
    } else {
      
      selectedRow = 0;
      const int maxCol = getRowLength(selectedRow) - 1;
      if (selectedCol > maxCol) selectedCol = maxCol;
    }
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    const int maxCol = getRowLength(selectedRow) - 1;

    
    if (selectedRow == SPECIAL_ROW) {
      
      if (selectedCol >= SHIFT_COL && selectedCol < SPACE_COL) {
        
        selectedCol = maxCol;
      } else if (selectedCol >= SPACE_COL && selectedCol < BACKSPACE_COL) {
        
        selectedCol = SHIFT_COL;
      } else if (selectedCol >= BACKSPACE_COL && selectedCol < DONE_COL) {
        
        selectedCol = SPACE_COL;
      } else if (selectedCol >= DONE_COL) {
        
        selectedCol = BACKSPACE_COL;
      }
      updateRequired = true;
      return;
    }

    if (selectedCol > 0) {
      selectedCol--;
    } else {
      
      selectedCol = maxCol;
    }
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    const int maxCol = getRowLength(selectedRow) - 1;

    
    if (selectedRow == SPECIAL_ROW) {
      
      if (selectedCol >= SHIFT_COL && selectedCol < SPACE_COL) {
        
        selectedCol = SPACE_COL;
      } else if (selectedCol >= SPACE_COL && selectedCol < BACKSPACE_COL) {
        
        selectedCol = BACKSPACE_COL;
      } else if (selectedCol >= BACKSPACE_COL && selectedCol < DONE_COL) {
        
        selectedCol = DONE_COL;
      } else if (selectedCol >= DONE_COL) {
        
        selectedCol = SHIFT_COL;
      }
      updateRequired = true;
      return;
    }

    if (selectedCol < maxCol) {
      selectedCol++;
    } else {
      
      selectedCol = 0;
    }
    updateRequired = true;
  }

  
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleKeyPress();
    updateRequired = true;
  }

  
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (onCancel) {
      onCancel();
    }
    updateRequired = true;
  }
}

void KeyboardEntryActivity::render() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  constexpr int titleFont = ATKINSON_HYPERLEGIBLE_16_FONT_ID;
  constexpr int inputFont = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
  constexpr int keyFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
  constexpr int hintFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;

  renderer.text.render(titleFont, PAGE_MARGIN, 22, title.c_str(), true, EpdFontFamily::BOLD);

  std::string displayText;
  if (isPassword) {
    displayText = std::string(text.length(), '*');
  } else {
    displayText = text;
  }

  
  displayText += "_";

  const int inputX = PAGE_MARGIN;
  const int inputY = 62;
  const int inputW = pageWidth - PAGE_MARGIN * 2;
  constexpr int inputH = 56;
  renderer.rectangle.render(inputX, inputY, inputW, inputH, true, true);

  std::string inputLine = renderer.text.truncate(inputFont, displayText.c_str(), inputW - 24);
  const int inputTextY = inputY + (inputH - renderer.text.getLineHeight(inputFont)) / 2;
  renderer.text.render(inputFont, inputX + 12, inputTextY, inputLine.c_str(), true);

  if (maxLength > 0) {
    char countText[24];
    snprintf(countText, sizeof(countText), "%u/%u", static_cast<unsigned>(text.length()),
             static_cast<unsigned>(maxLength));
    const int countW = renderer.text.getWidth(hintFont, countText);
    renderer.text.render(hintFont, inputX + inputW - countW - 10, inputY + inputH + 8, countText, true);
  }

  
  const int keyboardAreaHeight = NUM_ROWS * (KEY_HEIGHT + KEY_SPACING);
  const int keyboardStartY = pageHeight - keyboardAreaHeight - BOTTOM_MARGIN;

  
  const char* const* layout = shiftActive ? keyboardShift : keyboard;

  
  const int maxKeysInRow = 13;  
  const int keyWidth = (pageWidth - PAGE_MARGIN * 2 - (maxKeysInRow - 1) * KEY_SPACING) / maxKeysInRow;

  auto drawKey = [&](const int x, const int y, const int w, const int h, const char* label, const bool selected,
                     const bool emphasized = false) {
    const int labelW = renderer.text.getWidth(keyFont, label, emphasized ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    const int labelX = x + (w - labelW) / 2;
    const int labelY = y + (h - renderer.text.getLineHeight(keyFont)) / 2;
    if (selected) {
      renderer.rectangle.fill(x, y, w, h, true, true);
      renderer.text.render(keyFont, labelX, labelY, label, false, emphasized ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
      return;
    }

    renderer.rectangle.fill(x, y, w, h, false, true);
    renderer.rectangle.render(x, y, w, h, true, true);
    if (emphasized) {
      renderer.rectangle.render(x + 2, y + 2, w - 4, h - 4, true, true);
    }
    renderer.text.render(keyFont, labelX, labelY, label, true, emphasized ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  };

  for (int row = 0; row < NUM_ROWS; row++) {
    const int rowY = keyboardStartY + row * (KEY_HEIGHT + KEY_SPACING);
    const int rowLength = getRowLength(row);

    
    if (row == 4) {
      
      
      const int shiftWidth = 2 * keyWidth + KEY_SPACING;
      const int spaceWidth = 5 * keyWidth + 4 * KEY_SPACING;
      const int backspaceWidth = 2 * keyWidth + KEY_SPACING;
      const int okWidth = 2 * keyWidth + KEY_SPACING;
      
      
      const int totalRowWidth = shiftWidth + spaceWidth + backspaceWidth + okWidth + 3 * KEY_SPACING;
      const int startX = (pageWidth - totalRowWidth) / 2;
      
      int currentX = startX;

      
      const bool shiftSelected = (selectedRow == 4 && selectedCol >= SHIFT_COL && selectedCol < SPACE_COL);
      drawKey(currentX, rowY, shiftWidth, KEY_HEIGHT, shiftActive ? "SHIFT" : "Aa", shiftSelected, shiftActive);
      currentX += shiftWidth + KEY_SPACING;

      
      const bool spaceSelected = (selectedRow == 4 && selectedCol >= SPACE_COL && selectedCol < BACKSPACE_COL);
      drawKey(currentX, rowY, spaceWidth, KEY_HEIGHT, "SPACE", spaceSelected);
      currentX += spaceWidth + KEY_SPACING;

      
      const bool bsSelected = (selectedRow == 4 && selectedCol >= BACKSPACE_COL && selectedCol < DONE_COL);
      drawKey(currentX, rowY, backspaceWidth, KEY_HEIGHT, "DEL", bsSelected);
      currentX += backspaceWidth + KEY_SPACING;

      
      const bool okSelected = (selectedRow == 4 && selectedCol >= DONE_COL);
      drawKey(currentX, rowY, okWidth, KEY_HEIGHT, "OK", okSelected, true);
    } else {
      
      const int totalRowWidth = rowLength * keyWidth + (rowLength - 1) * KEY_SPACING;
      const int startX = (pageWidth - totalRowWidth) / 2;

      for (int col = 0; col < rowLength; col++) {
        
        const char c = layout[row][col];
        char keyLabel[2] = {c, '\0'};

        const int keyX = startX + col * (keyWidth + KEY_SPACING);
        const bool isSelected = row == selectedRow && col == selectedCol;
        drawKey(keyX, rowY, keyWidth, KEY_HEIGHT, keyLabel, isSelected);
      }
    }
  }

  
  const auto labels = mappedInput.mapLabels("Back", "Select", "Prev", "Next");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_12_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
