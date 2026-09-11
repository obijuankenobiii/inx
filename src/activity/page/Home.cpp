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
#include <functional>
#include <string>

#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "components/global/PopUp.h"
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
    requestRender();
    return;
  }

  if (mappedInput.isPressed(MappedInputManager::Button::Back)) {
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
      recentIndex_ = (recentIndex_ + 1) % recentCount;
    }
    requestRender();
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
      recentIndex_ = (recentIndex_ + recentCount - 1) % recentCount;
    }
    requestRender();
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
  if (sidebarOpen) navigation::Sidebar::render(renderer, "Shortcuts", shortcutIndex_);
}

bool Home::shortcutInput() {
  if (mappedInput.wasPressed(itemPrevButton())) {
    shortcutIndex_ = (shortcutIndex_ + navigation::Sidebar::shortcutCount - 1) % navigation::Sidebar::shortcutCount;
    requestRender();
    return true;
  }
  if (mappedInput.wasPressed(itemNextButton())) {
    shortcutIndex_ = (shortcutIndex_ + 1) % navigation::Sidebar::shortcutCount;
    requestRender();
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

  recentWidget.render(widget::Recent::modeFromSetting(SETTINGS.recentLibraryMode), 0, top(), renderer.getScreenWidth(),
                      contentHeight, recentIndex_, dashboardCarouselFocused_);
  if (!recentPopupPath_.empty()) renderRecentPopup();
}

bool Home::recentPopupInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    recentPopupPath_.clear();
    recentPopupAction_ = 0;
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
      search();
      break;
    default:
      break;
  }
}
