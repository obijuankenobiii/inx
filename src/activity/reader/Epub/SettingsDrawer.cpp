/**
 * @file SettingsDrawer.cpp
 * @brief Definitions for SettingsDrawer.
 */

#include "SettingsDrawer.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "../../settings/ReaderFontSettingsDraw.h"
#include "state/ReaderPreset.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"

#define SETTINGS SystemSetting::getInstance()

constexpr int LIST_ITEM_HEIGHT = 60;

namespace {
constexpr int kDrawerListTop = 64;
constexpr int kDrawerListBottomPadding = 8;
constexpr int kDrawerHeaderTitleY = 10;
constexpr int kDrawerHeaderTagGap = 20;
constexpr int kDrawerHeaderDividerGap = 28;

bool isLandscapeReader(const GfxRenderer& gfx) {
  const auto o = gfx.getOrientation();
  return o == GfxRenderer::LandscapeClockwise || o == GfxRenderer::LandscapeCounterClockwise;
}

/** List selection: portrait uses Up/Down only so Left/Right stay for value edits (matches pre-drawer UX). */
bool readSettingsListPrev(const MappedInputManager& in, const GfxRenderer& r) {
  if (isLandscapeReader(r)) {
    return in.wasPressed(MappedInputManager::Button::Right);
  }
  return in.wasPressed(MappedInputManager::Button::Up);
}

bool readSettingsListNext(const MappedInputManager& in, const GfxRenderer& r) {
  if (isLandscapeReader(r)) {
    return in.wasPressed(MappedInputManager::Button::Left);
  }
  return in.wasPressed(MappedInputManager::Button::Down);
}

/** Portrait: Left/Right adjust values. Landscape: Down/Up (swap with list so value edits match device). */
bool readValueDecrease(const MappedInputManager& in, const GfxRenderer& r) {
  if (isLandscapeReader(r)) {
    return in.wasPressed(MappedInputManager::Button::Down);
  }
  return in.wasPressed(MappedInputManager::Button::Left);
}

bool readValueIncrease(const MappedInputManager& in, const GfxRenderer& r) {
  if (isLandscapeReader(r)) {
    return in.wasPressed(MappedInputManager::Button::Up);
  }
  return in.wasPressed(MappedInputManager::Button::Right);
}

}  

/**
 * @brief Constructs a new SettingsDrawer
 * @param renderer Reference to the graphics renderer
 * @param settings Reference to book settings to modify
 * @param onSettingsChanged Callback triggered when settings are changed
 */
SettingsDrawer::SettingsDrawer(GfxRenderer& renderer, BookSettings& settings, std::function<void()> onSettingsChanged)
    : renderer(renderer),
      settings(settings),
      onSettingsChanged(onSettingsChanged),
      lastInputTime(0),
      settingsUpdated(false) {
  itemHeight = LIST_ITEM_HEIGHT;
  syncLayoutFromRenderer();

  selectedIndex = 0;
  scrollOffset = 0;
  visible = false;
  dismissed = false;

  groupExpanded[GroupType::FONT] = false;
  groupExpanded[GroupType::LAYOUT] = false;
  groupExpanded[GroupType::IMAGE] = false;
  groupExpanded[GroupType::CONTROLS] = false;
  groupExpanded[GroupType::STATUS_BAR] = false;

  setupMenu();
}

/**
 * @brief Destructor
 */
SettingsDrawer::~SettingsDrawer() {}

void SettingsDrawer::setEmbeddedRegion(int x, int y, int w, int h) {
  embedded_ = true;
  drawerX = x;
  drawerY = y;
  drawerWidth = w;
  drawerHeight = h;
  itemsPerPage = std::max(1, (drawerHeight - kDrawerListTop - kDrawerListBottomPadding) / itemHeight);
}

int SettingsDrawer::snapEmbeddedHeight(int maxHeight) const {
  // Largest height <= maxHeight that fits a whole number of rows under the header, so the embedded
  // drawer has no dead space below the last row.
  const int usable = maxHeight - kDrawerListTop - kDrawerListBottomPadding;
  const int rows = std::max(1, usable / itemHeight);
  return kDrawerListTop + rows * itemHeight + kDrawerListBottomPadding;
}

void SettingsDrawer::syncLayoutFromRenderer() {
  if (embedded_) {
    // Keep the host-provided region; just recompute how many rows fit.
    itemsPerPage = std::max(1, (drawerHeight - kDrawerListTop - kDrawerListBottomPadding) / itemHeight);
    return;
  }
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  if (isLandscapeReader(renderer)) {
    drawerWidth = sw / 2;
    drawerX = sw - drawerWidth;
    drawerY = 0;
    drawerHeight = sh;
  } else {
    drawerX = 0;
    drawerWidth = sw;
    drawerHeight = sh * 60 / 100;
    drawerY = sh - drawerHeight;
  }
  itemsPerPage = std::max(1, (drawerHeight - kDrawerListTop - kDrawerListBottomPadding) / itemHeight);
}

