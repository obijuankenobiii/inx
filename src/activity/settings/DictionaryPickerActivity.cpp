#include "DictionaryPickerActivity.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <algorithm>

#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"
#include "system/UiTheme.h"
#include "util/StringUtils.h"

namespace {
constexpr int kBodyFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
constexpr int kRowH = UiTheme::DRAWER_LIST_ITEM_HEIGHT;
constexpr const char* kDictionariesRoot = "/dictionaries";

/** A folder counts as a dictionary if it directly contains at least one .idx and one .dict file. */
bool folderLooksLikeDictionary(const std::string& folderPath) {
  FsFile dir = SdMan.open(folderPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return false;
  }
  bool hasIdx = false;
  bool hasDict = false;
  for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      char name[160] = {};
      file.getName(name, sizeof(name));
      if (StringUtils::checkFileExtension(std::string(name), ".idx")) {
        hasIdx = true;
      } else if (StringUtils::checkFileExtension(std::string(name), ".dict")) {
        hasDict = true;
      }
    }
    file.close();
    if (hasIdx && hasDict) {
      break;
    }
  }
  dir.close();
  return hasIdx && hasDict;
}
}  // namespace

void DictionaryPickerActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  scanDictionaryFolders();
  render();
}

void DictionaryPickerActivity::scanDictionaryFolders() {
  folders_.clear();
  selectedIndex_ = 0;
  scrollOffset_ = 0;

  FsFile dir = SdMan.open(kDictionariesRoot);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    return;
  }

  for (FsFile file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (file.isDirectory()) {
      char name[160] = {};
      file.getName(name, sizeof(name));
      const std::string folderPath = std::string(kDictionariesRoot) + "/" + name;
      if (folderLooksLikeDictionary(folderPath)) {
        folders_.push_back(name);
      }
    }
    file.close();
  }
  dir.close();

  std::sort(folders_.begin(), folders_.end());

  if (SETTINGS.dictionaryFolder[0] != '\0') {
    const auto it = std::find(folders_.begin(), folders_.end(), std::string(SETTINGS.dictionaryFolder));
    if (it != folders_.end()) {
      selectedIndex_ = static_cast<int>(std::distance(folders_.begin(), it));
    }
  }
}

void DictionaryPickerActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    goBack_();
    return;
  }

  const int total = static_cast<int>(folders_.size());
  if (total == 0) {
    return;
  }

  if (mappedInput.wasPressed(MenuNav::itemNext())) {
    selectedIndex_ = (selectedIndex_ + 1) % total;
    render();
    return;
  }
  if (mappedInput.wasPressed(MenuNav::itemPrev())) {
    selectedIndex_ = (selectedIndex_ + total - 1) % total;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    const std::string& chosen = folders_[static_cast<size_t>(selectedIndex_)];
    strncpy(SETTINGS.dictionaryFolder, chosen.c_str(), sizeof(SETTINGS.dictionaryFolder) - 1);
    SETTINGS.dictionaryFolder[sizeof(SETTINGS.dictionaryFolder) - 1] = '\0';
    SETTINGS.saveToFile();
    goBack_();
    return;
  }
}

void DictionaryPickerActivity::render() {
  renderer.clearScreen();
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int bodyTop = INX_THEME.drawPageHeader(renderer, "Choose dictionary");

  const int total = static_cast<int>(folders_.size());
  if (total == 0) {
    const int centerY = bodyTop + (screenH - bodyTop - 80) / 2;
    renderer.text.centered(kBodyFont, centerY, "No dictionaries found.", true, EpdFontFamily::BOLD);
    renderer.text.centered(kBodyFont, centerY + 32, "Put StarDict folders under /dictionaries/", true,
                           EpdFontFamily::REGULAR);
    const auto hints = mappedInput.mapLabels("\xC2\xAB Back", "", "", "");
    renderer.ui.buttonHints(kBodyFont, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
    renderer.displayBuffer();
    return;
  }

  const int listBottom = screenH - 44;
  const int visibleRows = std::max(1, (listBottom - bodyTop) / kRowH);
  if (selectedIndex_ < scrollOffset_) {
    scrollOffset_ = selectedIndex_;
  } else if (selectedIndex_ >= scrollOffset_ + visibleRows) {
    scrollOffset_ = selectedIndex_ - visibleRows + 1;
  }
  const int maxScroll = std::max(0, total - visibleRows);
  scrollOffset_ = std::max(0, std::min(scrollOffset_, maxScroll));
  const int endIndex = std::min(total, scrollOffset_ + visibleRows);

  for (int i = scrollOffset_; i < endIndex; ++i) {
    const int y = bodyTop + (i - scrollOffset_) * kRowH;
    const bool selected = i == selectedIndex_;
    const bool active = folders_[static_cast<size_t>(i)] == SETTINGS.dictionaryFolder;
    if (selected) {
      renderer.rectangle.fill(0, y, screenW, kRowH, static_cast<int>(GfxRenderer::FillTone::Ink));
    }
    const int titleY = y + (kRowH - renderer.text.getLineHeight(kBodyFont)) / 2;
    std::string label = folders_[static_cast<size_t>(i)];
    if (active) {
      label += "  \xE2\x9C\x93";  // checkmark on the currently active dictionary
    }
    renderer.text.render(kBodyFont, 20, titleY, label.c_str(), !selected, EpdFontFamily::REGULAR);
    renderer.line.render(0, y + kRowH - 1, screenW, y + kRowH - 1, true, LineRender::Style::Dotted);
  }

  const auto hints = mappedInput.mapLabels("\xC2\xAB Back", "Select", "Up", "Down");
  renderer.ui.buttonHints(kBodyFont, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  renderer.displayBuffer();
}
