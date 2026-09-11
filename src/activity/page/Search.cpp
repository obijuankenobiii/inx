#include "Search.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include <algorithm>

#include "components/search/SearchText.h"
#include "images/BookSmall.h"
#include "images/Folder.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"
#include "system/UiLayout.h"

extern void onGoToRecent();
extern void onGoToLibrary(const std::string& path);
extern void onSelectBook(const std::string& path);

namespace {
constexpr unsigned long kKeyboardToggleHoldMs = 500;
}

Search::Search(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> returnToCaller)
    : Page("Search", renderer, mappedInput), returnToCaller_(std::move(returnToCaller)) {
  results_.reserve(kMaxResults);
}

void Search::onEnter() {
  Page::onEnter();
  query_.clear();
  results_.clear();
  keyboard_.reset();
  selectedResult_ = 0;
  scroll_ = 0;
  keyboardCollapsed_ = false;
  nextButtonActive_ = false;
  nextButtonLongFired_ = false;
  nextButtonPressStartedMs_ = 0;
}

int Search::resultHeight() const {
  // The X3 portrait panel is 792px high. Once the keyboard is hidden, one
  // pixel of row compression lets all 10 bounded results fit below the
  // search field without reserving the Pro keyboard-control row.
  return keyboardCollapsed_ ? UiLayout::LIST_ITEM_HEIGHT - 1 : UiLayout::LIST_ITEM_HEIGHT;
}

int Search::keyboardTop() const {
  // There is no keyboard-show control on the button-only search page. When
  // the keyboard is collapsed, library results may use the entire display.
  if (keyboardCollapsed_) return renderer.getScreenHeight();
  return renderer.getScreenHeight() - keyboard_.height(renderer);
}

int Search::resultsTop() const { return SearchText::top() + SearchText::height + 5; }

int Search::maxScroll() const {
  const int visible = std::max(0, keyboardTop() - resultsTop());
  const int total = static_cast<int>(results_.size()) * resultHeight();
  return std::max(0, total - visible);
}

void Search::keepSelectedVisible() {
  if (results_.empty()) return;

  const int rowHeight = resultHeight();
  const int visibleRows = std::max(1, (keyboardTop() - resultsTop()) / rowHeight);
  const int firstVisible = scroll_ / rowHeight;
  const int lastVisible = firstVisible + visibleRows - 1;
  const int selected = static_cast<int>(selectedResult_);

  if (selected < firstVisible) {
    scroll_ = selected * rowHeight;
  } else if (selected > lastVisible) {
    scroll_ = (selected - visibleRows + 1) * rowHeight;
  }
  scroll_ = std::min(maxScroll(), std::max(0, scroll_));
}

void Search::updateResults() {
  if (query_.empty()) {
    results_.clear();
  } else {
    // LibraryIndex::search(limit) retains only this bounded sorted window while
    // scanning the SD card; it never builds a full duplicate library here.
    LibraryIndex::search(query_, results_, kMaxResults);
  }
  selectedResult_ = 0;
  scroll_ = 0;
  requestRender();
}

void Search::openSelected() {
  if (results_.empty() || selectedResult_ >= results_.size()) return;
  const LibraryIndex::Book& result = results_[selectedResult_];
  if (result.type == LibraryIndex::Book::Type::FOLDER) {
    onGoToLibrary(result.path);
  } else {
    onSelectBook(result.path);
  }
}

