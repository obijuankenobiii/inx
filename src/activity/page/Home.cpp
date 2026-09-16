/**
 * @file Home.cpp
 * @brief Empty Home page shell for the inx-pro UI migration.
 */

#include "Home.h"

#include "HomeSubPage.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "components/global/PopUp.h"
#include "components/widget/grid/Grid.h"
#include "components/widget/grid2x2/Grid2x2.h"
#include "images/Hamburger.h"
#include "state/RecentBooks.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiLayout.h"

extern void onGoToLibrary(const std::string& path);
extern void onGoToReader(const std::string& path);
extern void onGoToDescription(const std::string& path);
extern void onGoToSettings();
extern void onGoToFileTransfer();
extern void openHomeSubPage(HomeSubPage::Section section);
extern void onGoToStatistics();

namespace {

constexpr unsigned long kDoubleBackWindowMs = 450;
constexpr unsigned long kConfirmLongPressMs = 500;
constexpr int kGrid3x2PageSize = 6;
constexpr int kGrid2x2PageSize = 4;
unsigned long lastBackReleaseMs = 0;

std::string cachePath(const RecentBook& book) {
  if (!book.cachePath.empty()) return book.cachePath;
  return "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(book.path));
}

bool removeTree(const std::string& path, int& removed) {
  FsFile directory = SdMan.open(path.c_str());
  if (!directory || !directory.isDirectory()) return false;

  char name[128] = {};
  while (true) {
    FsFile entry = directory.openNextFile();
    if (!entry) break;
    const bool isDirectory = entry.isDirectory();
    entry.getName(name, sizeof(name));
    entry.close();

    const std::string child = path + "/" + name;
    if (isDirectory ? !removeTree(child, removed) : !SdMan.remove(child.c_str())) {
      directory.close();
      return false;
    }
    if ((++removed & 7) == 0) {
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  directory.close();
  return SdMan.removeDir(path.c_str());
}

}  // namespace

Home::Home(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Page("Home", renderer, mappedInput), recentWidget(renderer) {}

void Home::onEnter() {
  Page::onEnter();
  invalidateGridPageBuffer();
  invalidateShortcutPageBuffer();
  sidebarOpen = false;
  tabSelectorIndex = 0;
  recentIndex_ = 0;
  dashboardCarouselFocused_ = false;
  recentPopupAction_ = 0;
  recentPopupPath_.clear();
  shortcutIndex_ = 0;
  confirmLongPressProcessed_ = false;
  ignoreBackReleaseAfterPopup_ = false;
  ignoreBackReleaseOnEnter_ = mappedInput.isPressed(MappedInputManager::Button::Back);
}

void Home::onExit() {
  invalidateGridPageBuffer();
  invalidateShortcutPageBuffer();
  if (gridPageBuffer_) {
    std::free(gridPageBuffer_);
    gridPageBuffer_ = nullptr;
  }
  Page::onExit();
}

void Home::loop() {
  if (ignoreBackReleaseOnEnter_) {
    if (mappedInput.isPressed(MappedInputManager::Button::Back)) return;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      ignoreBackReleaseOnEnter_ = false;
      return;
    }
    ignoreBackReleaseOnEnter_ = false;
  }

  if (!recentPopupPath_.empty()) {
    if (recentPopupInput()) return;
    // Keep the popup modal, but let Page::loop() perform the pending redraw.
    // Returning on every idle loop prevents the popup from ever reaching the
    // shared renderer.
    Page::loop();
    return;
  }

  if (sidebarOpen && shortcutInput()) return;

  if (ignoreBackReleaseAfterPopup_ && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ignoreBackReleaseAfterPopup_ = false;
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    const unsigned long now = millis();
    const bool doubleBack = lastBackReleaseMs != 0 && now - lastBackReleaseMs <= kDoubleBackWindowMs;
    lastBackReleaseMs = doubleBack ? 0 : now;
    if (doubleBack) {
      sidebarOpen = true;
    } else {
      sidebarOpen = !sidebarOpen;
    }
    invalidateGridPageBuffer();
    invalidateShortcutPageBuffer();
    requestRender();
    return;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Back)) {
    return;
  }

  // Selecting Search in the main menu only focuses its container. Confirm is
  // the activation action that opens the actual search page.
  if (tabSelectorIndex == 4) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      search();
      return;
    }
    Page::loop();
    return;
  }

  // These are the same item controls used by RecentActivity's Flow/Grid/List
  // views in the linked inx source. The bottom menu keeps left/right for tab
  // navigation, while up/down select a recent book and Confirm opens it.
  const auto& books = RECENT_BOOKS.getBooks();
  const int recentCount = static_cast<int>(books.size());
  const bool dashboard = widget::Recent::modeFromSetting(SETTINGS.recentLibraryMode) == widget::Recent::Mode::Dashboard;

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    confirmLongPressProcessed_ = false;
  }
  if (recentCount > 0 && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      !confirmLongPressProcessed_ && mappedInput.getHeldTime() >= kConfirmLongPressMs) {
    const int index = std::max(0, std::min(recentIndex_, recentCount - 1));
    recentPopupPath_ = books[static_cast<size_t>(index)].path;
    recentPopupAction_ = 0;
    confirmLongPressProcessed_ = true;
    requestRender();
    return;
  }

  if (recentCount > 0 && mappedInput.wasPressed(itemNextButton())) {
    if (dashboard) {
      if (!dashboardCarouselFocused_ && recentCount > 1) {
        dashboardCarouselFocused_ = true;
        recentIndex_ = 1;
      } else if (dashboardCarouselFocused_ && recentCount > 1) {
        recentIndex_ = recentIndex_ + 1 < recentCount ? recentIndex_ + 1 : 1;
      }
    } else {
      const int nextIndex = (recentIndex_ + 1) % recentCount;
      if (!tryFastGridSelection(nextIndex)) {
        recentIndex_ = nextIndex;
        invalidateGridPageBuffer();
        requestRender();
      } else {
        recentIndex_ = nextIndex;
      }
    }
    if (dashboard) requestRender();
    return;
  }
  if (recentCount > 0 && mappedInput.wasPressed(itemPrevButton())) {
    if (dashboard) {
      if (dashboardCarouselFocused_) {
        if (recentIndex_ <= 1) {
          dashboardCarouselFocused_ = false;
          recentIndex_ = 0;
        } else {
          --recentIndex_;
        }
      }
    } else {
      const int nextIndex = (recentIndex_ + recentCount - 1) % recentCount;
      if (!tryFastGridSelection(nextIndex)) {
        recentIndex_ = nextIndex;
        invalidateGridPageBuffer();
        requestRender();
      } else {
        recentIndex_ = nextIndex;
      }
    }
    if (dashboard) requestRender();
    return;
  }
  if (recentCount > 0 && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // The input manager keeps the final hold duration after release. Use it as
    // a fallback as well as the live isPressed() check above; this is important
    // for every widget mode because image-heavy renders can skip the exact
    // loop in which the hold threshold is crossed.
    const bool wasLongPress = confirmLongPressProcessed_ || mappedInput.getHeldTime() >= kConfirmLongPressMs;
    if (wasLongPress && !confirmLongPressProcessed_) {
      const int index = std::max(0, std::min(recentIndex_, recentCount - 1));
      recentPopupPath_ = books[static_cast<size_t>(index)].path;
      recentPopupAction_ = 0;
      requestRender();
    }
    confirmLongPressProcessed_ = false;
    if (wasLongPress) return;
    const int index = std::max(0, std::min(recentIndex_, recentCount - 1));
    onGoToReader(books[static_cast<size_t>(index)].path);
    return;
  }

  Page::loop();
}

