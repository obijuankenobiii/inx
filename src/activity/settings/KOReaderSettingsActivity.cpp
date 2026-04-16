#include "KOReaderSettingsActivity.h"

#include <GfxRenderer.h>
#include <cstring>

#include "KOReaderAuthActivity.h"
#include "KOReaderCredentialStore.h"
#include "system/MappedInputManager.h"
#include "activity/util/KeyboardEntryActivity.h"
#include "system/Fonts.h"

constexpr int LIST_ITEM_HEIGHT = 60;
constexpr int HEADER_HEIGHT = 60;

namespace {
constexpr int MENU_ITEMS = 5;
const char* menuNames[MENU_ITEMS] = {"Username", "Password", "Sync Server URL", "Document Matching", "Authenticate"};
}  // namespace

void KOReaderSettingsActivity::taskTrampoline(void* param) {
  auto* self = static_cast<KOReaderSettingsActivity*>(param);
  self->displayTaskLoop();
}

void KOReaderSettingsActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  selectedIndex = 0;
  updateRequired = true;

  xTaskCreate(&KOReaderSettingsActivity::taskTrampoline, "KOReaderSettingsTask",
              4096,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void KOReaderSettingsActivity::onExit() {
  ActivityWithSubactivity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void KOReaderSettingsActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    selectedIndex = (selectedIndex + MENU_ITEMS - 1) % MENU_ITEMS;
    updateRequired = true;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Down) ||
             mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    selectedIndex = (selectedIndex + 1) % MENU_ITEMS;
    updateRequired = true;
  }
}

void KOReaderSettingsActivity::handleSelection() {
  xSemaphoreTake(renderingMutex, portMAX_DELAY);

  if (selectedIndex == 0) {
    // Username
    exitActivity();
    enterNewActivity(new KeyboardEntryActivity(
        renderer, mappedInput, "KOReader Username", KOREADER_STORE.getUsername(), 10,
        64,     // maxLength
        false,  // not password
        [this](const std::string& username) {
          KOREADER_STORE.setCredentials(username, KOREADER_STORE.getPassword());
          KOREADER_STORE.saveToFile();
          exitActivity();
          updateRequired = true;
        },
        [this]() {
          exitActivity();
          updateRequired = true;
        }));
  } else if (selectedIndex == 1) {
    // Password
    exitActivity();
    enterNewActivity(new KeyboardEntryActivity(
        renderer, mappedInput, "KOReader Password", KOREADER_STORE.getPassword(), 10,
        64,     // maxLength
        true,   // password mode
        [this](const std::string& password) {
          KOREADER_STORE.setCredentials(KOREADER_STORE.getUsername(), password);
          KOREADER_STORE.saveToFile();
          exitActivity();
          updateRequired = true;
        },
        [this]() {
          exitActivity();
          updateRequired = true;
        }));
  } else if (selectedIndex == 2) {
    // Sync Server URL - prefill with https:// if empty to save typing
    const std::string currentUrl = KOREADER_STORE.getServerUrl();
    const std::string prefillUrl = currentUrl.empty() ? "https://" : currentUrl;
    exitActivity();
    enterNewActivity(new KeyboardEntryActivity(
        renderer, mappedInput, "Sync Server URL", prefillUrl, 10,
        128,    // maxLength - URLs can be long
        false,  // not password
        [this](const std::string& url) {
          // Clear if user just left the prefilled https://
          const std::string urlToSave = (url == "https://" || url == "http://") ? "" : url;
          KOREADER_STORE.setServerUrl(urlToSave);
          KOREADER_STORE.saveToFile();
          exitActivity();
          updateRequired = true;
        },
        [this]() {
          exitActivity();
          updateRequired = true;
        }));
  } else if (selectedIndex == 3) {
    // Document Matching - toggle between Filename and Binary
    const auto current = KOREADER_STORE.getMatchMethod();
    const auto newMethod =
        (current == DocumentMatchMethod::FILENAME) ? DocumentMatchMethod::BINARY : DocumentMatchMethod::FILENAME;
    KOREADER_STORE.setMatchMethod(newMethod);
    KOREADER_STORE.saveToFile();
    updateRequired = true;
  } else if (selectedIndex == 4) {
    // Authenticate
    if (!KOREADER_STORE.hasCredentials()) {
      // Can't authenticate without credentials - just show message briefly
      xSemaphoreGive(renderingMutex);
      return;
    }
    exitActivity();
    enterNewActivity(new KOReaderAuthActivity(renderer, mappedInput, [this] {
      exitActivity();
      updateRequired = true;
    }));
  }

  xSemaphoreGive(renderingMutex);
}

void KOReaderSettingsActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void KOReaderSettingsActivity::render() {
  const auto screenWidth = renderer.getScreenWidth();
  const auto screenHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, 25, "KOReader Sync", true, EpdFontFamily::BOLD);
  
  const char* subtitleText = "Configure sync settings.";
  renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, 55, subtitleText);

  const int dividerY = HEADER_HEIGHT + 30;
  renderer.drawLine(0, dividerY, screenWidth, dividerY);

  // Draw menu items
  int startY = dividerY;
  int visibleAreaHeight = screenHeight - startY - 60;
  
  for (int i = 0; i < MENU_ITEMS; i++) {
    if (i * LIST_ITEM_HEIGHT < visibleAreaHeight) {
      int itemY = startY + (i * LIST_ITEM_HEIGHT);
      bool isSelected = (i == selectedIndex);

      if (isSelected) {
        renderer.fillRect(0, itemY, screenWidth, LIST_ITEM_HEIGHT, GfxRenderer::FillTone::Ink);
      }

      // Calculate text Y position for perfect vertical centering
      int textY = itemY + (LIST_ITEM_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

      // Draw menu name
      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, textY, menuNames[i], !isSelected);

      // Draw status/value on the right
      const char* status = "";
      if (i == 0) {
        status = KOREADER_STORE.getUsername().empty() ? "Not Set" : "Set";
      } else if (i == 1) {
        status = KOREADER_STORE.getPassword().empty() ? "Not Set" : "Set";
      } else if (i == 2) {
        status = KOREADER_STORE.getServerUrl().empty() ? "Default" : "Custom";
      } else if (i == 3) {
        status = KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME ? "Filename" : "Binary";
      } else if (i == 4) {
        status = KOREADER_STORE.hasCredentials() ? "" : "Set credentials first";
      }

      if (strlen(status) > 0) {
        int statusWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, status);
        renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenWidth - statusWidth - 40, textY, status, !isSelected);
      }

      // Draw arrow for items that have submenus (not Document Matching)
      if (i != 3) {
        renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenWidth - 25, textY, "›", !isSelected);
      }

      renderer.drawLine(0, itemY + LIST_ITEM_HEIGHT - 1, screenWidth, itemY + LIST_ITEM_HEIGHT - 1);
    }
  }

  // Draw button hints
  const auto labels = mappedInput.mapLabels("« Back", "Select", "", "");
  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}