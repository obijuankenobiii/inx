#include "FontManagerActivity.h"

#include <GfxRenderer.h>
#include <WiFi.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "activity/page/SubPage.h"
#include "activity/network/WifiSelectionActivity.h"
#include "activity/page/components/global/Button.h"
#include "activity/page/components/global/PopUp.h"
#include "images/Check.h"
#include "images/Download.h"
#include "images/LibraryFilterRight.h"
#include "state/ReaderSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/StringUtils.h"

namespace {
constexpr uint32_t kDisplayTaskStack = 4096;
constexpr uint32_t kInstallTaskStack = 8192;
constexpr int kBottomMargin = 44;
constexpr int kSideMargin = 20;
constexpr int kActionIconSize = 40;
constexpr int kScrollCaretSize = 40;
constexpr int kTabWidth = 120;
constexpr int kTabHeight = Button::height - 10;
constexpr int kCategoryFilterWidth = 150;
constexpr int kCategoryFilterRowHeight = UiLayout::LIST_ITEM_HEIGHT;

int pageBodyTop() { return 80; }

bool contains(const ButtonBounds& bounds, const int x, const int y) {
  return x >= bounds.x && x < bounds.x + bounds.width && y >= bounds.y && y < bounds.y + bounds.height;
}

std::string displayFontName(const std::string& name) {
  std::string result;
  result.reserve(name.size() + 8);
  for (size_t i = 0; i < name.size(); ++i) {
    const unsigned char current = static_cast<unsigned char>(name[i]);
    if (current == '-' || current == '_') {
      if (!result.empty() && result.back() != ' ') result.push_back(' ');
      continue;
    }

    const unsigned char previous = i > 0 ? static_cast<unsigned char>(name[i - 1]) : 0;
    const unsigned char next = i + 1 < name.size() ? static_cast<unsigned char>(name[i + 1]) : 0;
    const bool startsWord = std::isupper(current) &&
                            (std::islower(previous) || std::isdigit(previous) ||
                             (std::isupper(previous) && std::islower(next)));
    const bool startsNumber = std::isdigit(current) && std::isalpha(previous);
    if ((startsWord || startsNumber) && !result.empty() && result.back() != ' ') result.push_back(' ');
    result.push_back(static_cast<char>(current));
  }
  return result;
}

ButtonBounds actionBounds(const GfxRenderer& renderer) {
  constexpr int width = 180;
  return {(renderer.getScreenWidth() - width) / 2, renderer.getScreenHeight() - kBottomMargin - Button::height, width,
          Button::height};
}

void drawProgressBar(const GfxRenderer& renderer, const int x, const int y, const int width, const int height,
                     const int percent) {
  renderer.rectangle.render(x, y, width, height, true);
  const int innerWidth = std::max(1, width - 2);
  renderer.rectangle.fill(x + 1, y + 1, innerWidth, height - 2, false);
  const int fillWidth = std::max(0, std::min(innerWidth, innerWidth * percent / 100));
  if (fillWidth > 0) renderer.rectangle.fill(x + 1, y + 1, fillWidth, height - 2, true);
}

void drawScrollBar(const GfxRenderer& renderer, const int x, const int y, const int height, const int total,
                   const int visible, const int offset) {
  if (total <= visible || height <= 0) return;

  constexpr int width = 3;
  const int maxOffset = std::max(1, total - visible);
  const int thumbHeight = std::max(14, height * visible / total);
  const int thumbTravel = std::max(1, height - thumbHeight);
  const int thumbY = y + offset * thumbTravel / maxOffset;
  renderer.rectangle.fill(x, y, width, height, static_cast<int>(GfxRenderer::FillTone::Gray), true);
  renderer.rectangle.fill(x, thumbY, width, thumbHeight, static_cast<int>(GfxRenderer::FillTone::Ink), true);
}

ButtonBounds scrollCaretBounds(const GfxRenderer& renderer) {
  return {renderer.getScreenWidth() - kSideMargin - kScrollCaretSize, renderer.getScreenHeight() - kBottomMargin + 2,
          kScrollCaretSize, kScrollCaretSize};
}

void renderVariantTab(const GfxRenderer& renderer, const int x, const int y, const char* label, const bool selected,
                      const bool roundLeft, const bool roundRight) {
  constexpr int corner = 4;
  const int tone = selected ? static_cast<int>(GfxRenderer::FillTone::Ink)
                            : static_cast<int>(GfxRenderer::FillTone::Paper);
  renderer.rectangle.fill(x, y, kTabWidth, kTabHeight, tone, false);
  renderer.rectangle.render(x, y, kTabWidth, kTabHeight, true, false);

  auto clearOutsideCorner = [&](const int cornerX, const int cornerY, const bool left, const bool top) {
    for (int dy = 0; dy <= corner; ++dy) {
      for (int dx = 0; dx <= corner; ++dx) {
        const int distanceX = left ? dx - corner : dx;
        const int distanceY = top ? dy - corner : dy;
        if (distanceX * distanceX + distanceY * distanceY > corner * corner) {
          renderer.drawPixel(cornerX + dx, cornerY + dy, false);
        }
      }
    }
    for (int offset = 0; offset <= corner; ++offset) {
      const int span = static_cast<int>(std::sqrt(corner * corner - (corner - offset) * (corner - offset)));
      const int arcX = left ? cornerX + corner - span : cornerX + span;
      const int arcY = top ? cornerY + offset : cornerY + corner - offset;
      renderer.drawPixel(arcX, arcY, true);
    }
  };

  if (roundLeft) {
    clearOutsideCorner(x, y, true, true);
    clearOutsideCorner(x, y + kTabHeight - corner - 1, true, false);
  }
  if (roundRight) {
    clearOutsideCorner(x + kTabWidth - corner - 1, y, false, true);
    clearOutsideCorner(x + kTabWidth - corner - 1, y + kTabHeight - corner - 1, false, false);
  }

  const int font = systemFontId();
  const int textWidth = renderer.text.getWidth(font, label);
  const int textY = y + (kTabHeight - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, x + (kTabWidth - textWidth) / 2, textY, label, !selected, EpdFontFamily::REGULAR);
}
}

