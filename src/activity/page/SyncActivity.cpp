/**
 * @file SyncActivity.cpp
 * @brief Definitions for the redesigned Device Management page.
 */

#include "SyncActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include <cstdlib>
#include <cstring>

#include "activity/network/BackupRestoreActivity.h"
#include "activity/settings/DeviceInfoActivity.h"
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

void SyncActivity::requestRender() {
  pageBufferValid_ = false;
  pageBufferBuilding_ = false;
  Page::requestRender();
}

void SyncActivity::onEnter() {
  Page::onEnter();
  pageBufferValid_ = false;
  pageBufferBuilding_ = false;
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

  if (tabSelectorIndex == 4) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      search();
      return;
    }
    renderIfNeeded();
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
      enter(new DeviceInfoActivity(renderer, mappedInput, [this] {
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
    const int nextIndex = (selectedIndex + kMenuItemCount - 1) % kMenuItemCount;
    if (tryFastSelection(nextIndex)) {
      selectedIndex = nextIndex;
    } else {
      selectedIndex = nextIndex;
      needUpdate = true;
    }
    selectedVisible = true;
  }
  if (mappedInput.wasPressed(itemNextButton())) {
    if (selectedVisible) {
      const int nextIndex = (selectedIndex + 1) % kMenuItemCount;
      if (tryFastSelection(nextIndex)) {
        selectedIndex = nextIndex;
      } else {
        selectedIndex = nextIndex;
        needUpdate = true;
      }
    } else {
      selectedIndex = 0;
      if (!tryFastSelection(selectedIndex)) needUpdate = true;
    }
    selectedVisible = true;
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
  if (canBufferPage()) {
    renderItems(false);
    pageBufferBuilding_ = true;
    return;
  }
  renderItems(selectedVisible);
}

void SyncActivity::renderItems(const bool selected) const {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int listStartY = kHeaderTop + kHeaderHeight + kListGap;
  const int contentBottom = screenHeight - navigation::Menu::bottomHeight - 10;

  for (int index = 0; index < kMenuItemCount; ++index) {
    const int itemY = listStartY + index * kListItemHeight;
    if (itemY >= contentBottom || itemY + kListItemHeight <= listStartY) continue;

    const bool itemSelected = selected && selectedVisible && index == selectedIndex;
    if (itemSelected) {
      renderer.rectangle.fill(0, itemY, screenWidth, kListItemHeight,
                              static_cast<int>(GfxRenderer::FillTone::Ink));
    }

    const int titleY = itemY + (kListItemHeight - renderer.text.getLineHeight(systemFontId())) / 2;
    renderer.text.render(systemFontId(), 20, titleY, kMenuItems[index], !itemSelected);
    const int caretWidth = renderer.text.getWidth(systemFontId(), "›");
    renderer.text.render(systemFontId(), screenWidth - caretWidth - 30, titleY, "›", !itemSelected);

    if (index + 1 < kMenuItemCount) {
      renderer.line.render(0, itemY + kListItemHeight - 1, screenWidth, itemY + kListItemHeight - 1, true,
                           LineRender::Style::Dotted);
    }
  }
}

void SyncActivity::afterRender() {
  if (!pageBufferBuilding_) return;
  pageBufferBuilding_ = false;

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer || !storePageBuffer()) {
    pageBufferValid_ = false;
    renderItems(true);
    return;
  }

  memcpy(pageBuffer_, frameBuffer, renderer.getBufferSize());
  pageBufferValid_ = true;
  if (selectedVisible) renderSelection();
}

bool SyncActivity::canBufferPage() const {
  return !subActivity && tabSelectorIndex == 3;
}

bool SyncActivity::storePageBuffer() {
  if (!renderer.getFrameBuffer()) return false;
  if (!pageBuffer_) {
    pageBuffer_ = static_cast<uint8_t*>(std::malloc(renderer.getBufferSize()));
    if (!pageBuffer_) return false;
  }
  return true;
}

bool SyncActivity::restorePageBuffer() {
  if (!pageBufferValid_ || !pageBuffer_) return false;
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) return false;
  memcpy(frameBuffer, pageBuffer_, renderer.getBufferSize());
  return true;
}

void SyncActivity::renderSelection() const {
  if (!selectedVisible || selectedIndex < 0 || selectedIndex >= kMenuItemCount) return;

  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int listStartY = kHeaderTop + kHeaderHeight + kListGap;
  const int contentBottom = screenHeight - navigation::Menu::bottomHeight - 10;
  const int itemY = listStartY + selectedIndex * kListItemHeight;
  if (itemY >= contentBottom || itemY + kListItemHeight <= listStartY) return;

  const bool selected = true;
  renderer.rectangle.fill(0, itemY, screenWidth, kListItemHeight,
                          static_cast<int>(GfxRenderer::FillTone::Ink));
  const int titleY = itemY + (kListItemHeight - renderer.text.getLineHeight(systemFontId())) / 2;
  renderer.text.render(systemFontId(), 20, titleY, kMenuItems[selectedIndex], !selected);
  const int caretWidth = renderer.text.getWidth(systemFontId(), "›");
  renderer.text.render(systemFontId(), screenWidth - caretWidth - 30, titleY, "›", !selected);
  if (selectedIndex + 1 < kMenuItemCount && itemY + kListItemHeight < contentBottom) {
    renderer.line.render(0, itemY + kListItemHeight - 1, screenWidth, itemY + kListItemHeight - 1, true,
                         LineRender::Style::Dotted);
  }
}

bool SyncActivity::tryFastSelection(const int nextIndex) {
  if (!canBufferPage() || !pageBufferValid_ || !pageBuffer_) return false;
  if (!restorePageBuffer()) return false;
  selectedIndex = nextIndex;
  selectedVisible = true;
  renderSelection();
  renderer.displayBuffer();
  return true;
}

void SyncActivity::onExit() {
  exit();
  pageBufferValid_ = false;
  pageBufferBuilding_ = false;
  if (pageBuffer_) {
    std::free(pageBuffer_);
    pageBuffer_ = nullptr;
  }
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
      // The fifth shell slot is the shared Search action, not the legacy
      // statistics destination used by the old tab bar.
      requestRender();
      break;
    default:
      requestRender();
      break;
  }
}