void Home::menu() {
  Page::menu();
  if (!sidebarOpen) return;

  // The shortcut drawer is drawn over a complete Home frame. Keep that frame
  // in the same cache used by the grid so moving through shortcuts can restore
  // it without rebuilding the image-heavy Home content.
  if (!shortcutPageBufferValid_) {
    shortcutPageBufferValid_ = storeShortcutPageBuffer();
  } else if (!restoreShortcutPageBuffer()) {
    shortcutPageBufferValid_ = storeShortcutPageBuffer();
  }
  navigation::Sidebar::render(renderer, "Shortcuts", shortcutIndex_);
}

bool Home::shortcutInput() {
  if (mappedInput.wasPressed(itemPrevButton())) {
    shortcutIndex_ = (shortcutIndex_ + navigation::Sidebar::shortcutCount - 1) % navigation::Sidebar::shortcutCount;
    if (restoreShortcutPageBuffer()) {
      navigation::Sidebar::render(renderer, "Shortcuts", shortcutIndex_);
      renderer.displayBuffer();
    } else {
      requestRender();
    }
    return true;
  }
  if (mappedInput.wasPressed(itemNextButton())) {
    shortcutIndex_ = (shortcutIndex_ + 1) % navigation::Sidebar::shortcutCount;
    if (restoreShortcutPageBuffer()) {
      navigation::Sidebar::render(renderer, "Shortcuts", shortcutIndex_);
      renderer.displayBuffer();
    } else {
      requestRender();
    }
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) return handleShortcut(shortcutIndex_);
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) ||
      mappedInput.wasPressed(tabPrevButton()) || mappedInput.wasPressed(tabNextButton())) {
    return true;
  }
  return false;
}