/**
 * @brief Sets up the menu structure based on current expansion states
 */
void SettingsDrawer::setupMenu() {
  menuItems.clear();

  // Per-book preset picker (hidden inside the preset editor, where settings ARE the preset being edited).
  if (!embedded_) {
    MenuEntry presetEntry;
    presetEntry.item = MenuItem::PresetPicker;
    presetEntry.group = GroupType::FONT;
    presetEntry.name = "Apply preset";
    presetEntry.getValueText = [this](const BookSettings&) -> const char* {
      thread_local std::string tls;
      tls = ReaderPresetStore::getInstance().nameOf(presetPickIndex_);
      return tls.c_str();
    };
    presetEntry.change = [this](BookSettings&, int delta) {
      const int n = ReaderPresetStore::getInstance().count();
      if (n <= 0) return;
      presetPickIndex_ = ((presetPickIndex_ + delta) % n + n) % n;
    };
    menuItems.push_back(presetEntry);
  }

  MenuEntry fontSeparator;
  fontSeparator.item = MenuItem::Separator;
  fontSeparator.group = GroupType::FONT;
  fontSeparator.name = "═══ Font ═══";
  fontSeparator.getValueText = [this](const BookSettings&) -> const char* {
    static char indicator[4];
    snprintf(indicator, sizeof(indicator), "%s", groupExpanded[GroupType::FONT] ? "-" : "+");
    return indicator;
  };
  fontSeparator.change = [](BookSettings&, int) {};
  menuItems.push_back(fontSeparator);

  if (groupExpanded[GroupType::FONT]) {
    MenuEntry fontFamEntry;
    fontFamEntry.item = MenuItem::FontFamily;
    fontFamEntry.group = GroupType::FONT;
    fontFamEntry.name = "Style";
    fontFamEntry.getValueText = [](const BookSettings& s) -> const char* {
      thread_local std::string tls;
      tls = FontManager::readerFontFamilyLabel(s.fontFamily);
      return tls.c_str();
    };
    fontFamEntry.change = [](BookSettings& s, int delta) {
      const int n = static_cast<int>(FontManager::readerFontFamilyOptionCount());
      if (n <= 0) {
        return;
      }
      int newVal = static_cast<int>(s.fontFamily) + delta;
      if (newVal < 0) {
        newVal = n - 1;
      }
      if (newVal >= n) {
        newVal = 0;
      }
      s.fontFamily = static_cast<uint8_t>(newVal);
      FontManager::clampReaderFontFamilySlot(s.fontFamily);
      s.useCustomSettings = true;
    };
    menuItems.push_back(fontFamEntry);

    MenuEntry fontEntry;
    fontEntry.item = MenuItem::FontSize;
    fontEntry.group = GroupType::FONT;
    fontEntry.name = "Size";
    fontEntry.getValueText = [](const BookSettings& s) -> const char* {
      static const char* sizes[] = {"Extra Small", "Small", "Medium", "Large", "X Large"};
      int index = s.fontSize;
      if (index > 4) index = 1;
      return sizes[index];
    };
    fontEntry.change = [](BookSettings& s, int delta) {
      int newVal = s.fontSize + delta;
      if (newVal >= 0 && newVal <= 4) {
        s.fontSize = newVal;
        s.useCustomSettings = true;
      }
    };
    menuItems.push_back(fontEntry);
  }

  MenuEntry layoutSeparator;
  layoutSeparator.item = MenuItem::Separator;
  layoutSeparator.group = GroupType::LAYOUT;
  layoutSeparator.name = "═══ Layout ═══";
  layoutSeparator.getValueText = [this](const BookSettings&) -> const char* {
    static char indicator[4];
    snprintf(indicator, sizeof(indicator), "%s", groupExpanded[GroupType::LAYOUT] ? "-" : "+");
    return indicator;
  };
  layoutSeparator.change = [](BookSettings&, int) {};
  menuItems.push_back(layoutSeparator);

  if (groupExpanded[GroupType::LAYOUT]) {
    MenuEntry lineHeightEntry;
    lineHeightEntry.item = MenuItem::LineHeight;
    lineHeightEntry.group = GroupType::LAYOUT;
    lineHeightEntry.name = "Line height";
    lineHeightEntry.getValueText = [](const BookSettings& s) -> const char* {
      static char buf[8];
      snprintf(buf, sizeof(buf), "%d%%", s.lineHeight);
      return buf;
    };
    lineHeightEntry.change = [](BookSettings& s, int delta) {
      int newVal = static_cast<int>(s.lineHeight) + delta * 5;
      if (newVal < 10) newVal = 10;
      if (newVal > 200) newVal = 200;
      s.lineHeight = static_cast<uint8_t>(newVal);
      s.useCustomSettings = true;
    };
    menuItems.push_back(lineHeightEntry);

    MenuEntry textSpaceEntry;
    textSpaceEntry.item = MenuItem::TextSpace;
    textSpaceEntry.group = GroupType::LAYOUT;
    textSpaceEntry.name = "Text space";
    textSpaceEntry.getValueText = [](const BookSettings& s) -> const char* {
      static char buf[8];
      snprintf(buf, sizeof(buf), "%d%%", s.textSpace);
      return buf;
    };
    textSpaceEntry.change = [](BookSettings& s, int delta) {
      int newVal = static_cast<int>(s.textSpace) + delta * 5;
      if (newVal < 10) newVal = 10;
      if (newVal > 200) newVal = 200;
      s.textSpace = static_cast<uint8_t>(newVal);
      s.useCustomSettings = true;
    };
    menuItems.push_back(textSpaceEntry);

    MenuEntry alignEntry;
    alignEntry.item = MenuItem::Alignment;
    alignEntry.group = GroupType::LAYOUT;
    alignEntry.name = "Paragraph Alignment";
    alignEntry.getValueText = [](const BookSettings& s) -> const char* {
      static const char* align[] = {"Justify", "Left", "Center", "Right", "Css"};
      int index = s.paragraphAlignment;
      if (index > 4) index = 0;
      return align[index];
    };
    alignEntry.change = [](BookSettings& s, int delta) {
      int newVal = s.paragraphAlignment + delta;
      if (newVal >= 0 && newVal <= 4) {
        s.paragraphAlignment = newVal;
        s.useCustomSettings = true;
      }
    };
    menuItems.push_back(alignEntry);

    MenuEntry extraParaEntry;
    extraParaEntry.item = MenuItem::ExtraParagraphSpacing;
    extraParaEntry.group = GroupType::LAYOUT;
    extraParaEntry.name = "Extra Paragraph Spacing";
    extraParaEntry.getValueText = [](const BookSettings& s) -> const char* {
      return s.extraParagraphSpacing ? "On" : "Off";
    };
    extraParaEntry.change = [](BookSettings& s, int) {
      s.extraParagraphSpacing = !s.extraParagraphSpacing;
      s.useCustomSettings = true;
    };
    menuItems.push_back(extraParaEntry);

    MenuEntry cssIndentEntry;
    cssIndentEntry.item = MenuItem::ParagraphCssIndent;
    cssIndentEntry.group = GroupType::LAYOUT;
    cssIndentEntry.name = "Indent";
    cssIndentEntry.getValueText = [](const BookSettings& s) -> const char* {
      return s.paragraphCssIndentEnabled ? "On" : "Off";
    };
    cssIndentEntry.change = [](BookSettings& s, int) {
      s.paragraphCssIndentEnabled = s.paragraphCssIndentEnabled ? 0 : 1;
      s.useCustomSettings = true;
    };
    menuItems.push_back(cssIndentEntry);

    MenuEntry marginEntry;
    marginEntry.item = MenuItem::ScreenMargin;
    marginEntry.group = GroupType::LAYOUT;
    marginEntry.name = "Screen Margin";
    marginEntry.getValueText = [](const BookSettings& s) -> const char* {
      static char buf[10];
      snprintf(buf, sizeof(buf), "%d px", s.screenMargin);
      return buf;
    };
    marginEntry.change = [](BookSettings& s, int delta) {
      int newVal = s.screenMargin + (delta * 5);
      if (newVal >= 5 && newVal <= 80) {
        s.screenMargin = newVal;
        s.useCustomSettings = true;
      }
    };
    menuItems.push_back(marginEntry);

    MenuEntry orientationEntry;
    orientationEntry.item = MenuItem::ReadingOrientation;
    orientationEntry.group = GroupType::LAYOUT;
    orientationEntry.name = "Orientation";
    orientationEntry.getValueText = [](const BookSettings& s) -> const char* {
      static const char* orientation[] = {"Portrait", "Landscape CW", "Inverted", "Landscape CCW"};
      int index = s.orientation;
      if (index > 3) index = 0;
      return orientation[index];
    };
    orientationEntry.change = [](BookSettings& s, int delta) {
      int newVal = s.orientation + delta;
      if (newVal >= 0 && newVal <= 3) {
        s.orientation = newVal;
        s.useCustomSettings = true;
      }
    };
    menuItems.push_back(orientationEntry);

    MenuEntry hypenEntry;
    hypenEntry.item = MenuItem::Hyphenation;
    hypenEntry.group = GroupType::CONTROLS;
    hypenEntry.name = "Hyphenation";
    hypenEntry.getValueText = [](const BookSettings& s) -> const char* { return s.hyphenationEnabled ? "On" : "Off"; };
    hypenEntry.change = [](BookSettings& s, int) {
      s.hyphenationEnabled = !s.hyphenationEnabled;
      s.useCustomSettings = true;
    };
    menuItems.push_back(hypenEntry);

    MenuEntry bionicEntry;
    bionicEntry.item = MenuItem::BionicReading;
    bionicEntry.group = GroupType::LAYOUT;
    bionicEntry.name = "Bionic Reading";
    bionicEntry.getValueText = [](const BookSettings& s) -> const char* {
      return s.bionicReadingEnabled ? "On" : "Off";
    };
    bionicEntry.change = [](BookSettings& s, int) {
      s.bionicReadingEnabled = s.bionicReadingEnabled ? 0 : 1;
      s.useCustomSettings = true;
    };
    menuItems.push_back(bionicEntry);
  }

  MenuEntry imageSeparator;
  imageSeparator.item = MenuItem::Separator;
  imageSeparator.group = GroupType::IMAGE;
  imageSeparator.name = "═══ Image ═══";
  imageSeparator.getValueText = [this](const BookSettings&) -> const char* {
    static char indicator[4];
    snprintf(indicator, sizeof(indicator), "%s", groupExpanded[GroupType::IMAGE] ? "-" : "+");
    return indicator;
  };
  imageSeparator.change = [](BookSettings&, int) {};
  menuItems.push_back(imageSeparator);

  if (groupExpanded[GroupType::IMAGE]) {
    MenuEntry imgGrayEntry;
    imgGrayEntry.item = MenuItem::ReaderImageGrayscale;
    imgGrayEntry.group = GroupType::IMAGE;
    imgGrayEntry.name = "Image Quality";
    imgGrayEntry.getValueText = [](const BookSettings& s) -> const char* {
      switch (s.readerImageGrayscale) {
        case SystemSetting::READER_IMAGE_MEDIUM:
          return "Medium";
        case SystemSetting::READER_IMAGE_HIGH:
          return "High";
        default:
          return "Low";
      }
    };
    imgGrayEntry.change = [](BookSettings& s, int delta) {
      const int step = delta >= 0 ? 1 : (SystemSetting::READER_IMAGE_QUALITY_COUNT - 1);
      s.readerImageGrayscale =
          static_cast<uint8_t>((s.readerImageGrayscale + step) % SystemSetting::READER_IMAGE_QUALITY_COUNT);
      s.useCustomSettings = true;
    };
    menuItems.push_back(imgGrayEntry);

    MenuEntry smartRefreshEntry;
    smartRefreshEntry.item = MenuItem::ReaderSmartImageRefresh;
    smartRefreshEntry.group = GroupType::IMAGE;
    smartRefreshEntry.name = "Smart Refresh (Images)";
    smartRefreshEntry.getValueText = [](const BookSettings& s) -> const char* {
      return s.readerSmartRefreshOnImages ? "On" : "Off";
    };
    smartRefreshEntry.change = [](BookSettings& s, int) {
      s.readerSmartRefreshOnImages = s.readerSmartRefreshOnImages ? 0 : 1;
      s.useCustomSettings = true;
    };
    menuItems.push_back(smartRefreshEntry);
  }

  MenuEntry controlsSeparator;
  controlsSeparator.item = MenuItem::Separator;
  controlsSeparator.group = GroupType::CONTROLS;
  controlsSeparator.name = "═══ System ═══";
  controlsSeparator.getValueText = [this](const BookSettings&) -> const char* {
    static char indicator[4];
    snprintf(indicator, sizeof(indicator), "%s", groupExpanded[GroupType::CONTROLS] ? "-" : "+");
    return indicator;
  };
  controlsSeparator.change = [](BookSettings&, int) {};
  menuItems.push_back(controlsSeparator);

  if (groupExpanded[GroupType::CONTROLS]) {
    MenuEntry aaEntry;
    aaEntry.item = MenuItem::AntiAliasing;
    aaEntry.group = GroupType::CONTROLS;
    aaEntry.name = "Text Anti-Aliasing";
    aaEntry.getValueText = [](const BookSettings& s) -> const char* { return s.textAntiAliasing ? "On" : "Off"; };
    aaEntry.change = [](BookSettings& s, int) {
      s.textAntiAliasing = !s.textAntiAliasing;
      s.useCustomSettings = true;
    };
    menuItems.push_back(aaEntry);

    MenuEntry refreshEntry;
    refreshEntry.item = MenuItem::RefreshRate;
    refreshEntry.group = GroupType::CONTROLS;
    refreshEntry.name = "Refresh Frequency";
    refreshEntry.getValueText = [](const BookSettings& s) -> const char* {
      if (s.refreshFrequency <= 1) return "1 page";
      if (s.refreshFrequency <= 5) return "5 pages";
      if (s.refreshFrequency <= 10) return "10 pages";
      if (s.refreshFrequency <= 15) return "15 pages";
      return "30 pages";
    };
    refreshEntry.change = [](BookSettings& s, int delta) {
      int currentIdx = 4;
      if (s.refreshFrequency <= 1) {
        currentIdx = 0;
      }
      if (s.refreshFrequency <= 5) {
        currentIdx = 1;
      }
      if (s.refreshFrequency <= 10) {
        currentIdx = 2;
      }
      if (s.refreshFrequency <= 15) {
        currentIdx = 3;
      }

      int newIdx = currentIdx + delta;
      if (newIdx >= 0 && newIdx <= 4) {
        static const uint8_t actualValues[] = {1, 5, 10, 15, 30};
        s.refreshFrequency = actualValues[newIdx];
        s.useCustomSettings = true;
      }
    };
    menuItems.push_back(refreshEntry);

    MenuEntry chapterEntry;
    chapterEntry.item = MenuItem::ChapterSkip;
    chapterEntry.group = GroupType::CONTROLS;
    chapterEntry.name = "Long press";
    chapterEntry.getValueText = [](const BookSettings& s) -> const char* {
      static const char* kLabels[] = {"Off", "Chapter skip", "Skip 5 pages"};
      const unsigned idx = s.longPressChapterSkip > SystemSetting::LONG_PRESS_PAGE_SKIP_5
                                 ? SystemSetting::LONG_PRESS_CHAPTER_SKIP
                                 : s.longPressChapterSkip;
      return kLabels[idx];
    };
    chapterEntry.change = [](BookSettings& s, int delta) {
      int v = static_cast<int>(s.longPressChapterSkip) + delta;
      if (v < SystemSetting::LONG_PRESS_OFF) {
        v = SystemSetting::LONG_PRESS_PAGE_SKIP_5;
      }
      if (v > SystemSetting::LONG_PRESS_PAGE_SKIP_5) {
        v = SystemSetting::LONG_PRESS_OFF;
      }
      s.longPressChapterSkip = static_cast<uint8_t>(v);
      s.useCustomSettings = true;
    };
    menuItems.push_back(chapterEntry);

    MenuEntry pageAutoTurnEntry;
    pageAutoTurnEntry.item = MenuItem::PageAutoTurn;
    pageAutoTurnEntry.group = GroupType::CONTROLS;
    pageAutoTurnEntry.name = "Page Auto Turn";
    pageAutoTurnEntry.getValueText = [](const BookSettings& s) -> const char* {
      static char buf[20];
      if (s.pageAutoTurnSeconds == 0) {
        return "Off";
      }
      snprintf(buf, sizeof(buf), "%d sec", s.pageAutoTurnSeconds);
      return buf;
    };
    pageAutoTurnEntry.change = [](BookSettings& s, int delta) {
      int newVal = s.pageAutoTurnSeconds + (delta * 10);
      if (newVal >= 0 && newVal <= 180) {
        s.pageAutoTurnSeconds = newVal;
        s.useCustomSettings = true;
      }
    };
    menuItems.push_back(pageAutoTurnEntry);
  }

  MenuEntry statusBarSeparator;
  statusBarSeparator.item = MenuItem::StatusBarSeparator;
  statusBarSeparator.group = GroupType::STATUS_BAR;
  statusBarSeparator.name = "═══ Status Bar ═══";
  statusBarSeparator.getValueText = [this](const BookSettings&) -> const char* {
    static char indicator[4];
    snprintf(indicator, sizeof(indicator), "%s", groupExpanded[GroupType::STATUS_BAR] ? "-" : "+");
    return indicator;
  };
  statusBarSeparator.change = [](BookSettings&, int) {};
  menuItems.push_back(statusBarSeparator);

  if (groupExpanded[GroupType::STATUS_BAR]) {
    MenuEntry statusLeftEntry;
    statusLeftEntry.item = MenuItem::StatusBarLeft;
    statusLeftEntry.group = GroupType::STATUS_BAR;
    statusLeftEntry.name = "Left Section";
    statusLeftEntry.getValueText = [](const BookSettings& s) -> const char* {
      return getStatusBarItemName(s.statusBarLeft.item);
    };
    statusLeftEntry.change = [](BookSettings& s, int delta) {
      int newVal = static_cast<int>(s.statusBarLeft.item) + delta;
      if (newVal >= 0 && newVal <= static_cast<int>(StatusBarItem::AUTHOR_NAME)) {
        s.statusBarLeft.item = static_cast<StatusBarItem>(newVal);
        s.useCustomSettings = true;
      }
    };
    menuItems.push_back(statusLeftEntry);

    MenuEntry statusMiddleEntry;
    statusMiddleEntry.item = MenuItem::StatusBarMiddle;
    statusMiddleEntry.group = GroupType::STATUS_BAR;
    statusMiddleEntry.name = "Middle Section";
    statusMiddleEntry.getValueText = [](const BookSettings& s) -> const char* {
      return getStatusBarItemName(s.statusBarMiddle.item);
    };
    statusMiddleEntry.change = [](BookSettings& s, int delta) {
      int newVal = static_cast<int>(s.statusBarMiddle.item) + delta;
      if (newVal >= 0 && newVal <= static_cast<int>(StatusBarItem::AUTHOR_NAME)) {
        s.statusBarMiddle.item = static_cast<StatusBarItem>(newVal);
        s.useCustomSettings = true;
      }
    };
    menuItems.push_back(statusMiddleEntry);

    MenuEntry statusRightEntry;
    statusRightEntry.item = MenuItem::StatusBarRight;
    statusRightEntry.group = GroupType::STATUS_BAR;
    statusRightEntry.name = "Right Section";
    statusRightEntry.getValueText = [](const BookSettings& s) -> const char* {
      return getStatusBarItemName(s.statusBarRight.item);
    };
    statusRightEntry.change = [](BookSettings& s, int delta) {
      int newVal = static_cast<int>(s.statusBarRight.item) + delta;
      if (newVal >= 0 && newVal <= static_cast<int>(StatusBarItem::AUTHOR_NAME)) {
        s.statusBarRight.item = static_cast<StatusBarItem>(newVal);
        s.useCustomSettings = true;
      }
    };
    menuItems.push_back(statusRightEntry);
  }

}