void FontManagerActivity::displayTaskTrampoline(void* param) {
  static_cast<FontManagerActivity*>(param)->displayTaskLoop();
}

void FontManagerActivity::installTaskTrampoline(void* param) {
  static_cast<FontManagerActivity*>(param)->installTaskLoop();
}

void FontManagerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  state_ = State::Ready;
  status_.clear();
  selectedIndex_ = 0;
  scrollOffset_ = 0;
  selectedVisible_ = false;
  categoryFilterOpen_ = false;
  categoryFilter_ = CategoryFilter::All;
  packages_.clear();
  activeVariant_ = 1;
  installingPackageIndex_ = -1;
  installedPopupOpen_ = false;
  installedPopupPackageIndex_ = -1;
  installedPopupActionIndex_ = 0;
  progressDownloaded_ = 0;
  progressTotal_ = 0;
  updateRequired_ = false;
  shuttingDown_ = false;
  lastProgressPercent_ = -1;
  lastProgressUpdateMs_ = 0;
  loadPackages();
}

void FontManagerActivity::onExit() {
  shuttingDown_ = true;

  const unsigned long start = millis();
  while (installTaskHandle_ && millis() - start < 1500) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  ActivityWithSubactivity::onExit();

  if (installTaskHandle_) {
    vTaskDelete(installTaskHandle_);
    installTaskHandle_ = nullptr;
  }
  if (displayTaskHandle_) {
    vTaskDelete(displayTaskHandle_);
    displayTaskHandle_ = nullptr;
  }
  if (renderingMutex_) {
    vSemaphoreDelete(renderingMutex_);
    renderingMutex_ = nullptr;
  }
}

void FontManagerActivity::loadPackages() {
  std::string error;
  if (FontPackageManager::fetchAvailable(packages_, error)) {
    state_ = State::Ready;
    status_ = packages_.empty() ? "No font packages found." : "Select a font, then press Confirm.";
  } else {
    state_ = State::Failed;
    status_ = error.empty() ? "Could not load font packages." : error;
  }
  updateDisplay();
}

int FontManagerActivity::visibleRowCount(const int bodyTop) const {
  return std::max(1, (renderer.getScreenHeight() - kBottomMargin - bodyTop) / kRowHeight);
}

int FontManagerActivity::visiblePackageCount() const {
  int count = 0;
  for (const FontPackageManager::Package& package : packages_) {
    if (package.variant == activeVariantName() && matchesCategory(package)) ++count;
  }
  return count;
}

int FontManagerActivity::packageIndexAt(const int visibleIndex) const {
  if (visibleIndex < 0) return -1;
  int index = 0;
  for (int packageIndex = 0; packageIndex < static_cast<int>(packages_.size()); ++packageIndex) {
    const FontPackageManager::Package& package = packages_[static_cast<size_t>(packageIndex)];
    if (package.variant != activeVariantName() || !matchesCategory(package)) continue;
    if (index == visibleIndex) return packageIndex;
    ++index;
  }
  return -1;
}

