#include "KeyboardEntryActivity.h"

#include "system/MappedInputManager.h"
#include "system/Fonts.h"

namespace {
constexpr int KEY_HEIGHT = 28;  // Increased height for better touch target
constexpr int KEY_SPACING = 4;  // Reduced spacing to fit more keys
constexpr int BOTTOM_MARGIN = 60;  // Space for button hints at bottom
}  // namespace

// Keyboard layouts - lowercase
const char* const KeyboardEntryActivity::keyboard[NUM_ROWS] = {
    "`1234567890-=", "qwertyuiop[]\\", "asdfghjkl;'", "zxcvbnm,./",
    "^  _____<OK"  // ^ = shift, _ = space, < = backspace, OK = done
};

// Keyboard layouts - uppercase/symbols
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

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&KeyboardEntryActivity::taskTrampoline, "KeyboardEntryActivity",
              2048,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void KeyboardEntryActivity::onExit() {
  Activity::onExit();

  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
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

  // Return actual length of each row based on keyboard layout
  switch (row) {
    case 0:
      return 13;  // `1234567890-=
    case 1:
      return 13;  // qwertyuiop[]backslash
    case 2:
      return 11;  // asdfghjkl;'
    case 3:
      return 10;  // zxcvbnm,./
    case 4:
      return 10;  // shift (2 wide), space (5 wide), backspace (2 wide), OK
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
  // Handle special row (bottom row with shift, space, backspace, done)
  if (selectedRow == SPECIAL_ROW) {
    if (selectedCol >= SHIFT_COL && selectedCol < SPACE_COL) {
      // Shift toggle
      shiftActive = !shiftActive;
      return;
    }

    if (selectedCol >= SPACE_COL && selectedCol < BACKSPACE_COL) {
      // Space bar
      if (maxLength == 0 || text.length() < maxLength) {
        text += ' ';
      }
      return;
    }

    if (selectedCol >= BACKSPACE_COL && selectedCol < DONE_COL) {
      // Backspace
      if (!text.empty()) {
        text.pop_back();
      }
      return;
    }

    if (selectedCol >= DONE_COL) {
      // Done button
      if (onComplete) {
        onComplete(text);
      }
      return;
    }
  }

  // Regular character
  const char c = getSelectedChar();
  if (c == '\0') {
    return;
  }

  if (maxLength == 0 || text.length() < maxLength) {
    text += c;
    // Auto-disable shift after typing a letter
    if (shiftActive && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) {
      shiftActive = false;
    }
  }
}

void KeyboardEntryActivity::loop() {
  // Navigation
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (selectedRow > 0) {
      selectedRow--;
      // Clamp column to valid range for new row
      const int maxCol = getRowLength(selectedRow) - 1;
      if (selectedCol > maxCol) selectedCol = maxCol;
    } else {
      // Wrap to bottom row
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
      // Wrap to top row
      selectedRow = 0;
      const int maxCol = getRowLength(selectedRow) - 1;
      if (selectedCol > maxCol) selectedCol = maxCol;
    }
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    const int maxCol = getRowLength(selectedRow) - 1;

    // Special bottom row case
    if (selectedRow == SPECIAL_ROW) {
      // Bottom row has special key widths
      if (selectedCol >= SHIFT_COL && selectedCol < SPACE_COL) {
        // In shift key, wrap to end of row
        selectedCol = maxCol;
      } else if (selectedCol >= SPACE_COL && selectedCol < BACKSPACE_COL) {
        // In space bar, move to shift
        selectedCol = SHIFT_COL;
      } else if (selectedCol >= BACKSPACE_COL && selectedCol < DONE_COL) {
        // In backspace, move to space
        selectedCol = SPACE_COL;
      } else if (selectedCol >= DONE_COL) {
        // At done button, move to backspace
        selectedCol = BACKSPACE_COL;
      }
      updateRequired = true;
      return;
    }

    if (selectedCol > 0) {
      selectedCol--;
    } else {
      // Wrap to end of current row
      selectedCol = maxCol;
    }
    updateRequired = true;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    const int maxCol = getRowLength(selectedRow) - 1;

    // Special bottom row case
    if (selectedRow == SPECIAL_ROW) {
      // Bottom row has special key widths
      if (selectedCol >= SHIFT_COL && selectedCol < SPACE_COL) {
        // In shift key, move to space
        selectedCol = SPACE_COL;
      } else if (selectedCol >= SPACE_COL && selectedCol < BACKSPACE_COL) {
        // In space bar, move to backspace
        selectedCol = BACKSPACE_COL;
      } else if (selectedCol >= BACKSPACE_COL && selectedCol < DONE_COL) {
        // In backspace, move to done
        selectedCol = DONE_COL;
      } else if (selectedCol >= DONE_COL) {
        // At done button, wrap to beginning of row
        selectedCol = SHIFT_COL;
      }
      updateRequired = true;
      return;
    }

    if (selectedCol < maxCol) {
      selectedCol++;
    } else {
      // Wrap to beginning of current row
      selectedCol = 0;
    }
    updateRequired = true;
  }

  // Selection
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleKeyPress();
    updateRequired = true;
  }

  // Cancel
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

  // Draw title
  renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, title.c_str(), true, EpdFontFamily::BOLD);

  // Draw input field
  const int inputStartY = 50;
  int inputEndY = inputStartY;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 10, inputStartY, "[");

  std::string displayText;
  if (isPassword) {
    displayText = std::string(text.length(), '*');
  } else {
    displayText = text;
  }

  // Show cursor at end
  displayText += "_";

  // Render input text across multiple lines
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

  // Calculate keyboard position - push it down almost to the bottom
  const int keyboardAreaHeight = NUM_ROWS * (KEY_HEIGHT + KEY_SPACING);
  const int keyboardStartY = pageHeight - keyboardAreaHeight - BOTTOM_MARGIN;

  // Draw keyboard - use full width keys
  const char* const* layout = shiftActive ? keyboardShift : keyboard;

  // Calculate key width based on screen width and number of keys per row
  const int maxKeysInRow = 13;  // Row 0 and 1 have 13 keys
  const int keyWidth = (pageWidth - (maxKeysInRow + 1) * KEY_SPACING) / maxKeysInRow;

  for (int row = 0; row < NUM_ROWS; row++) {
    const int rowY = keyboardStartY + row * (KEY_HEIGHT + KEY_SPACING);
    const int rowLength = getRowLength(row);

    // Calculate total width of this row and center it
    if (row == 4) {
      // Bottom row layout: SHIFT (2 cols) | SPACE (5 cols) | <- (2 cols) | OK (2 cols)
      // Calculate widths for special keys (using same keyWidth as other rows)
      const int shiftWidth = 2 * keyWidth + KEY_SPACING;
      const int spaceWidth = 5 * keyWidth + 4 * KEY_SPACING;
      const int backspaceWidth = 2 * keyWidth + KEY_SPACING;
      const int okWidth = 2 * keyWidth + KEY_SPACING;
      
      // Calculate total width of bottom row
      const int totalRowWidth = shiftWidth + spaceWidth + backspaceWidth + okWidth + 3 * KEY_SPACING;
      const int startX = (pageWidth - totalRowWidth) / 2;
      
      int currentX = startX;

      // SHIFT key (logical col 0, spans 2 key widths)
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

      // Space bar (logical cols 2-6, spans 5 key widths)
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

      // Backspace key (logical col 7, spans 2 key widths)
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

      // OK button (logical col 9, spans 2 key widths)
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
      // Regular rows: render each key individually
      const int totalRowWidth = rowLength * keyWidth + (rowLength - 1) * KEY_SPACING;
      const int startX = (pageWidth - totalRowWidth) / 2;

      for (int col = 0; col < rowLength; col++) {
        // Get the character to display
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

  // Draw help text at the very bottom
  const auto labels = mappedInput.mapLabels("« Back", "Select", "Left", "Right");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_12_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}