/**
 * @brief Gets the display name for a status bar item
 * @param item The status bar item enum value
 * @return String representation of the item
 */
const char* SettingsDrawer::getStatusBarItemName(StatusBarItem item) {
  static const char* names[] = {"None",           "Page Numbers", "Percentage",     "Chapter Title",
                                "Battery Icon",   "Battery %",    "Battery Icon+%", "Progress Bar",
                                "Progress Bar+%", "Page Bars",    "Book Title",     "Author Name"};
  int index = static_cast<int>(item);
  if (index > static_cast<int>(StatusBarItem::AUTHOR_NAME)) {
    index = 0;
  }
  return names[index];
}

/**
 * @brief Shows the settings drawer
 */
void SettingsDrawer::show() {
  if (visible) return;
  syncLayoutFromRenderer();
  visible = true;
  dismissed = false;
  selectedIndex = 0;
  scrollOffset = 0;
  renderWithRefresh(HalDisplay::FAST_REFRESH);
}

/**
 * @brief Hides the settings drawer
 */
void SettingsDrawer::hide() {
  visible = false;
  dismissed = true;
}

void SettingsDrawer::relayoutForRendererChange() {
  syncLayoutFromRenderer();
  setupMenu();
}

/**
 * @brief Renders the settings drawer
 */