void FontManagerActivity::selectVariant(const int variant) {
  if (variant < 0 || variant > 1 || variant == activeVariant_) return;
  activeVariant_ = variant;
  selectedIndex_ = 0;
  scrollOffset_ = 0;
  selectedVisible_ = false;
  updateDisplay();
}

int FontManagerActivity::categoryFilterCount() const { return 3; }

const char* FontManagerActivity::categoryFilterLabel() const {
  switch (categoryFilter_) {
    case CategoryFilter::SansSerif:
      return "Sans Serif";
    case CategoryFilter::Serif:
      return "Serif";
    case CategoryFilter::All:
    default:
      return "All";
  }
}

bool FontManagerActivity::matchesCategory(const FontPackageManager::Package& package) const {
  if (categoryFilter_ == CategoryFilter::All) return true;
  const auto category = categoryFilter_ == CategoryFilter::Serif ? FontPackageManager::Category::Serif
                                                                  : FontPackageManager::Category::SansSerif;
  return package.category == category;
}

ButtonBounds FontManagerActivity::categoryFilterBounds() const {
  return {kSideMargin, pageBodyTop(), kCategoryFilterWidth, kTabHeight};
}

void FontManagerActivity::categoryFilterDropdown() const {
  const int x = kSideMargin;
  const int y = pageBodyTop() + kTabHeight;
  const int height = categoryFilterCount() * kCategoryFilterRowHeight + 1;
  const char* labels[] = {"All", "Sans Serif", "Serif"};
  const int font = systemFontId();

  renderer.rectangle.fill(x, y, kCategoryFilterWidth, height, false);
  for (int index = 0; index < categoryFilterCount(); ++index) {
    const int rowY = y + index * kCategoryFilterRowHeight;
    const int textY = rowY + (kCategoryFilterRowHeight - renderer.text.getLineHeight(font)) / 2;
    renderer.text.render(font, x + 20, textY, labels[index], true, EpdFontFamily::REGULAR);
    if (index + 1 < categoryFilterCount()) {
      renderer.line.render(x + 10, rowY + kCategoryFilterRowHeight, x + kCategoryFilterWidth - 10,
                           rowY + kCategoryFilterRowHeight, true, LineRender::Style::Dotted);
    }
  }
  renderer.rectangle.render(x, y, kCategoryFilterWidth, height, true);
}

void FontManagerActivity::handleCategoryFilterTap(const int tapX, const int tapY) {
  const int x = kSideMargin;
  const int y = pageBodyTop() + kTabHeight;
  const int height = categoryFilterCount() * kCategoryFilterRowHeight + 1;
  if (tapX >= x && tapX < x + kCategoryFilterWidth && tapY >= y && tapY < y + height) {
    applyCategoryFilter((tapY - y) / kCategoryFilterRowHeight);
  } else {
    categoryFilterOpen_ = false;
    updateDisplay();
  }
}

void FontManagerActivity::applyCategoryFilter(const int index) {
  if (index < 0 || index >= categoryFilterCount()) return;
  categoryFilter_ = static_cast<CategoryFilter>(index);
  selectedIndex_ = 0;
  scrollOffset_ = 0;
  selectedVisible_ = false;
  categoryFilterOpen_ = false;
  updateDisplay();
}

void FontManagerActivity::installSelected() {
  const int packageIndex = packageIndexAt(selectedIndex_);
  if (packageIndex < 0) return;

  if (FontPackageManager::isInstalled(packages_[static_cast<size_t>(packageIndex)])) {
    openInstalledPopup();
    return;
  }

  if (WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    launchWifiSelection();
    return;
  }

  state_ = State::Downloading;
  status_ = "Downloading and installing...";
  installingPackageIndex_ = packageIndex;
  progressDownloaded_ = 0;
  progressTotal_ = 0;
  lastProgressPercent_ = -1;
  lastProgressUpdateMs_ = 0;
  startInstallation();
}