void Search::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    back();
    return;
  }

  const auto nextButton = itemNextButton();
  const bool nextPressed = mappedInput.isPressed(nextButton);
  const unsigned long now = millis();
  if (nextPressed && !nextButtonActive_) {
    nextButtonActive_ = true;
    nextButtonLongFired_ = false;
    nextButtonPressStartedMs_ = now;
    return;
  }

  if (nextPressed && nextButtonActive_ && !nextButtonLongFired_ &&
      now - nextButtonPressStartedMs_ >= kKeyboardToggleHoldMs) {
    // Match ReaderButtonBindings: long action fires while held and suppresses
    // the short action that would otherwise fire on release.
    nextButtonLongFired_ = true;
    keyboardCollapsed_ = !keyboardCollapsed_;
    if (keyboardCollapsed_) {
      selectedResult_ = 0;
      scroll_ = 0;
    } else {
      keepSelectedVisible();
    }
    requestRender();
    return;
  }

  if (!nextPressed && nextButtonActive_) {
    const bool shortPress = !nextButtonLongFired_;
    nextButtonActive_ = false;
    nextButtonLongFired_ = false;
    if (shortPress) {
      if (keyboardCollapsed_) {
        if (!results_.empty()) {
          selectedResult_ = (selectedResult_ + 1) % results_.size();
          keepSelectedVisible();
        }
      } else {
        keyboard_.moveVertical(1);
      }
      requestRender();
    }
    return;
  }

  if (keyboardCollapsed_) {
    if (!results_.empty() && mappedInput.wasPressed(itemPrevButton())) {
      selectedResult_ = selectedResult_ == 0 ? results_.size() - 1 : selectedResult_ - 1;
      keepSelectedVisible();
      requestRender();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      openSelected();
      return;
    }
    // Back is intentionally handled above as the page close action, matching
    // the Pro search page rather than trapping the user in the result list.
    renderIfNeeded();
    return;
  }

  if (mappedInput.wasPressed(tabPrevButton())) {
    keyboard_.moveHorizontal(-1);
    requestRender();
    return;
  }
  if (mappedInput.wasPressed(tabNextButton())) {
    keyboard_.moveHorizontal(1);
    requestRender();
    return;
  }
  if (mappedInput.wasPressed(itemPrevButton())) {
    keyboard_.moveVertical(-1);
    requestRender();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    const SearchKeyboard::Action action = keyboard_.activate(query_);
    if (action == SearchKeyboard::Action::Collapse) {
      keyboardCollapsed_ = true;
      selectedResult_ = 0;
      scroll_ = 0;
    } else {
      updateResults();
      if (action == SearchKeyboard::Action::Go) {
        if (!results_.empty()) openSelected();
        return;
      }
    }
    requestRender();
    return;
  }

  renderIfNeeded();
}

void Search::title() const {
  ScreenComponents::drawSubPageHeader(renderer, name());
}

void Search::menu() {
  // Search is a focused sub-page in Pro: keep the keyboard clear of the main
  // bottom navigation and retain only the page header.
  title();
}

bool Search::back() {
  if (returnToCaller_) {
    returnToCaller_();
  } else {
    onGoToRecent();
  }
  return true;
}

void Search::content() {
  SearchText::render(renderer, query_);
  if (!query_.empty()) drawResults();

  if (!keyboardCollapsed_) {
    keyboard_.render(renderer, keyboardTop(), renderer.getScreenHeight());
  }

}

void Search::drawResults() const {
  const int top = resultsTop();
  const int bottom = keyboardTop();
  const int width = renderer.getScreenWidth();
  const int font = systemFontId();
  const int currentScroll = std::min(maxScroll(), std::max(0, scroll_));

  if (results_.empty()) {
    const char* message = LibraryIndex::hasIndex() ? "No matching books" : "Build the library index first";
    renderer.text.centered(font, top + 18, message);
    return;
  }

  for (size_t index = 0; index < results_.size(); ++index) {
    const int y = top + static_cast<int>(index) * resultHeight() - currentScroll;
    if (y < top || y + resultHeight() > bottom) continue;

    const bool selected = index == selectedResult_ && keyboardCollapsed_;
    if (selected) renderer.rectangle.fill(0, y, width, resultHeight(), true);

    constexpr int iconSize = 24;
    const bool inverted = selected;
    if (results_[index].type == LibraryIndex::Book::Type::FOLDER) {
      renderer.bitmap.icon(Folder, 25, y + (resultHeight() - iconSize) / 2, iconSize, iconSize,
                           BitmapRender::Orientation::None, inverted);
    } else {
      renderer.bitmap.icon(BookSmall, 25, y + (resultHeight() - iconSize) / 2, iconSize, iconSize,
                           BitmapRender::Orientation::None, inverted);
    }

    const int textX = 65;
    const int available = std::max(40, width - textX - 20);
    const std::string title = renderer.text.truncate(font, results_[index].title.c_str(), available);
    const int textY = y + (resultHeight() - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, textX, textY, title.c_str(), !selected);
    if (index + 1 < results_.size()) {
      renderer.line.render(20, y + resultHeight() - 1, width - 20, y + resultHeight() - 1, true,
                           LineRender::Style::Dotted);
    }
  }
}