void SettingsDrawer::render() {
  if (!visible) return;
  renderWithRefresh(HalDisplay::FAST_REFRESH);
}

/**
 * @brief Renders the settings drawer with specified refresh mode
 * @param mode Display refresh mode to use
 */
void SettingsDrawer::renderWithRefresh(HalDisplay::RefreshMode mode) {
  if (!visible) {
    return;
  }
  syncLayoutFromRenderer();
  drawBackground();
  drawMenuItems();
  drawScrollIndicator();
  if (embedded_) {
    // Host owns the rest of the screen and the display push.
    if (onEmbeddedInvalidate_) onEmbeddedInvalidate_();
    return;
  }
  if (!isLandscapeReader(renderer)) {
    if (mappedInputForHints_ != nullptr) {
      const auto labels =
          mappedInputForHints_->mapLabels("\xC2\xAB Back", "Open", "\xC2\xAB", "\xC2\xBB");
      renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3,
                               labels.btn4);
    } else {
      renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, "\xC2\xAB Back", "Open", "\xC2\xAB", "\xC2\xBB");
    }
  }
  renderer.displayBuffer(mode);
}

/**
 * @brief Draws the background panel of the settings drawer
 */
void SettingsDrawer::drawBackground() {
  renderer.rectangle.fill(drawerX, drawerY, drawerWidth, drawerHeight, false);
  renderer.rectangle.render(drawerX, drawerY, drawerWidth, drawerHeight, true);

  int currentY = drawerY + kDrawerHeaderTitleY;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_12_FONT_ID, drawerX + 20, currentY, "Book Settings", true, EpdFontFamily::BOLD);

  const char* tag = settings.useCustomSettings ? "[Custom]" : "[Global]";
  currentY += kDrawerHeaderTagGap + 5;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, drawerX + 20, currentY, tag, true);

  int dividerY = currentY + kDrawerHeaderDividerGap;
  renderer.line.render(drawerX, dividerY, drawerX + drawerWidth, dividerY, true);
}