bool Home::handleShortcut(const int index) {
  sidebarOpen = false;
  invalidateGridPageBuffer();
  invalidateShortcutPageBuffer();
  requestRender();
  switch (index) {
    case 0: openHomeSubPage(HomeSubPage::Section::Bookmarks); return true;
    case 1: openHomeSubPage(HomeSubPage::Section::Highlights); return true;
    case 2: openHomeSubPage(HomeSubPage::Section::Favorites); return true;
    case 3: onGoToStatistics(); return true;
    case 4: openHomeSubPage(HomeSubPage::Section::Dictionary); return true;
    default: return false;
  }
}

void Home::title() const {
  renderer.bitmap.icon(Hamburger, UiLayout::MENU_LEFT_MARGIN, UiLayout::MENU_TOP_PADDING, UiLayout::MENU_ICON_SIZE,
                       UiLayout::MENU_ICON_SIZE);
  renderer.text.render(MONTSERRAT_16_FONT_ID,
                       UiLayout::MENU_LEFT_MARGIN + UiLayout::MENU_ICON_SIZE + 12, UiLayout::MENU_TOP_PADDING, name(), true,
                       EpdFontFamily::BOLD);
}

int Home::top() const { return navigation::Menu::height; }

int Home::bottom() const { return renderer.getScreenHeight() - navigation::Menu::bottomHeight; }

void Home::content() {
  const int contentHeight = std::max(0, bottom() - top());
  if (contentHeight <= 0) {
    return;
  }

  const widget::Recent::Mode mode = widget::Recent::modeFromSetting(SETTINGS.recentLibraryMode);
  const bool canBufferRecentGrid = (mode == widget::Recent::Mode::Grid || mode == widget::Recent::Mode::Grid2x2) &&
                                   tabSelectorIndex == 0 && !sidebarOpen && recentPopupPath_.empty();
  if (canBufferRecentGrid) {
    // Build the page without its selection highlight. afterRender() snapshots
    // the completed page (including the header menu) and then draws the
    // highlight on top, leaving a clean frame to restore on the next move.
    invalidateGridPageBuffer();
    recentWidget.render(mode, 0, top(), renderer.getScreenWidth(), contentHeight, recentIndex_,
                        dashboardCarouselFocused_, false);
    if (storeGridPageBuffer()) {
      gridBufferBuilding_ = true;
    } else {
      recentWidget.render(mode, 0, top(), renderer.getScreenWidth(), contentHeight, recentIndex_,
                          dashboardCarouselFocused_, true);
    }
  } else {
    invalidateGridPageBuffer();
    recentWidget.render(mode, 0, top(), renderer.getScreenWidth(), contentHeight, recentIndex_,
                        dashboardCarouselFocused_, true);
  }
  if (!recentPopupPath_.empty()) renderRecentPopup();
}

void Home::afterRender() {
  if (!gridBufferBuilding_) return;
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer || !gridPageBuffer_) {
    gridBufferBuilding_ = false;
    gridPageBufferValid_ = false;
    return;
  }

  memcpy(gridPageBuffer_, frameBuffer, renderer.getBufferSize());
  gridPageBufferValid_ = true;
  gridPageBufferBookCount_ = RECENT_BOOKS.getCount();
  const widget::Recent::Mode mode = widget::Recent::modeFromSetting(SETTINGS.recentLibraryMode);
  gridPageBufferPageSize_ = mode == widget::Recent::Mode::Grid ? kGrid3x2PageSize : kGrid2x2PageSize;
  gridPageBufferStartIndex_ = (recentIndex_ / gridPageBufferPageSize_) * gridPageBufferPageSize_;
  if (mode == widget::Recent::Mode::Grid) {
    widget::grid::Grid::renderSelection(renderer, 0, top(), renderer.getScreenWidth(), bottom() - top(), recentIndex_);
  } else {
    widget::grid2x2::Grid2x2::renderSelection(renderer, 0, top(), renderer.getScreenWidth(), bottom() - top(),
                                               recentIndex_);
  }
  gridBufferBuilding_ = false;
}

bool Home::storeGridPageBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) return false;

  if (!gridPageBuffer_) {
    gridPageBuffer_ = static_cast<uint8_t*>(std::malloc(renderer.getBufferSize()));
    if (!gridPageBuffer_) return false;
  }
  memcpy(gridPageBuffer_, frameBuffer, renderer.getBufferSize());
  return true;
}

bool Home::storeShortcutPageBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) return false;

  if (!gridPageBuffer_) {
    gridPageBuffer_ = static_cast<uint8_t*>(std::malloc(renderer.getBufferSize()));
    if (!gridPageBuffer_) return false;
  }
  memcpy(gridPageBuffer_, frameBuffer, renderer.getBufferSize());
  return true;
}

