/**
 * @file CalibreSettingsActivity.cpp
 * @brief Definitions for CalibreSettingsActivity.
 */

#include "CalibreSettingsActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include <cstring>

#include "activity/util/KeyboardEntryActivity.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"
#include "system/UiTheme.h"

namespace {
constexpr int MENU_ITEMS = 3;
constexpr int kListItemHeight = UiTheme::DRAWER_LIST_ITEM_HEIGHT;
const char* menuNames[MENU_ITEMS] = {"OPDS Server URL", "Username", "Password"};
}  // namespace

/** FreeRTOS task entry point that forwards to displayTaskLoop. */
void CalibreSettingsActivity::taskTrampoline(void* param) {
  auto* self = static_cast<CalibreSettingsActivity*>(param);
  self->displayTaskLoop();
}

/** Starts the display task and initializes selection state. */
void CalibreSettingsActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();
  selectedIndex = 0;
  updateRequired = true;

  xTaskCreate(&CalibreSettingsActivity::taskTrampoline, "CalibreSettingsTask", 4096, this, 1, &displayTaskHandle);
}

/** Stops the display task and releases synchronization resources. */
void CalibreSettingsActivity::onExit() {
  ActivityWithSubactivity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

/** Handles one iteration of input processing for the menu. */
void CalibreSettingsActivity::loop() {
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

  if (mappedInput.wasPressed(MenuNav::itemPrev()) || mappedInput.wasPressed(MenuNav::tabPrev())) {
    selectedIndex = (selectedIndex + MENU_ITEMS - 1) % MENU_ITEMS;
    updateRequired = true;
  } else if (mappedInput.wasPressed(MenuNav::itemNext()) || mappedInput.wasPressed(MenuNav::tabNext())) {
    selectedIndex = (selectedIndex + 1) % MENU_ITEMS;
    updateRequired = true;
  }
}

/** Opens the keyboard entry activity for the currently selected setting. */
void CalibreSettingsActivity::handleSelection() {
  xSemaphoreTake(renderingMutex, portMAX_DELAY);

  if (selectedIndex == 0) {
    exitActivity();
    enterNewActivity(new KeyboardEntryActivity(
        renderer, mappedInput, "OPDS Server URL", SETTINGS.opdsServerUrl, 10, 127, false,
        [this](const std::string& url) {
          strncpy(SETTINGS.opdsServerUrl, url.c_str(), sizeof(SETTINGS.opdsServerUrl) - 1);
          SETTINGS.opdsServerUrl[sizeof(SETTINGS.opdsServerUrl) - 1] = '\0';
          SETTINGS.saveToFile();
          exitActivity();
          updateRequired = true;
        },
        [this]() {
          exitActivity();
          updateRequired = true;
        }));
  } else if (selectedIndex == 1) {
    exitActivity();
    enterNewActivity(new KeyboardEntryActivity(
        renderer, mappedInput, "Username", SETTINGS.opdsUsername, 10, 63, false,
        [this](const std::string& username) {
          strncpy(SETTINGS.opdsUsername, username.c_str(), sizeof(SETTINGS.opdsUsername) - 1);
          SETTINGS.opdsUsername[sizeof(SETTINGS.opdsUsername) - 1] = '\0';
          SETTINGS.saveToFile();
          exitActivity();
          updateRequired = true;
        },
        [this]() {
          exitActivity();
          updateRequired = true;
        }));
  } else if (selectedIndex == 2) {
    exitActivity();
    enterNewActivity(new KeyboardEntryActivity(
        renderer, mappedInput, "Password", SETTINGS.opdsPassword, 10, 63, false,
        [this](const std::string& password) {
          strncpy(SETTINGS.opdsPassword, password.c_str(), sizeof(SETTINGS.opdsPassword) - 1);
          SETTINGS.opdsPassword[sizeof(SETTINGS.opdsPassword) - 1] = '\0';
          SETTINGS.saveToFile();
          exitActivity();
          updateRequired = true;
        },
        [this]() {
          exitActivity();
          updateRequired = true;
        }));
  }

  xSemaphoreGive(renderingMutex);
}

/** Background task loop that redraws the menu when required. */
void CalibreSettingsActivity::displayTaskLoop() {
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

/** Draws the OPDS settings menu. */
void CalibreSettingsActivity::render() {
#ifdef SIMULATOR
  Serial.printf("[%lu] [SIM] OPDS settings UI: urlSet=%d usernameSet=%d passwordSet=%d\n", millis(),
                strlen(SETTINGS.opdsServerUrl) > 0, strlen(SETTINGS.opdsUsername) > 0,
                strlen(SETTINGS.opdsPassword) > 0);
#endif
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();

  const int dividerY = INX_THEME.drawPageHeader(renderer, "OPDS Browser");

  for (int i = 0; i < MENU_ITEMS; i++) {
    const int itemY = dividerY + i * kListItemHeight;
    const bool isSelected = (i == selectedIndex);
    if (isSelected) {
      renderer.rectangle.fill(0, itemY, pageWidth, kListItemHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
    }
    const int settingY = itemY + (kListItemHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, settingY, menuNames[i], !isSelected);

    const char* status = "[Not Set]";
    if (i == 0) {
      status = (strlen(SETTINGS.opdsServerUrl) > 0) ? "[Set]" : "[Not Set]";
    } else if (i == 1) {
      status = (strlen(SETTINGS.opdsUsername) > 0) ? "[Set]" : "[Not Set]";
    } else if (i == 2) {
      status = (strlen(SETTINGS.opdsPassword) > 0) ? "[Set]" : "[Not Set]";
    }
    const auto width = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, status);
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageWidth - 20 - width, settingY, status, !isSelected);
    renderer.line.render(0, itemY + kListItemHeight - 1, pageWidth, itemY + kListItemHeight - 1, true,
                         LineRender::Style::Dotted);
  }

  const auto labels = mappedInput.mapLabels("« Back", "Select", "", "");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