void FontManagerActivity::removeSelected() {
  const int packageIndex = installedPopupPackageIndex_ >= 0 ? installedPopupPackageIndex_ : packageIndexAt(selectedIndex_);
  if (packageIndex < 0) return;

  const FontPackageManager::Package& package = packages_[static_cast<size_t>(packageIndex)];
  if (!FontPackageManager::isInstalled(package)) return;

  const std::string family = StringUtils::sanitizeFilename(package.installFamily, 48);
  const std::vector<std::string> families = FontManager::readerFontFamilyEnumLabels();
  const bool wasSelectedReaderFont = READER_SETTINGS.fontFamily < families.size() &&
                                     families[READER_SETTINGS.fontFamily] == family;
  std::string error;
  if (!FontPackageManager::remove(package, error)) {
    status_ = error.empty() ? "Font removal failed." : error;
    installedPopupOpen_ = false;
    installedPopupPackageIndex_ = -1;
    updateDisplay();
    return;
  }

  if (wasSelectedReaderFont) {
    READER_SETTINGS.fontFamily = SystemSetting::MONTSERRAT;
    READER_SETTINGS.saveToFile();
  }
  installedPopupOpen_ = false;
  installedPopupPackageIndex_ = -1;
  selectedVisible_ = false;
  status_ = "Font removed.";
  updateDisplay();
}

void FontManagerActivity::openInstalledPopup() {
  const int packageIndex = packageIndexAt(selectedIndex_);
  if (packageIndex < 0) return;
  installedPopupPackageIndex_ = packageIndex;
  installedPopupActionIndex_ = 0;
  installedPopupOpen_ = true;
  updateDisplay();
}

bool FontManagerActivity::handleInstalledPopupInput() {
  if (!installedPopupOpen_) return false;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    installedPopupOpen_ = false;
    installedPopupPackageIndex_ = -1;
    updateDisplay();
    return true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    installedPopupActionIndex_ = (installedPopupActionIndex_ + 1) % 2;
    updateDisplay();
    return true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    installedPopupActionIndex_ = (installedPopupActionIndex_ + 1) % 2;
    updateDisplay();
    return true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (installedPopupActionIndex_ == 0) {
      removeSelected();
    } else {
      installedPopupOpen_ = false;
      installedPopupPackageIndex_ = -1;
      updateDisplay();
    }
    return true;
  }
  return true;
}

void FontManagerActivity::renderInstalledPopup() const {
  if (!installedPopupOpen_ || installedPopupPackageIndex_ < 0 ||
      installedPopupPackageIndex_ >= static_cast<int>(packages_.size())) {
    return;
  }
  const std::vector<std::string> actions = {"Delete", "Cancel"};
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(actions.size()));
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, displayFontName(packages_[static_cast<size_t>(installedPopupPackageIndex_)].name));
  PopUp::list(renderer, box, actions, installedPopupActionIndex_, 0);
  PopUp::border(renderer, box);
}

void FontManagerActivity::startInstallation() {
  if (installTaskHandle_ || installingPackageIndex_ < 0 || shuttingDown_) return;

  if (!renderingMutex_) renderingMutex_ = xSemaphoreCreateMutex();
  if (!renderingMutex_) {
    state_ = State::Failed;
    status_ = "Could not start download.";
    updateDisplay();
    return;
  }

  if (!displayTaskHandle_ &&
      xTaskCreatePinnedToCore(&FontManagerActivity::displayTaskTrampoline, "FontDisplayTask", kDisplayTaskStack,
                              this, 1, &displayTaskHandle_, 1) != pdPASS) {
    state_ = State::Failed;
    status_ = "Could not start download.";
    updateDisplay();
    return;
  }

  updateRequired_ = true;
  if (xTaskCreatePinnedToCore(&FontManagerActivity::installTaskTrampoline, "FontInstallTask", kInstallTaskStack, this,
                              1, &installTaskHandle_, 0) != pdPASS) {
    installTaskHandle_ = nullptr;
    state_ = State::Failed;
    status_ = "Could not start download.";
    updateRequired_ = true;
  }
}

void FontManagerActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired_) {
      updateRequired_ = false;
      if (renderingMutex_ && xSemaphoreTake(renderingMutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        render();
        xSemaphoreGive(renderingMutex_);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void FontManagerActivity::installTaskLoop() {
  const int packageIndex = installingPackageIndex_;
  std::string error;
  bool installed = false;
  if (packageIndex >= 0 && packageIndex < static_cast<int>(packages_.size())) {
    installed = FontPackageManager::install(
        packages_[static_cast<size_t>(packageIndex)], error,
        [this](const size_t downloaded, const size_t total) {
          progressDownloaded_ = downloaded;
          progressTotal_ = total;
          const int percent = total > 0 ? static_cast<int>((downloaded * 100) / total) : 0;
          const unsigned long now = millis();
          if (percent != lastProgressPercent_ || now - lastProgressUpdateMs_ >= 750) {
            lastProgressPercent_ = percent;
            lastProgressUpdateMs_ = now;
            updateRequired_ = true;
          }
        });
  }

  if (!shuttingDown_) {
    if (installed) {
      state_ = State::Ready;
      status_ = "Font installed.";
      selectedVisible_ = false;
    } else {
      state_ = State::Failed;
      status_ = error.empty() ? "Font installation failed." : error;
    }
    updateRequired_ = true;
  }
  installTaskHandle_ = nullptr;
  vTaskDelete(nullptr);
}

void FontManagerActivity::updateDisplay() {
  if (displayTaskHandle_) {
    updateRequired_ = true;
    return;
  }

  render();
}

void FontManagerActivity::launchWifiSelection() {
  enterNewActivity(new WifiSelectionActivity(renderer, mappedInput,
                                             [this](const bool connected) { onWifiSelectionComplete(connected); }));
}

void FontManagerActivity::onWifiSelectionComplete(const bool connected) {
  exitActivity();

  if (!connected || WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    state_ = State::Failed;
    status_ = "Wi-Fi connection failed. Tap Retry.";
    updateDisplay();
    return;
  }

  installSelected();
}

void FontManagerActivity::render() {
  renderer.clearScreen();
  const int bodyTop = SubPage::header(renderer, "Font Manager");
  const int fontListTop = listTop(bodyTop);
  const int font = systemFontId();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();

  if (state_ == State::Downloading) {
    const int centerY = bodyTop + (screenH - bodyTop - 80) / 2;
    renderer.text.centered(font, centerY - 72, "DOWNLOADING FONT", true, EpdFontFamily::BOLD);
    const int packageIndex = installingPackageIndex_;
    const std::string packageName = packageIndex >= 0 && packageIndex < static_cast<int>(packages_.size())
                                        ? displayFontName(packages_[static_cast<size_t>(packageIndex)].name)
                                        : "Font";
    const std::string name = renderer.text.truncate(font, packageName.c_str(), screenW - 60);
    renderer.text.centered(font, centerY - 34, name.c_str(), true, EpdFontFamily::BOLD);
    renderer.text.centered(font, centerY + 4, "Installing font package", true, EpdFontFamily::REGULAR);

    const size_t downloaded = progressDownloaded_;
    const size_t total = progressTotal_;
    const int percent = total > 0 ? std::max(0, std::min(100, static_cast<int>((downloaded * 100) / total))) : 0;
    char percentText[8];
    std::snprintf(percentText, sizeof(percentText), "%d%%", percent);
    constexpr int barHeight = 6;
    constexpr int barWidth = 260;
    const int percentWidth = renderer.text.getWidth(font, percentText);
    const int groupWidth = barWidth + 14 + percentWidth;
    const int groupX = (screenW - groupWidth) / 2;
    drawProgressBar(renderer, groupX, centerY + 36, barWidth, barHeight, percent);
    renderer.text.render(font, groupX + barWidth + 14,
                         centerY + 36 + (barHeight - renderer.text.getLineHeight(font)) / 2, percentText, true,
                         EpdFontFamily::REGULAR);
    mappedInput.mapLabels("\xC2\xAB Back", "", "", "");
    renderer.displayBuffer();
    return;
  }

  if (state_ == State::Ready && !packages_.empty()) {
    const int total = visiblePackageCount();
    const int visibleRows = visibleRowCount(fontListTop);
    const int maxScroll = std::max(0, total - visibleRows);
    scrollOffset_ = std::max(0, std::min(scrollOffset_, maxScroll));
    if (selectedVisible_) {
      if (selectedIndex_ < scrollOffset_) scrollOffset_ = selectedIndex_;
      if (selectedIndex_ >= scrollOffset_ + visibleRows) scrollOffset_ = selectedIndex_ - visibleRows + 1;
    }

    const int tabsX = screenW - kSideMargin - kTabWidth * 2;
    Button::render(renderer, categoryFilterBounds(), categoryFilterLabel(), categoryFilterOpen_, font);
    for (int tab = 0; tab < 2; ++tab) {
      const int tabX = tabsX + tab * kTabWidth;
      const bool selectedTab = tab == activeVariant_;
      const char* label = tab == 0 ? "1-bit" : "2-bit";
      renderVariantTab(renderer, tabX, bodyTop, label, selectedTab, tab == 0, tab == 1);
    }

    const int end = std::min(total, scrollOffset_ + visibleRows);
    for (int index = scrollOffset_; index < end; ++index) {
      const int packageIndex = packageIndexAt(index);
      if (packageIndex < 0) continue;
      const int y = fontListTop + (index - scrollOffset_) * kRowHeight;
      const bool selected = selectedVisible_ && index == selectedIndex_;
      if (selected) renderer.rectangle.fill(0, y, screenW, kRowHeight, static_cast<int>(GfxRenderer::FillTone::Ink));
      const int textY = y + (kRowHeight - renderer.text.getLineHeight(font)) / 2;
      const int actionIconX = screenW - kSideMargin - kActionIconSize;
      const int maxNameWidth = screenW - (kSideMargin * 2) - kActionIconSize - 20;
      const std::string displayName = displayFontName(packages_[static_cast<size_t>(packageIndex)].name);
      const std::string packageName = renderer.text.truncate(font, displayName.c_str(), maxNameWidth,
                                                              EpdFontFamily::REGULAR);
      const bool installed = FontPackageManager::isInstalled(packages_[static_cast<size_t>(packageIndex)]);
      if (installed && !selected) {
        renderer.text.render(font, kSideMargin, textY, packageName.c_str(), true, EpdFontFamily::REGULAR);
      } else {
        renderer.text.render(font, kSideMargin, textY, packageName.c_str(), !selected, EpdFontFamily::REGULAR);
      }
      const int iconY = y + (kRowHeight - kActionIconSize) / 2;
      if (installed) {
        renderer.bitmap.icon(Check, actionIconX, iconY, kActionIconSize, kActionIconSize,
                             BitmapRender::Orientation::None, selected);
      } else {
        renderer.bitmap.icon(Download, actionIconX, iconY, kActionIconSize, kActionIconSize,
                             BitmapRender::Orientation::None, selected);
      }
      if (index + 1 < end) {
        renderer.line.render(0, y + kRowHeight - 1, screenW, y + kRowHeight - 1, true, LineRender::Style::Dotted);
      }
    }
    drawScrollBar(renderer, screenW - 8, fontListTop, visibleRows * kRowHeight, total, visibleRows, scrollOffset_);
    const ButtonBounds caretBounds = scrollCaretBounds(renderer);
    const auto caretOrientation = scrollOffset_ >= maxScroll ? BitmapRender::Orientation::Rotate270CW
                                                               : BitmapRender::Orientation::Rotate90CW;
    renderer.bitmap.iconScaled(LibraryFilterRight, caretBounds.x, caretBounds.y, 30, 30, kScrollCaretSize,
                               kScrollCaretSize, caretOrientation);
    mappedInput.mapLabels("\xC2\xAB Back", "Download", "Up", "Down");
    if (categoryFilterOpen_) categoryFilterDropdown();
  } else {
    const int centerY = bodyTop + (screenH - bodyTop - 80) / 2;
    renderer.text.centered(font, centerY - 26, status_.c_str(), true, EpdFontFamily::BOLD);
    if (state_ == State::Failed) {
      Button::render(renderer, actionBounds(renderer), "Retry", true, font);
    }
    mappedInput.mapLabels("\xC2\xAB Back", state_ == State::Failed ? "Retry" : "", "", "");
  }
  renderInstalledPopup();
  renderer.displayBuffer();
}

void FontManagerActivity::loop() {
  if (subActivity) {
    ActivityWithSubactivity::loop();
    return;
  }

  if (installedPopupOpen_) {
    handleInstalledPopupInput();
    return;
  }

  if (SubPage::closeInput(renderer, mappedInput, goBack_, false)) return;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    goBack_();
    return;
  }
  if (state_ == State::Ready && !packages_.empty()) {
    const int total = visiblePackageCount();
    if (total == 0) return;
    // The X3/X4 has buttons rather than the Pro touch panel: Left/Right select the
    // visible 1-bit/2-bit package tab while Up/Down move through its font list.
    if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
      selectVariant(0);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
      selectVariant(1);
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
      if (selectedVisible_) {
        selectedIndex_ = (selectedIndex_ + 1) % total;
      }
      selectedVisible_ = true;
      updateDisplay();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
      selectedIndex_ = (selectedIndex_ + total - 1) % total;
      selectedVisible_ = true;
      updateDisplay();
      return;
    }
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      installSelected();
      return;
    }
  } else if (state_ == State::Failed && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    loadPackages();
  }
}
