/**
 * @file KeyboardEntryActivity.cpp
 * @brief Definitions for KeyboardEntryActivity.
 */

#include "KeyboardEntryActivity.h"

#include "system/MappedInputManager.h"
#include "system/Fonts.h"

namespace {
constexpr int KEY_HEIGHT = 28;
constexpr int KEY_SPACING = 4;
constexpr int BOTTOM_MARGIN = 60;
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

  
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, title.c_str(), true, EpdFontFamily::BOLD);

  
  const int inputStartY = 50;
  int inputEndY = inputStartY;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 10, inputStartY, "[");

  std::string displayText;
  if (isPassword) {
    displayText = std::string(text.length(), '*');
  } else {
    displayText = text;
  }

  
  displayText += "_";

  
  int lineStartIdx = 0;
  int lineEndIdx = displayText.length();
  while (true) {
    std::string lineText = displayText.substr(lineStartIdx, lineEndIdx - lineStartIdx);
    const int textWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, lineText.c_str());
    if (textWidth <= pageWidth - 40) {
      renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, inputEndY, lineText.c_str());
      if (lineEndIdx == displayText.length()) {
        break;
      }

      inputEndY += renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID);
      lineStartIdx = lineEndIdx;
      lineEndIdx = displayText.length();
    } else {
      lineEndIdx -= 1;
    }
  }
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, pageWidth - 15, inputEndY, "]");

  
  const int keyboardAreaHeight = NUM_ROWS * (KEY_HEIGHT + KEY_SPACING);
  const int keyboardStartY = pageHeight - keyboardAreaHeight - BOTTOM_MARGIN;

  
  const char* const* layout = shiftActive ? keyboardShift : keyboard;

  
  const int maxKeysInRow = 13;  
  const int keyWidth = (pageWidth - (maxKeysInRow + 1) * KEY_SPACING) / maxKeysInRow;

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
      const std::string shiftText = shiftActive ? "SHIFT" : "shift";
      const int shiftTextWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, shiftText.c_str());
      const int shiftTextX = currentX + (shiftWidth - shiftTextWidth) / 2;
      
      if (shiftSelected) {
        renderer.fillRect(currentX, rowY, shiftWidth, KEY_HEIGHT);
        renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, shiftTextX, rowY + (KEY_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2, 
                         shiftText.c_str(), false);
      } else {
        renderer.drawRect(currentX, rowY, shiftWidth, KEY_HEIGHT);
        renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, shiftTextX, rowY + (KEY_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2, 
                         shiftText.c_str(), true);
      }
      currentX += shiftWidth + KEY_SPACING;

      
      const bool spaceSelected = (selectedRow == 4 && selectedCol >= SPACE_COL && selectedCol < BACKSPACE_COL);
      const std::string spaceText = "_____";
      const int spaceTextWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, spaceText.c_str());
      const int spaceTextX = currentX + (spaceWidth - spaceTextWidth) / 2;
      
      if (spaceSelected) {
        renderer.fillRect(currentX, rowY, spaceWidth, KEY_HEIGHT);
        renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, spaceTextX, rowY + (KEY_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2, 
                         spaceText.c_str(), false);
      } else {
        renderer.drawRect(currentX, rowY, spaceWidth, KEY_HEIGHT);
        renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, spaceTextX, rowY + (KEY_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2, 
                         spaceText.c_str(), true);
      }
      currentX += spaceWidth + KEY_SPACING;

      
      const bool bsSelected = (selectedRow == 4 && selectedCol >= BACKSPACE_COL && selectedCol < DONE_COL);
      const std::string bsText = "<-";
      const int bsTextWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, bsText.c_str());
      const int bsTextX = currentX + (backspaceWidth - bsTextWidth) / 2;
      
      if (bsSelected) {
        renderer.fillRect(currentX, rowY, backspaceWidth, KEY_HEIGHT);
        renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, bsTextX, rowY + (KEY_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2, 
                         bsText.c_str(), false);
      } else {
        renderer.drawRect(currentX, rowY, backspaceWidth, KEY_HEIGHT);
        renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, bsTextX, rowY + (KEY_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2, 
                         bsText.c_str(), true);
      }
      currentX += backspaceWidth + KEY_SPACING;

      
      const bool okSelected = (selectedRow == 4 && selectedCol >= DONE_COL);
      const std::string okText = "OK";
      const int okTextWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, okText.c_str());
      const int okTextX = currentX + (okWidth - okTextWidth) / 2;
      
      if (okSelected) {
        renderer.fillRect(currentX, rowY, okWidth, KEY_HEIGHT);
        renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, okTextX, rowY + (KEY_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2, 
                         okText.c_str(), false);
      } else {
        renderer.drawRect(currentX, rowY, okWidth, KEY_HEIGHT);
        renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, okTextX, rowY + (KEY_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2, 
                         okText.c_str(), true);
      }
    } else {
      
      const int totalRowWidth = rowLength * keyWidth + (rowLength - 1) * KEY_SPACING;
      const int startX = (pageWidth - totalRowWidth) / 2;

      for (int col = 0; col < rowLength; col++) {
        
        const char c = layout[row][col];
        std::string keyLabel(1, c);
        const int charWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, keyLabel.c_str());

        const int keyX = startX + col * (keyWidth + KEY_SPACING);
        const int textX = keyX + (keyWidth - charWidth) / 2;
        const int textY = rowY + (KEY_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
        
        const bool isSelected = row == selectedRow && col == selectedCol;
        
        if (isSelected) {
          renderer.fillRect(keyX, rowY, keyWidth, KEY_HEIGHT);
          renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, textX, textY, keyLabel.c_str(), false);
        } else {
          renderer.drawRect(keyX, rowY, keyWidth, KEY_HEIGHT);
          renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, textX, textY, keyLabel.c_str(), true);
        }
      }
    }
  }

  
  const auto labels = mappedInput.mapLabels("« Back", "Select", "Left", "Right");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_12_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}