bool Home::restoreShortcutPageBuffer() {
  if (!shortcutPageBufferValid_ || !gridPageBuffer_) return false;

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) return false;
  memcpy(frameBuffer, gridPageBuffer_, renderer.getBufferSize());
  return true;
}

void Home::invalidateShortcutPageBuffer() {
  shortcutPageBufferValid_ = false;
}

bool Home::tryFastGridSelection(const int nextIndex) {
  const widget::Recent::Mode mode = widget::Recent::modeFromSetting(SETTINGS.recentLibraryMode);
  const auto& books = RECENT_BOOKS.getBooks();
  const bool is3x2Grid = mode == widget::Recent::Mode::Grid;
  const bool is2x2Grid = mode == widget::Recent::Mode::Grid2x2;
  const int pageSize = is3x2Grid ? kGrid3x2PageSize : kGrid2x2PageSize;
  const int currentPageStart = (recentIndex_ / pageSize) * pageSize;
  const int nextPageStart = (nextIndex / pageSize) * pageSize;
  if ((!is3x2Grid && !is2x2Grid) || tabSelectorIndex != 0 || sidebarOpen || !recentPopupPath_.empty() ||
      !gridPageBufferValid_ || !gridPageBuffer_ || gridPageBufferBookCount_ != static_cast<int>(books.size()) ||
      gridPageBufferPageSize_ != pageSize || gridPageBufferStartIndex_ != currentPageStart ||
      currentPageStart != nextPageStart) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) return false;
  memcpy(frameBuffer, gridPageBuffer_, renderer.getBufferSize());
  if (is3x2Grid) {
    widget::grid::Grid::renderSelection(renderer, 0, top(), renderer.getScreenWidth(), bottom() - top(), nextIndex);
  } else {
    widget::grid2x2::Grid2x2::renderSelection(renderer, 0, top(), renderer.getScreenWidth(), bottom() - top(),
                                               nextIndex);
  }
  renderer.displayBuffer();
  return true;
}

void Home::invalidateGridPageBuffer() {
  gridPageBufferValid_ = false;
  gridBufferBuilding_ = false;
  gridPageBufferBookCount_ = -1;
  gridPageBufferStartIndex_ = -1;
  gridPageBufferPageSize_ = -1;
}

bool Home::recentPopupInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    recentPopupPath_.clear();
    recentPopupAction_ = 0;
    invalidateGridPageBuffer();
    ignoreBackReleaseAfterPopup_ = true;
    requestRender();
    return true;
  }
  if (mappedInput.wasPressed(itemPrevButton())) {
    recentPopupAction_ = std::max(0, recentPopupAction_ - 1);
    requestRender();
    return true;
  }
  if (mappedInput.wasPressed(itemNextButton())) {
    recentPopupAction_ = std::min(2, recentPopupAction_ + 1);
    requestRender();
    return true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (recentPopupAction_ == 0) {
      removeSelectedRecent();
    } else if (recentPopupAction_ == 1) {
      deleteSelectedRecentCache();
    } else {
      const std::string path = recentPopupPath_;
      recentPopupPath_.clear();
      recentPopupAction_ = 0;
      onGoToDescription(path);
      return true;
    }
    recentPopupPath_.clear();
    recentPopupAction_ = 0;
    requestRender();
    return true;
  }
  return false;
}

void Home::renderRecentPopup() const {
  const std::vector<std::string> actions = {"Remove Recent", "Delete cache", "View description"};
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(actions.size()), top());
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, "Book");
  PopUp::list(renderer, box, actions, recentPopupAction_, 0);
  PopUp::border(renderer, box);
}

void Home::removeSelectedRecent() {
  if (!recentPopupPath_.empty()) RECENT_BOOKS.removeBook(recentPopupPath_);
  const int count = RECENT_BOOKS.getCount();
  recentIndex_ = count <= 0 ? 0 : std::min(recentIndex_, count - 1);
}

void Home::deleteSelectedRecentCache() {
  if (recentPopupPath_.empty()) return;
  const auto& books = RECENT_BOOKS.getBooks();
  const auto it = std::find_if(books.begin(), books.end(), [&](const RecentBook& book) {
    return book.path == recentPopupPath_;
  });
  if (it == books.end()) return;

  const std::string path = cachePath(*it);
  if (SdMan.exists(path.c_str())) {
    int removed = 0;
    removeTree(path, removed);
  }
}

void Home::navigateToSelectedMenu() {
  switch (tabSelectorIndex) {
    case 1:
      onGoToLibrary("/");
      break;
    case 2:
      onGoToSettings();
      break;
    case 3:
      onGoToFileTransfer();
      break;
    case 4:
      requestRender();
      break;
    default:
      requestRender();
      break;
  }
}