/**
 * @brief Draws all menu items in the current scroll view
 */
void SettingsDrawer::drawMenuItems() {
  for (int i = 0; i < itemsPerPage && (i + scrollOffset) < static_cast<int>(menuItems.size()); i++) {
    drawMenuItemRow(i, i + scrollOffset);
  }
}

void SettingsDrawer::drawMenuItemRow(int visibleRow, int menuIndex) {
  if (menuIndex < 0 || menuIndex >= static_cast<int>(menuItems.size())) {
    return;
  }

  const int startY = drawerY + kDrawerListTop;
  const int itemY = startY + (visibleRow * itemHeight);
  const auto& entry = menuItems[static_cast<size_t>(menuIndex)];
  const bool isSelected = (menuIndex == selectedIndex);

  renderer.rectangle.fill(drawerX, itemY, drawerWidth, itemHeight,
                          isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink)
                                     : static_cast<int>(GfxRenderer::FillTone::Paper));

  if (entry.item == MenuItem::Separator || entry.item == MenuItem::StatusBarSeparator) {
    const int textX = drawerX + 15;
    const int textY = itemY + (itemHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
    renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, textY, entry.name, isSelected ? 0 : 1);

    const char* indicator = entry.getValueText(settings);
    if (indicator && indicator[0] != '\0') {
      const int indicatorW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, indicator);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, drawerX + drawerWidth - indicatorW - 30, textY,
                           indicator, isSelected ? 0 : 1, EpdFontFamily::BOLD);
    }

    renderer.line.render(drawerX, itemY + itemHeight - 1, drawerX + drawerWidth, itemY + itemHeight - 1, true);
    return;
  }

  const int textX = drawerX + 23;
  const int textY = itemY + (itemHeight - renderer.text.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
  renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textX, textY, entry.name, isSelected ? 0 : 1);

  const int valueColumnRight = drawerX + drawerWidth - 24;
  if (entry.item == MenuItem::FontFamily) {
    const char* val = entry.getValueText(settings);
    if (val && val[0] != '\0') {
      ReaderFontSettingsDraw::drawFontFamilyRowValue(renderer, settings.fontFamily, valueColumnRight, itemY,
                                                     itemHeight, isSelected, val);
    }
  } else if (entry.item == MenuItem::FontSize) {
    const int valueAreaLeft = std::max(textX + 72, drawerX + drawerWidth * 35 / 100);
    ReaderFontSettingsDraw::drawFontSizeSliderRowValue(renderer, settings.fontFamily, settings.fontSize,
                                                       valueAreaLeft, valueColumnRight, itemY, itemHeight,
                                                       isSelected);
  } else {
    bool checkbox = false;
    bool checked = false;
    switch (entry.item) {
      case MenuItem::ExtraParagraphSpacing:
        checkbox = true;
        checked = settings.extraParagraphSpacing != 0;
        break;
      case MenuItem::ParagraphCssIndent:
        checkbox = true;
        checked = settings.paragraphCssIndentEnabled != 0;
        break;
      case MenuItem::Hyphenation:
        checkbox = true;
        checked = settings.hyphenationEnabled != 0;
        break;
      case MenuItem::BionicReading:
        checkbox = true;
        checked = settings.bionicReadingEnabled != 0;
        break;
      case MenuItem::ReaderSmartImageRefresh:
        checkbox = true;
        checked = settings.readerSmartRefreshOnImages != 0;
        break;
      case MenuItem::AntiAliasing:
        checkbox = true;
        checked = settings.textAntiAliasing != 0;
        break;
      case MenuItem::ChapterSkip:
        checkbox = false;
        break;
      default:
        break;
    }
    if (checkbox) {
      ReaderFontSettingsDraw::drawToggleCheckbox(renderer, valueColumnRight, itemY, itemHeight, isSelected, checked);
    } else {
      const char* val = entry.getValueText(settings);
      if (val && val[0] != '\0') {
        const int valW = renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, val);
        renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, valueColumnRight - valW, textY, val,
                             isSelected ? 0 : 1);
      }
    }
  }

  renderer.line.render(drawerX, itemY + itemHeight - 1, drawerX + drawerWidth, itemY + itemHeight - 1, true);
}

