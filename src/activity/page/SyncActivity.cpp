/**
 * @file SyncActivity.cpp
 * @brief Definitions for the redesigned Device Management page.
 */

#include "SyncActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include "activity/network/BackupRestoreActivity.h"
#include "activity/reader/ImageViewerActivity.h"
#include "activity/settings/DictionaryPickerActivity.h"
#include "activity/settings/KOReaderSettingsActivity.h"
#include "activity/settings/OtaUpdateActivity.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiLayout.h"

namespace {
constexpr int kMenuItemCount = 9;
const char* kMenuItems[kMenuItemCount] = {
    "Manage via wifi",       "Calibre File Transfer", "Create hotspot",     "OPDS Browser",
    "Backup and restore",    "KOReader Sync",         "Check for updates",   "Choose dictionary",
    "Device Information",
};
constexpr int kListItemHeight = UiLayout::LIST_ITEM_HEIGHT;
constexpr int kHeaderTop = 20;
constexpr int kHeaderHeight = 40;
constexpr int kListGap = 30;
}  // namespace

void SyncActivity::onEnter() {
  Page::onEnter();
  selectedIndex = 0;
  selectedVisible = false;
  SETTINGS.runHalfRefreshOnLoadIfEnabled(renderer, SystemSetting::RefreshOnLoadPage::Sync);
}

void SyncActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (mappedInput.getHeldTime() >= 300 && onRecentOpen) {
      vTaskDelay(pdMS_TO_TICKS(300));
      onRecentOpen();
    }
    return;
  }

  if (mappedInput.wasPressed(tabPrevButton())) {
    handleTabNavigation(true, false);
    return;
  }

  if (mappedInput.wasPressed(tabNextButton())) {
    handleTabNavigation(false, true);
    return;
  }

  if (tabSelectorIndex != 3) {
    renderIfNeeded();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    NetworkMode mode = NetworkMode::JOIN_NETWORK;
    if (selectedIndex == 1) mode = NetworkMode::CONNECT_CALIBRE;
    if (selectedIndex == 2) mode = NetworkMode::CREATE_HOTSPOT;
    if (selectedIndex == 3) mode = NetworkMode::OPDS_BROWSER;

    if (selectedIndex == 4) {
      enter(new BackupRestoreActivity(renderer, mappedInput, [this] {
        exit();
        selectedVisible = false;
        requestRender();
      }));
      return;
    }

    if (selectedIndex == 5) {
      enter(new KOReaderSettingsActivity(renderer, mappedInput, [this] {
        exit();
        selectedVisible = false;
        requestRender();
      }));
      return;
    }

    if (selectedIndex == 6) {
      enter(new OtaUpdateActivity(renderer, mappedInput, [this] {
        exit();
        selectedVisible = false;
        requestRender();
      }));
      return;
    }

    if (selectedIndex == 7) {
      enter(new DictionaryPickerActivity(renderer, mappedInput, [this] {
        exit();
        selectedVisible = false;
        requestRender();
      }));
      return;
    }

    if (selectedIndex == 8) {
      // Keep this device's existing device-identity action until its Pro-style
      // Device Information subpage is migrated into this repository.
      enter(new ImageViewerActivity(renderer, mappedInput, "/sleep/device-identity.jpg", [this] {
        exit();
        requestRender();
      }));
      return;
    }

    if (onModeSelected) onModeSelected(mode);
    return;
  }

  bool needUpdate = false;
  if (mappedInput.wasPressed(itemPrevButton())) {
    selectedIndex = (selectedIndex + kMenuItemCount - 1) % kMenuItemCount;
    selectedVisible = true;
    needUpdate = true;
  }
  if (mappedInput.wasPressed(itemNextButton())) {
    selectedIndex = (selectedIndex + 1) % kMenuItemCount;
    selectedVisible = true;
    needUpdate = true;
  }

  if (needUpdate) requestRender();
  renderIfNeeded();
}

void SyncActivity::title() const {
  const int font = MONTSERRAT_16_FONT_ID;
  const int textY = navigation::Menu::topPadding +
                    (navigation::Menu::iconSize - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, navigation::Menu::leftMargin, textY, name(), true, EpdFontFamily::BOLD);
}

void SyncActivity::content() {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int listStartY = kHeaderTop + kHeaderHeight + kListGap;
  const int contentBottom = screenHeight - navigation::Menu::bottomHeight - 10;

  for (int index = 0; index < kMenuItemCount; ++index) {
    const int itemY = listStartY + index * kListItemHeight;
    if (itemY >= contentBottom || itemY + kListItemHeight <= listStartY) continue;

    const bool selected = selectedVisible && index == selectedIndex;
    if (selected) {
      renderer.rectangle.fill(0, itemY, screenWidth, kListItemHeight,
                              static_cast<int>(GfxRenderer::FillTone::Ink));
    }

    const int titleY = itemY + (kListItemHeight - renderer.text.getLineHeight(systemFontId())) / 2;
    renderer.text.render(systemFontId(), 20, titleY, kMenuItems[index], !selected);
    const int caretWidth = renderer.text.getWidth(systemFontId(), "›");
    renderer.text.render(systemFontId(), screenWidth - caretWidth - 30, titleY, "›", !selected);

    if (index + 1 < kMenuItemCount) {
      renderer.line.render(0, itemY + kListItemHeight - 1, screenWidth, itemY + kListItemHeight - 1, true,
                           LineRender::Style::Dotted);
    }
  }
}

void SyncActivity::onExit() {
  exit();
  Page::onExit();
}

void SyncActivity::enter(Activity* activity) {
  if (!activity) return;
  subActivity.reset(activity);
  subActivity->onEnter();
}

void SyncActivity::exit() {
  if (!subActivity) return;
  subActivity->onExit();
  subActivity.reset();
}

void SyncActivity::navigateToSelectedMenu() {
  switch (tabSelectorIndex) {
    case 0:
      if (onRecentOpen) onRecentOpen();
      break;
    case 2:
      if (onSettingsOpen) onSettingsOpen();
      break;
    case 4:
      if (onStatisticsOpen) onStatisticsOpen();
      break;
    default:
      break;
  }
}