/**
 * @brief Draws a scroll indicator when content exceeds visible area
 */
void SettingsDrawer::drawScrollIndicator() {
  int totalItems = static_cast<int>(menuItems.size());
  if (totalItems <= itemsPerPage) return;

  int startY = drawerY + 80;
  int listHeight = itemsPerPage * itemHeight;
  int thumbH = (itemsPerPage * listHeight) / totalItems;
  int thumbY = startY + (scrollOffset * listHeight) / totalItems;

  renderer.rectangle.fill(drawerX + drawerWidth - 4, thumbY, 2, thumbH, true);
}

void SettingsDrawer::clearScrollIndicatorArea() {
  const int startY = drawerY + 80;
  const int listHeight = itemsPerPage * itemHeight;
  renderer.rectangle.fill(drawerX + drawerWidth - 5, startY, 4, listHeight, false);
}

void SettingsDrawer::refreshSelectionRows(int previousIndex, bool redrawScrollIndicator) {
  if (!visible) {
    return;
  }

  if (previousIndex >= scrollOffset && previousIndex < scrollOffset + itemsPerPage) {
    drawMenuItemRow(previousIndex - scrollOffset, previousIndex);
  }

  if (selectedIndex >= scrollOffset && selectedIndex < scrollOffset + itemsPerPage) {
    drawMenuItemRow(selectedIndex - scrollOffset, selectedIndex);
  }

  if (redrawScrollIndicator) {
    clearScrollIndicatorArea();
    drawScrollIndicator();
  }

  if (embedded_) {
    if (onEmbeddedInvalidate_) onEmbeddedInvalidate_();
    return;
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

/**
 * @brief Toggles expansion state of a settings group
 * @param group The group to toggle
 */
void SettingsDrawer::toggleGroup(GroupType group) {
  groupExpanded[group] = !groupExpanded[group];
  setupMenu();

  for (size_t i = 0; i < menuItems.size(); i++) {
    if (menuItems[i].group == group &&
        (menuItems[i].item == MenuItem::Separator || menuItems[i].item == MenuItem::StatusBarSeparator)) {
      selectedIndex = static_cast<int>(i);
      if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
      } else if (selectedIndex >= scrollOffset + itemsPerPage) {
        scrollOffset = selectedIndex - itemsPerPage + 1;
      }
      break;
    }
  }
}

/**
 * @brief Handles input for the settings drawer
 * @param input Reference to the input manager
 */
void SettingsDrawer::handleInput(MappedInputManager& input) {
  if (!visible) return;

  uint32_t currentTime = xTaskGetTickCount();
  if (currentTime - lastInputTime < pdMS_TO_TICKS(150)) {
    return;
  }

  bool needRedraw = false;

  if (readSettingsListPrev(input, renderer)) {
    const int previousIndex = selectedIndex;
    if (selectedIndex > 0) {
      selectedIndex--;
      const bool scrolled = selectedIndex < scrollOffset;
      if (scrolled) {
        scrollOffset = selectedIndex;
        needRedraw = true;
      } else {
        lastInputTime = currentTime;
        refreshSelectionRows(previousIndex, false);
        return;
      }
    }
  }

  if (readSettingsListNext(input, renderer)) {
    const int previousIndex = selectedIndex;
    if (selectedIndex < static_cast<int>(menuItems.size()) - 1) {
      selectedIndex++;
      int maxScroll = std::max(0, static_cast<int>(menuItems.size()) - itemsPerPage);
      const bool scrolled = selectedIndex > scrollOffset + itemsPerPage - 1;
      if (scrolled) {
        scrollOffset = std::min(selectedIndex - itemsPerPage + 1, maxScroll);
        needRedraw = true;
      } else {
        lastInputTime = currentTime;
        refreshSelectionRows(previousIndex, false);
        return;
      }
    }
  }

  if (readValueDecrease(input, renderer)) {
    applyChange(-1);
    needRedraw = true;
  }

  if (readValueIncrease(input, renderer)) {
    applyChange(1);
    needRedraw = true;
  }

  if (input.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(menuItems.size())) {
      const auto& selected = menuItems[selectedIndex];
      if (selected.item == MenuItem::Separator || selected.item == MenuItem::StatusBarSeparator) {
        toggleGroup(selected.group);
        needRedraw = true;
      } else if (selected.item == MenuItem::PresetPicker) {
        // Snapshot-apply the highlighted preset onto this book, then rebuild rows to reflect new values.
        ReaderPresetStore::getInstance().applyToBook(presetPickIndex_, settings);
        setupMenu();
        settingsUpdated = true;
        if (onSettingsChanged) onSettingsChanged();
        needRedraw = true;
      }
    }
  }

  if (input.isPressed(MappedInputManager::Button::Back)) {
    hide();
    needRedraw = true;
  }

  if (needRedraw) {
    lastInputTime = currentTime;
    renderWithRefresh(HalDisplay::FAST_REFRESH);
  }
}

/**
 * @brief Applies a delta change to the currently selected menu item
 * @param delta Amount to change (-1 or 1)
 */
void SettingsDrawer::applyChange(int delta) {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(menuItems.size())) return;
  const auto& selected = menuItems[selectedIndex];
  selected.change(settings, delta);

  switch (selected.item) {
    case MenuItem::FontSize:
    case MenuItem::LineHeight:
    case MenuItem::TextSpace:
    case MenuItem::ScreenMargin:
    case MenuItem::Alignment:
    case MenuItem::ExtraParagraphSpacing:
    case MenuItem::ParagraphCssIndent:
    case MenuItem::BionicReading:
    case MenuItem::FontFamily:
      settingsUpdated = true;
      break;
    case MenuItem::ReadingOrientation:
    case MenuItem::PageAutoTurn:
    case MenuItem::ReaderImageGrayscale:
    case MenuItem::ReaderSmartImageRefresh:
    case MenuItem::StatusBarLeft:
    case MenuItem::StatusBarMiddle:
    case MenuItem::StatusBarRight:
    case MenuItem::Hyphenation:
    case MenuItem::RefreshRate:
    case MenuItem::AntiAliasing:
    case MenuItem::ChapterSkip:
    case MenuItem::NavigationLock:
    case MenuItem::Separator:
    case MenuItem::StatusBarSeparator:
    case MenuItem::PresetPicker:
      break;
  }

  if (onSettingsChanged) onSettingsChanged();
}
