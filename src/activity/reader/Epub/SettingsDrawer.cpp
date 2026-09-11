/**
 * @file SettingsDrawer.cpp
 * @brief Definitions for SettingsDrawer.
 */

#include "SettingsDrawer.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "../../settings/ReaderFontSettingsDraw.h"
#include "StatusBar.h"
#include "images/AlignCenter.h"
#include "images/AlignCss.h"
#include "images/AlignJustify.h"
#include "images/AlignLeft.h"
#include "images/AlignRight.h"
#include "images/LibraryFilterLeft.h"
#include "images/LibraryFilterRight.h"
#include "images/PresetBars.h"
#include "images/PresetFont.h"
#include "images/PresetLayout.h"
#include "images/PresetSettings.h"
#include "images/Rotate.h"
#include "state/ReaderPreset.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/UiTheme.h"

#define SETTINGS SystemSetting::getInstance()

constexpr int LIST_ITEM_HEIGHT = UiTheme::DRAWER_LIST_ITEM_HEIGHT;

namespace {
constexpr int kPresetTabSize = 40;
// Keep the preset editor tab strip compact so the book-settings list gets the
// same usable height as the Pro layout.
constexpr int kPresetTabPadding = 10;
constexpr int kPresetTabHeight = kPresetTabSize + kPresetTabPadding * 2;

const char* statusBarItemName(const StatusBarItem item) {
  static const char* names[] = {"None",       "Page Numbers",   "Percentage",   "Chapter Title",      "Battery Icon",
                                "Battery %",  "Battery Icon+%", "Progress Bar", "Progress Bar+%",     "Page Bars",
                                "Book Title", "Author Name",    "Page Num+%",   "Time Left (Chapter)", "Time Left (Book)"};
  int index = static_cast<int>(item);
  if (index < 0 || index >= static_cast<int>(StatusBarItem::STATUS_BAR_ITEM_COUNT)) {
    index = 0;
  }
  return names[index];
}

int drawerHeaderHeight() { return INX_THEME.drawerHeaderHeight(); }

int drawerListTop() { return drawerHeaderHeight() + 1; }
constexpr int kDrawerListBottomPadding = UiTheme::DRAWER_LIST_BOTTOM_PADDING;
constexpr int kDrawerHeaderHPad = 20;
constexpr int kDrawerHeaderPillPadX = 10;
constexpr int kDrawerHeaderPillHeight = 24;
constexpr int kPortraitDrawerHeightPercent = 50;
constexpr int kVisibleMenuRows = 5;
constexpr int kSelectorRows = 5;

bool isLandscapeReader(const GfxRenderer& gfx) {
  const auto o = gfx.getOrientation();
  return o == GfxRenderer::LandscapeClockwise || o == GfxRenderer::LandscapeCounterClockwise;
}

void drawSettingsDropdown(const GfxRenderer& renderer, int left, int right, int itemY, int itemHeight,
                          const char* value, const bool rowSelected) {
  constexpr int kPadX = 10;
  const int boxY = itemY + 8;
  const int boxH = itemHeight - 16;
  const bool ink = !rowSelected;
  renderer.rectangle.render(left, boxY, right - left, boxH, ink, false);

  const int textMaxW = std::max(1, right - left - 32);
  const std::string shown = renderer.text.truncate(MONTSERRAT_8_FONT_ID, value ? value : "", textMaxW,
                                                    EpdFontFamily::REGULAR);
  const int textY = boxY + (boxH - renderer.text.getLineHeight(MONTSERRAT_8_FONT_ID)) / 2;
  renderer.text.render(MONTSERRAT_8_FONT_ID, left + kPadX, textY, shown.c_str(), ink,
                       EpdFontFamily::REGULAR);

  const int chevronX = right - 14;
  const int chevronY = boxY + boxH / 2 - 2;
  renderer.line.render(chevronX - 3, chevronY, chevronX, chevronY + 3, ink);
  renderer.line.render(chevronX, chevronY + 3, chevronX + 3, chevronY, ink);
}

void drawValueStepper(const GfxRenderer& renderer, const char* value, int left, int right, int itemY,
                      int itemHeight, const bool rowSelected) {
  constexpr int iconSize = 30;
  constexpr int gap = 8;
  const bool ink = !rowSelected;
  const int valueW = renderer.text.getWidth(MONTSERRAT_10_FONT_ID, value ? value : "", EpdFontFamily::REGULAR);
  const int width = iconSize + gap + valueW + gap + iconSize;
  const int x = std::max(left, right - width);
  const int iconY = itemY + (itemHeight - iconSize) / 2 + 5;
  const int textY = itemY + (itemHeight - renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID)) / 2;

  renderer.bitmap.icon(LibraryFilterLeft, x, iconY, iconSize, iconSize, BitmapRender::Orientation::None, !ink);
  renderer.text.render(MONTSERRAT_10_FONT_ID, x + iconSize + gap, textY, value ? value : "", ink,
                       EpdFontFamily::REGULAR);
  renderer.bitmap.icon(LibraryFilterRight, x + iconSize + gap + valueW + gap, iconY, iconSize, iconSize,
                       BitmapRender::Orientation::None, !ink);
}

void drawJustificationSegments(const GfxRenderer& renderer, int left, int right, int itemY, int itemHeight,
                               const int selectedIndex, const bool rowSelected) {
  static constexpr const uint8_t* kIcons[] = {AlignJustify, AlignLeft, AlignRight, AlignCenter, AlignCss};
  constexpr int kCount = sizeof(kIcons) / sizeof(kIcons[0]);
  const int boxY = itemY + 7;
  const int boxH = itemHeight - 14;
  const int segmentW = std::max(1, (right - left) / kCount);

  for (int i = 0; i < kCount; ++i) {
    const int segmentX = left + i * segmentW;
    const int width = (i == kCount - 1) ? right - segmentX : segmentW;
    const bool selected = i == selectedIndex;
    const bool fill = rowSelected ? !selected : selected;
    renderer.rectangle.fill(segmentX, boxY, width, boxH, fill);
    renderer.rectangle.render(segmentX, boxY, width, boxH, !rowSelected, false);
    constexpr int kIconSize = 30;
    const int iconX = segmentX + std::max(0, (width - kIconSize) / 2);
    const int iconY = boxY + std::max(0, (boxH - kIconSize) / 2);
    renderer.bitmap.icon(kIcons[i], iconX, iconY, kIconSize, kIconSize, BitmapRender::Orientation::None,
                         rowSelected ? !selected : selected);
  }
}

struct SelectorBounds {
  int x;
  int y;
  int width;
  int height;
  int rows;
};

SelectorBounds selectorBounds(const int drawerX, const int drawerY, const int drawerWidth, const int drawerHeight,
                              const int fieldY, const int fieldHeight, const int rows, const bool openUpward) {
  const int left = drawerX + drawerWidth * 40 / 100;
  const int right = drawerX + drawerWidth - 24;
  const int width = std::max(80, right - left);
  const int rowHeight = LIST_ITEM_HEIGHT;
  const int available = openUpward ? std::max(1, fieldY - drawerY)
                                   : std::max(1, drawerY + drawerHeight - fieldY - fieldHeight);
  const int visibleRows = std::max(1, std::min(rows, available / rowHeight));
  const int height = visibleRows * rowHeight + 1;
  const int y = openUpward ? std::max(drawerY, fieldY - height)
                           : std::min(fieldY + fieldHeight, drawerY + drawerHeight - height);
  return {left, y, width, height, visibleRows};
}

bool selectorOpensUpward(const int drawerY, const int drawerHeight, const int fieldY, const int fieldHeight) {
  const int above = std::max(0, fieldY - drawerY);
  const int below = std::max(0, drawerY + drawerHeight - fieldY - fieldHeight);
  return above >= below;
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

void drawModePill(const GfxRenderer& renderer, const int right, const int centerY, const char* label,
                  const bool filled) {
  const int labelW = renderer.text.getWidth(MONTSERRAT_8_FONT_ID, label);
  const int pillW = labelW + kDrawerHeaderPillPadX * 2;
  const int pillX = right - pillW;
  const int pillY = centerY - kDrawerHeaderPillHeight / 2;
  renderer.rectangle.fill(pillX, pillY, pillW, kDrawerHeaderPillHeight, filled, /*rounded=*/true, /*subtle=*/true);
  renderer.rectangle.render(pillX, pillY, pillW, kDrawerHeaderPillHeight, true, /*rounded=*/true, /*subtle=*/true);
  const int textY =
      pillY + (kDrawerHeaderPillHeight - renderer.text.getLineHeight(MONTSERRAT_8_FONT_ID)) / 2;
  renderer.text.render(MONTSERRAT_8_FONT_ID, pillX + kDrawerHeaderPillPadX, textY, label, !filled,
                       EpdFontFamily::BOLD);
}

const char* drawerModeLabel(const BookSettings& settings) {
  if (settings.readerPresetIndex == BookSettings::kNoReaderPreset) {
    return "Custom";
  }

  thread_local std::string label;
  label = ReaderPresetStore::getInstance().nameOf(settings.readerPresetIndex);
  return label.empty() ? "Custom" : label.c_str();
}

bool hasPresetSource(const BookSettings& settings) {
  return settings.readerPresetIndex != BookSettings::kNoReaderPreset &&
         !ReaderPresetStore::getInstance().nameOf(settings.readerPresetIndex).empty();
}

}  // namespace

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

  groupExpanded_.fill(false);

  setupMenu();
}

/**
 * @brief Destructor
 */
SettingsDrawer::~SettingsDrawer() {}

int SettingsDrawer::contentListTop() const { return kPresetTabHeight + 1; }

void SettingsDrawer::setEmbeddedRegion(int x, int y, int w, int h) {
  embedded_ = true;
  drawerX = x;
  drawerY = y;
  drawerWidth = w;
  drawerHeight = h;
  itemsPerPage = std::min(kVisibleMenuRows,
                          std::max(1, (drawerHeight - contentListTop() - kDrawerListBottomPadding) / itemHeight));
  setupMenu();
}

int SettingsDrawer::snapEmbeddedHeight(int maxHeight) const {
  // Largest height <= maxHeight that fits a whole number of rows under the header, so the embedded
  // drawer has no dead space below the last row. This runs just before setEmbeddedRegion(), so use
  // the preset-editor tab header even though embedded_ is not set yet.
  const int listTop = kPresetTabHeight + 1;
  const int usable = maxHeight - listTop - kDrawerListBottomPadding;
  const int rows = std::min(kVisibleMenuRows, std::max(1, usable / itemHeight));
  return listTop + rows * itemHeight + kDrawerListBottomPadding;
}

void SettingsDrawer::syncLayoutFromRenderer() {
  if (embedded_) {
    // Keep the host-provided region; just recompute how many rows fit.
    itemsPerPage = std::min(kVisibleMenuRows,
                            std::max(1, (drawerHeight - contentListTop() - kDrawerListBottomPadding) / itemHeight));
    return;
  }
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  // Keep the live Book Settings surface as a bottom drawer. Its contents use the same
  // tabbed Reader Preset layout, but remain constrained to the reader drawer geometry.
  if (isLandscapeReader(renderer)) {
    drawerWidth = sw / 2;
    drawerX = sw - drawerWidth;
    drawerY = 0;
    drawerHeight = sh;
  } else {
    drawerX = 0;
    drawerWidth = sw;
    // Keep the book settings surface as a bottom drawer, but make it tall
    // enough for the same five visible rows as the preset editor. The old
    // fixed 50% height only fit four rows on the X3/X4 portrait display.
    const int fiveRowHeight = contentListTop() + kVisibleMenuRows * itemHeight + kDrawerListBottomPadding;
    drawerHeight = std::min(sh, std::max(sh * kPortraitDrawerHeightPercent / 100, fiveRowHeight));
    drawerY = sh - drawerHeight;
  }
  itemsPerPage = std::min(kVisibleMenuRows,
                          std::max(1, (drawerHeight - contentListTop() - kDrawerListBottomPadding) / itemHeight));
}

/**
 * @brief Sets up the menu structure based on current expansion states
 */
void SettingsDrawer::setupMenu() {
  menuItems.clear();

  {
    // Both the preset editor and the live book drawer use the Pro layout: each tab is a short,
    // flat list. The global reader-only settings remain in ReaderPresetsActivity and are not
    // duplicated here.
    auto addToggle = [&](const MenuItem item, const GroupType group, const char* name,
                         std::function<bool(const BookSettings&)> get, std::function<void(BookSettings&)> set) {
      MenuEntry entry;
      entry.item = item;
      entry.group = group;
      entry.name = name;
      entry.getValueText = [get](const BookSettings& s) -> const char* { return get(s) ? "On" : "Off"; };
      entry.change = [set](BookSettings& s, int) {
        set(s);
        s.markCustomSettings();
      };
      menuItems.push_back(std::move(entry));
    };

    if (selectedGroup_ == GroupType::FONT) {
      if (!embedded_) {
        MenuEntry preset;
        preset.item = MenuItem::PresetPicker;
        preset.group = GroupType::FONT;
        preset.name = "Preset";
        preset.getValueText = [](const BookSettings& s) -> const char* {
          static thread_local std::string name;
          if (s.readerPresetIndex == BookSettings::kNoReaderPreset) return "Custom";
          name = READER_PRESETS.nameOf(s.readerPresetIndex);
          return name.empty() ? "Custom" : name.c_str();
        };
        preset.change = [](BookSettings& s, const int delta) {
          const int count = READER_PRESETS.count();
          if (count <= 0) return;
          int selected = s.readerPresetIndex == BookSettings::kNoReaderPreset ? 0 : s.readerPresetIndex;
          selected = (selected + delta + count) % count;
          READER_PRESETS.applyToBook(selected, s);
        };
        menuItems.push_back(std::move(preset));
      }

      MenuEntry style;
      style.item = MenuItem::FontFamily;
      style.group = GroupType::FONT;
      style.name = "Style";
      style.getValueText = [](const BookSettings& s) -> const char* {
        thread_local std::string label;
        label = FontManager::readerFontFamilyLabel(s.fontFamily);
        return label.c_str();
      };
      style.change = [](BookSettings& s, int delta) {
        const int count = static_cast<int>(FontManager::readerFontFamilyOptionCount());
        if (count <= 0) return;
        int next = static_cast<int>(s.fontFamily) + delta;
        if (next < 0) next = count - 1;
        if (next >= count) next = 0;
        s.fontFamily = static_cast<uint8_t>(next);
        FontManager::clampReaderFontFamilySlot(s.fontFamily);
        s.markCustomSettings();
      };
      menuItems.push_back(std::move(style));

      MenuEntry size;
      size.item = MenuItem::FontSize;
      size.group = GroupType::FONT;
      size.name = "Size";
      size.getValueText = [](const BookSettings& s) -> const char* {
        static const char* values[] = {"Extra Small", "Small", "Medium", "Large", "X Large"};
        return values[s.fontSize <= 4 ? s.fontSize : 1];
      };
      size.change = [](BookSettings& s, int delta) {
        const int next = static_cast<int>(s.fontSize) + delta;
        if (next >= 0 && next <= 4) {
          s.fontSize = static_cast<uint8_t>(next);
          s.markCustomSettings();
        }
      };
      menuItems.push_back(std::move(size));

      MenuEntry alignment;
      alignment.item = MenuItem::Alignment;
      alignment.group = GroupType::FONT;
      alignment.name = "Alignment";
      alignment.getValueText = [](const BookSettings& s) -> const char* {
        static const char* values[] = {"Justify", "Left", "Center", "Right", "Book's style"};
        return values[s.paragraphAlignment <= 4 ? s.paragraphAlignment : 0];
      };
      alignment.change = [](BookSettings& s, int delta) {
        const int next = static_cast<int>(s.paragraphAlignment) + delta;
        if (next >= 0 && next <= 4) {
          s.paragraphAlignment = static_cast<uint8_t>(next);
          s.markCustomSettings();
        }
      };
      menuItems.push_back(std::move(alignment));
      addToggle(MenuItem::Hyphenation, GroupType::FONT, "Hyphenation",
                [](const BookSettings& s) { return s.hyphenationEnabled != 0; },
                [](BookSettings& s) { s.hyphenationEnabled = s.hyphenationEnabled ? 0 : 1; });

    } else if (selectedGroup_ == GroupType::LAYOUT) {
      MenuEntry lineHeight;
      lineHeight.item = MenuItem::LineHeight;
      lineHeight.group = GroupType::LAYOUT;
      lineHeight.name = "Line height";
      lineHeight.getValueText = [](const BookSettings& s) -> const char* {
        static char value[8];
        snprintf(value, sizeof(value), "%u%%", s.lineHeight);
        return value;
      };
      lineHeight.change = [](BookSettings& s, int delta) {
        const int next = std::max(10, std::min(200, static_cast<int>(s.lineHeight) + delta * 5));
        s.lineHeight = static_cast<uint8_t>(next);
        s.markCustomSettings();
      };
      menuItems.push_back(std::move(lineHeight));

      MenuEntry textSpace;
      textSpace.item = MenuItem::TextSpace;
      textSpace.group = GroupType::LAYOUT;
      textSpace.name = "Word spacing";
      textSpace.getValueText = [](const BookSettings& s) -> const char* {
        static char value[8];
        snprintf(value, sizeof(value), "%u%%", s.textSpace);
        return value;
      };
      textSpace.change = [](BookSettings& s, int delta) {
        const int next = std::max(10, std::min(200, static_cast<int>(s.textSpace) + delta * 5));
        s.textSpace = static_cast<uint8_t>(next);
        s.markCustomSettings();
      };
      menuItems.push_back(std::move(textSpace));

      addToggle(MenuItem::ExtraParagraphSpacing, GroupType::LAYOUT, "Extra Paragraph Spacing",
                [](const BookSettings& s) { return s.extraParagraphSpacing != 0; },
                [](BookSettings& s) { s.extraParagraphSpacing = s.extraParagraphSpacing ? 0 : 1; });
      addToggle(MenuItem::ParagraphCssIndent, GroupType::LAYOUT, "Indent",
                [](const BookSettings& s) { return s.paragraphCssIndentEnabled != 0; },
                [](BookSettings& s) { s.paragraphCssIndentEnabled = s.paragraphCssIndentEnabled ? 0 : 1; });

      MenuEntry margin;
      margin.item = MenuItem::ScreenMargin;
      margin.group = GroupType::LAYOUT;
      margin.name = "Screen Margin";
      margin.getValueText = [](const BookSettings& s) -> const char* {
        static char value[10];
        snprintf(value, sizeof(value), "%u px", s.screenMargin);
        return value;
      };
      margin.change = [](BookSettings& s, int delta) {
        const int next = std::max(0, std::min(80, static_cast<int>(s.screenMargin) + delta * 5));
        s.screenMargin = static_cast<uint8_t>(next);
        s.markCustomSettings();
      };
      menuItems.push_back(std::move(margin));
    } else if (selectedGroup_ == GroupType::CONTROLS) {
      MenuEntry orientation;
      orientation.item = MenuItem::ReadingOrientation;
      orientation.group = GroupType::CONTROLS;
      orientation.name = "Orientation";
      orientation.getValueText = [](const BookSettings& s) -> const char* {
        static const char* values[] = {"Portrait", "Landscape CW", "Inverted", "Landscape CCW"};
        return values[s.orientation <= 3 ? s.orientation : 0];
      };
      orientation.change = [](BookSettings& s, int delta) {
        const int next = static_cast<int>(s.orientation) + delta;
        if (next >= 0 && next <= 3) {
          s.orientation = static_cast<uint8_t>(next);
          s.markCustomSettings();
        }
      };
      menuItems.push_back(std::move(orientation));
      addToggle(MenuItem::BionicReading, GroupType::CONTROLS, "Bionic Reading",
                [](const BookSettings& s) { return s.bionicReadingEnabled != 0; },
                [](BookSettings& s) { s.bionicReadingEnabled = s.bionicReadingEnabled ? 0 : 1; });
      MenuEntry guide;
      guide.item = MenuItem::ReadingGuideLines;
      guide.group = GroupType::CONTROLS;
      guide.name = "Guide Lines";
      guide.getValueText = [](const BookSettings& s) -> const char* {
        static const char* values[] = {"Off", "Grid", "Notebook"};
        return values[s.readingGuideLinesEnabled <= 2 ? s.readingGuideLinesEnabled : 0];
      };
      guide.change = [](BookSettings& s, int delta) {
        const int next = static_cast<int>(s.readingGuideLinesEnabled) + delta;
        if (next >= 0 && next <= 2) {
          s.readingGuideLinesEnabled = static_cast<uint8_t>(next);
          s.markCustomSettings();
        }
      };
      menuItems.push_back(std::move(guide));
    } else if (selectedGroup_ == GroupType::STATUS_BAR) {
      const auto addStatus = [&](const MenuItem item, const char* name) {
        MenuEntry entry;
        entry.item = item;
        entry.group = GroupType::STATUS_BAR;
        entry.name = name;
        entry.getValueText = [item](const BookSettings& s) -> const char* {
          const StatusBarItem value = item == MenuItem::StatusBarLeft
                                          ? s.statusBarLeft.item
                                          : item == MenuItem::StatusBarMiddle ? s.statusBarMiddle.item
                                                                               : s.statusBarRight.item;
          return statusBarItemName(value);
        };
        entry.change = [item](BookSettings& s, int delta) {
          StatusBarItem& value = item == MenuItem::StatusBarLeft
                                     ? s.statusBarLeft.item
                                     : item == MenuItem::StatusBarMiddle ? s.statusBarMiddle.item
                                                                          : s.statusBarRight.item;
          const int next = static_cast<int>(value) + delta;
          if (next >= 0 && next < static_cast<int>(StatusBarItem::STATUS_BAR_ITEM_COUNT)) {
            value = static_cast<StatusBarItem>(next);
            s.markCustomSettings();
          }
        };
        menuItems.push_back(std::move(entry));
      };
      addStatus(MenuItem::StatusBarLeft, "Left Section");
      addStatus(MenuItem::StatusBarMiddle, "Middle Section");
      addStatus(MenuItem::StatusBarRight, "Right Section");

      // Full Bar is a global reader setting, so keep it separate from the three per-book
      // section values above. This is the same single-style control used by the Pro UI.
      MenuEntry fullStyle;
      fullStyle.item = MenuItem::StatusBarFullStyle;
      fullStyle.group = GroupType::STATUS_BAR;
      fullStyle.name = "Full Bar";
      fullStyle.getValueText = [](const BookSettings&) -> const char* {
        return statusBarItemName(static_cast<StatusBarItem>(READER_SETTINGS.statusBarFullStyle));
      };
      fullStyle.change = [](BookSettings&, int delta) {
        int current = 0;
        for (int i = 0; i < StatusBar::kFullBarStyleCount; ++i) {
          if (StatusBar::kFullBarStyles[i] == static_cast<StatusBarItem>(READER_SETTINGS.statusBarFullStyle)) {
            current = i;
            break;
          }
        }
        const int next = current + delta;
        if (next >= 0 && next < StatusBar::kFullBarStyleCount) {
          READER_SETTINGS.statusBarFullStyle = static_cast<uint8_t>(StatusBar::kFullBarStyles[next]);
          READER_SETTINGS.saveToFile();
        }
      };
      menuItems.push_back(std::move(fullStyle));
    }
    return;
  }

  // Per-book preset picker (hidden inside the preset editor, where settings ARE the preset being edited).
  if (!embedded_) {
    MenuEntry presetEntry;
    presetEntry.item = MenuItem::PresetPicker;
    presetEntry.group = GroupType::FONT;
    presetEntry.name = presetAppliedInDrawer_ ? "Preset in use" : "Apply preset";
    presetEntry.getValueText = [this](const BookSettings&) -> const char* {
      thread_local std::string tls;
      tls = ReaderPresetStore::getInstance().nameOf(presetPickIndex_);
      return tls.c_str();
    };
    presetEntry.change = [this](BookSettings&, int delta) {
      const int n = ReaderPresetStore::getInstance().count();
      if (n <= 0) return;
      presetPickIndex_ = ((presetPickIndex_ + delta) % n + n) % n;
      ReaderPresetStore::getInstance().applyToBook(presetPickIndex_, settings);
      presetAppliedInDrawer_ = true;
    };
    menuItems.push_back(presetEntry);
  }

  MenuEntry fontSeparator;
  fontSeparator.item = MenuItem::Separator;
  fontSeparator.group = GroupType::FONT;
  fontSeparator.name = "═══ Font ═══";
  fontSeparator.getValueText = [this](const BookSettings&) -> const char* {
    static char indicator[4];
    snprintf(indicator, sizeof(indicator), "%s", isGroupExpanded(GroupType::FONT) ? "-" : "+");
    return indicator;
  };
  fontSeparator.change = [](BookSettings&, int) {};
  menuItems.push_back(fontSeparator);

  if (isGroupExpanded(GroupType::FONT)) {
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
      s.markCustomSettings();
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
        s.markCustomSettings();
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
    snprintf(indicator, sizeof(indicator), "%s", isGroupExpanded(GroupType::LAYOUT) ? "-" : "+");
    return indicator;
  };
  layoutSeparator.change = [](BookSettings&, int) {};
  menuItems.push_back(layoutSeparator);

  if (isGroupExpanded(GroupType::LAYOUT)) {
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
      s.markCustomSettings();
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
      s.markCustomSettings();
    };
    menuItems.push_back(textSpaceEntry);

    MenuEntry alignEntry;
    alignEntry.item = MenuItem::Alignment;
    alignEntry.group = GroupType::LAYOUT;
    alignEntry.name = "Paragraph Alignment";
    alignEntry.getValueText = [](const BookSettings& s) -> const char* {
      static const char* align[] = {"Justify", "Left", "Center", "Right", "Book's style"};
      int index = s.paragraphAlignment;
      if (index > 4) index = 0;
      return align[index];
    };
    alignEntry.change = [](BookSettings& s, int delta) {
      int newVal = s.paragraphAlignment + delta;
      if (newVal >= 0 && newVal <= 4) {
        s.paragraphAlignment = newVal;
        s.markCustomSettings();
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
      s.markCustomSettings();
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
      s.markCustomSettings();
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
      if (newVal >= 0 && newVal <= 80) {
        s.screenMargin = newVal;
        s.markCustomSettings();
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
        s.markCustomSettings();
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
      s.markCustomSettings();
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
      s.markCustomSettings();
    };
    menuItems.push_back(bionicEntry);

    // Per-book (pure visual overlay, never baked into layout/cache) — same pattern as hyphenation/bionic above.
    // Grid lines sit at 25%/75% of content width so ~half the page is the fixation band.
    MenuEntry guideLinesEntry;
    guideLinesEntry.item = MenuItem::ReadingGuideLines;
    guideLinesEntry.group = GroupType::LAYOUT;
    guideLinesEntry.name = "Guide Lines";
    guideLinesEntry.getValueText = [](const BookSettings& s) -> const char* {
      static const char* styles[] = {"Off", "Grid", "Notebook"};
      int index = s.readingGuideLinesEnabled;
      if (index > 2) index = 0;
      return styles[index];
    };
    guideLinesEntry.change = [](BookSettings& s, int delta) {
      int newVal = s.readingGuideLinesEnabled + delta;
      if (newVal >= 0 && newVal <= 2) {
        s.readingGuideLinesEnabled = newVal;
        s.markCustomSettings();
      }
    };
    menuItems.push_back(guideLinesEntry);
  }

  // The "═══ System ═══" group (Text Anti-Aliasing, Refresh Frequency, Power Button, Long-press,
  // Page Auto Turn) and the "═══ Image ═══" group (Image Quality, Smart Refresh) moved out of this
  // per-book/per-preset drawer to ReaderPresetsActivity's own top-level "System" section (now single
  // global SystemSetting fields instead of per-book overrides - see textAntiAliasing/refreshFrequency/
  // pageAutoTurnSeconds/readerImageGrayscale/readerSmartRefreshOnImages there). Power Button and
  // Long-press specifically are further superseded by the per-button action mapping
  // (ReaderButtonBindings). Status Bar stays visible here for quick access while reading, but reads and
  // writes the global SystemSetting fields directly (READER_SETTINGS.statusBarLeft/Middle/Right, also editable
  // from ReaderPresetsActivity's System section) rather than a per-book BookSettings override - the
  // BookSettings& parameter these lambdas take is unused for that reason.

  MenuEntry statusBarSeparator;
  statusBarSeparator.item = MenuItem::StatusBarSeparator;
  statusBarSeparator.group = GroupType::STATUS_BAR;
  statusBarSeparator.name = "═══ Status Bar ═══";
  statusBarSeparator.getValueText = [this](const BookSettings&) -> const char* {
    static char indicator[4];
    snprintf(indicator, sizeof(indicator), "%s", isGroupExpanded(GroupType::STATUS_BAR) ? "-" : "+");
    return indicator;
  };
  statusBarSeparator.change = [](BookSettings&, int) {};
  menuItems.push_back(statusBarSeparator);

  if (isGroupExpanded(GroupType::STATUS_BAR)) {
    MenuEntry statusLeftEntry;
    statusLeftEntry.item = MenuItem::StatusBarLeft;
    statusLeftEntry.group = GroupType::STATUS_BAR;
    statusLeftEntry.name = "Left Section";
    statusLeftEntry.getValueText = [](const BookSettings&) -> const char* {
      return statusBarItemName(static_cast<StatusBarItem>(READER_SETTINGS.statusBarLeft));
    };
    statusLeftEntry.change = [](BookSettings&, int delta) {
      int newVal = static_cast<int>(READER_SETTINGS.statusBarLeft) + delta;
      if (newVal >= 0 && newVal < static_cast<int>(StatusBarItem::STATUS_BAR_ITEM_COUNT)) {
        READER_SETTINGS.statusBarLeft = static_cast<uint8_t>(newVal);
        READER_SETTINGS.saveToFile();
      }
    };
    menuItems.push_back(statusLeftEntry);

    MenuEntry statusMiddleEntry;
    statusMiddleEntry.item = MenuItem::StatusBarMiddle;
    statusMiddleEntry.group = GroupType::STATUS_BAR;
    statusMiddleEntry.name = "Middle Section";
    statusMiddleEntry.getValueText = [](const BookSettings&) -> const char* {
      return statusBarItemName(static_cast<StatusBarItem>(READER_SETTINGS.statusBarMiddle));
    };
    statusMiddleEntry.change = [](BookSettings&, int delta) {
      int newVal = static_cast<int>(READER_SETTINGS.statusBarMiddle) + delta;
      if (newVal >= 0 && newVal < static_cast<int>(StatusBarItem::STATUS_BAR_ITEM_COUNT)) {
        READER_SETTINGS.statusBarMiddle = static_cast<uint8_t>(newVal);
        READER_SETTINGS.saveToFile();
      }
    };
    menuItems.push_back(statusMiddleEntry);

    MenuEntry statusRightEntry;
    statusRightEntry.item = MenuItem::StatusBarRight;
    statusRightEntry.group = GroupType::STATUS_BAR;
    statusRightEntry.name = "Right Section";
    statusRightEntry.getValueText = [](const BookSettings&) -> const char* {
      return statusBarItemName(static_cast<StatusBarItem>(READER_SETTINGS.statusBarRight));
    };
    statusRightEntry.change = [](BookSettings&, int delta) {
      int newVal = static_cast<int>(READER_SETTINGS.statusBarRight) + delta;
      if (newVal >= 0 && newVal < static_cast<int>(StatusBarItem::STATUS_BAR_ITEM_COUNT)) {
        READER_SETTINGS.statusBarRight = static_cast<uint8_t>(newVal);
        READER_SETTINGS.saveToFile();
      }
    };
    menuItems.push_back(statusRightEntry);
  }

  MenuEntry statusBarFullSeparator;
  statusBarFullSeparator.item = MenuItem::StatusBarFullSeparator;
  statusBarFullSeparator.group = GroupType::STATUS_BAR_FULL;
  statusBarFullSeparator.name = "═══ Full Width Bar ═══";
  statusBarFullSeparator.getValueText = [this](const BookSettings&) -> const char* {
    static char indicator[4];
    snprintf(indicator, sizeof(indicator), "%s", isGroupExpanded(GroupType::STATUS_BAR_FULL) ? "-" : "+");
    return indicator;
  };
  statusBarFullSeparator.change = [](BookSettings&, int) {};
  menuItems.push_back(statusBarFullSeparator);

  if (isGroupExpanded(GroupType::STATUS_BAR_FULL)) {
    // A single style row, not 3 Left/Middle/Right rows - Full is one full-width bar restricted to
    // StatusBar::kFullBarStyles (loading/progress visualizations), so there's nothing to put in
    // separate sections.
    MenuEntry fullStyleEntry;
    fullStyleEntry.item = MenuItem::StatusBarFullStyle;
    fullStyleEntry.group = GroupType::STATUS_BAR_FULL;
    fullStyleEntry.name = "Style";
    fullStyleEntry.getValueText = [](const BookSettings&) -> const char* {
      return statusBarItemName(static_cast<StatusBarItem>(READER_SETTINGS.statusBarFullStyle));
    };
    fullStyleEntry.change = [](BookSettings&, int delta) {
      int idx = 0;
      for (int i = 0; i < StatusBar::kFullBarStyleCount; ++i) {
        if (StatusBar::kFullBarStyles[i] == static_cast<StatusBarItem>(READER_SETTINGS.statusBarFullStyle)) {
          idx = i;
          break;
        }
      }
      const int newIdx = idx + delta;
      if (newIdx >= 0 && newIdx < StatusBar::kFullBarStyleCount) {
        READER_SETTINGS.statusBarFullStyle = static_cast<uint8_t>(StatusBar::kFullBarStyles[newIdx]);
        READER_SETTINGS.saveToFile();
      }
    };
    menuItems.push_back(fullStyleEntry);
  }
}

/**
 * @brief Shows the settings drawer
 */
void SettingsDrawer::show() {
  if (visible) return;
  syncLayoutFromRenderer();
  if (!embedded_ && settings.readerPresetIndex != BookSettings::kNoReaderPreset) {
    const int n = ReaderPresetStore::getInstance().count();
    if (settings.readerPresetIndex < n) {
      presetPickIndex_ = settings.readerPresetIndex;
    }
  }
  visible = true;
  dismissed = false;
  closeSelector();
  selectedGroup_ = GroupType::FONT;
  rotateTabFocused_ = false;
  setupMenu();
  selectedIndex = -1;
  scrollOffset = 0;
  renderWithRefresh(HalDisplay::FAST_REFRESH);
}

/**
 * @brief Hides the settings drawer
 */
void SettingsDrawer::hide() {
  visible = false;
  dismissed = true;
  closeSelector();
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
  if (selectorOpen_) drawSelectorPopup();
  if (embedded_) {
    renderer.line.render(drawerX, drawerY + drawerHeight - 1, drawerX + drawerWidth, drawerY + drawerHeight - 1,
                         true);
    // Host owns the rest of the screen and the display push.
    if (onEmbeddedInvalidate_) onEmbeddedInvalidate_();
    return;
  }
  renderer.displayBuffer(mode);
}

/**
 * @brief Draws the background panel of the settings drawer
 */
void SettingsDrawer::drawBackground() {
  renderer.rectangle.fill(drawerX, drawerY, drawerWidth, drawerHeight, false);
  renderer.line.render(drawerX, drawerY, drawerX + drawerWidth, drawerY, true);
  drawTabs();
  renderer.line.render(drawerX, drawerY + drawerHeight - 1, drawerX + drawerWidth, drawerY + drawerHeight - 1,
                       true);
}

void SettingsDrawer::drawTabs() {
  struct Tab {
    GroupType group;
    const uint8_t* icon;
    bool rotateControl;
  };
  static constexpr Tab regularTabs[] = {
      {GroupType::FONT, PresetFont, false},
      {GroupType::LAYOUT, PresetBars, false},
      {GroupType::STATUS_BAR, PresetLayout, false},
      {GroupType::CONTROLS, PresetSettings, false},
  };
  static constexpr Tab inBookTabs[] = {
      {GroupType::FONT, PresetFont, false},
      {GroupType::LAYOUT, PresetBars, false},
      {GroupType::STATUS_BAR, PresetLayout, false},
      {GroupType::CONTROLS, PresetSettings, false},
      {GroupType::FONT, Rotate, true},
  };

  const Tab* tabs = embedded_ ? regularTabs : inBookTabs;
  const int count = embedded_ ? static_cast<int>(sizeof(regularTabs) / sizeof(regularTabs[0]))
                              : static_cast<int>(sizeof(inBookTabs) / sizeof(inBookTabs[0]));
  const int y = drawerY + 1;
  const int width = std::max(1, drawerWidth / count);
  for (int i = 0; i < count; ++i) {
    const int x = drawerX + i * width;
    const int w = i == count - 1 ? drawerX + drawerWidth - x : width;
    const bool selected = tabs[i].rotateControl ? rotateTabFocused_ :
                                                   (!rotateTabFocused_ && tabs[i].group == selectedGroup_);
    renderer.rectangle.fill(x, y, w, kPresetTabHeight, selected, false);
    renderer.bitmap.icon(tabs[i].icon, x + std::max(0, (w - kPresetTabSize) / 2), y + kPresetTabPadding,
                         kPresetTabSize, kPresetTabSize, BitmapRender::Orientation::None, selected);
  }
  for (int i = 1; i < count; ++i) {
    const int dividerX = drawerX + i * width;
    renderer.line.render(dividerX, y, dividerX, y + kPresetTabHeight, true, LineRender::Style::Dotted);
  }
  renderer.line.render(drawerX, y + kPresetTabHeight - 1, drawerX + drawerWidth, y + kPresetTabHeight - 1, true);

  // Match the settings shell: the small dot indicates that the tab strip, rather
  // than a row, currently owns focus.
  if (selectedIndex < 0) {
    int selectedTab = 0;
    for (int i = 0; i < count; ++i) {
      if (tabs[i].rotateControl && !embedded_) {
        // The rotate tab is a command rather than a settings group.
        // Its focus is tracked separately from selectedGroup_.
        if (rotateTabFocused_) selectedTab = i;
      } else if (!rotateTabFocused_ && tabs[i].group == selectedGroup_) {
        selectedTab = i;
        break;
      }
    }
    const int centerX = drawerX + selectedTab * width + width / 2;
    const int centerY = y + kPresetTabHeight + 6;
    constexpr int radius = 3;
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dx = -radius; dx <= radius; ++dx) {
        if (dx * dx + dy * dy <= radius * radius) renderer.drawPixel(centerX + dx, centerY + dy, true);
      }
    }
  }
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

  const int startY = drawerY + contentListTop();
  const int itemY = startY + (visibleRow * itemHeight);
  const auto& entry = menuItems[static_cast<size_t>(menuIndex)];
  const bool isSelected = (menuIndex == selectedIndex);
  const bool hasNextRow = visibleRow + 1 < itemsPerPage && menuIndex + 1 < static_cast<int>(menuItems.size());

  renderer.rectangle.fill(
      drawerX, itemY, drawerWidth, itemHeight,
      isSelected ? static_cast<int>(GfxRenderer::FillTone::Ink) : static_cast<int>(GfxRenderer::FillTone::Paper));

  if (entry.item == MenuItem::Separator || entry.item == MenuItem::StatusBarSeparator ||
      entry.item == MenuItem::StatusBarFullSeparator) {
    const int textX = drawerX + 15;
    const int textY = itemY + (itemHeight - renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID)) / 2;
    renderer.text.render(MONTSERRAT_10_FONT_ID, textX, textY, entry.name, isSelected ? 0 : 1);

    const char* indicator = entry.getValueText(settings);
    if (indicator && indicator[0] != '\0') {
      const int indicatorW = renderer.text.getWidth(MONTSERRAT_10_FONT_ID, indicator);
      renderer.text.render(MONTSERRAT_10_FONT_ID, drawerX + drawerWidth - indicatorW - 30, textY, indicator,
                           isSelected ? 0 : 1, EpdFontFamily::BOLD);
    }

    if (hasNextRow) {
      renderer.line.render(drawerX, itemY + itemHeight - 1, drawerX + drawerWidth, itemY + itemHeight - 1, true,
                           LineRender::Style::Dotted);
    }
    return;
  }

  const int textX = drawerX + 23;
  const int textY = itemY + (itemHeight - renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID)) / 2;
  renderer.text.render(MONTSERRAT_10_FONT_ID, textX, textY, entry.name, isSelected ? 0 : 1);

  const int valueColumnRight = drawerX + drawerWidth - 24;
  const int valueAreaLeft = drawerX + drawerWidth * 40 / 100;
  const bool dropdown = entry.item == MenuItem::FontFamily || entry.item == MenuItem::PresetPicker ||
                        entry.item == MenuItem::ReadingOrientation || entry.item == MenuItem::ReadingGuideLines ||
                        entry.item == MenuItem::StatusBarLeft || entry.item == MenuItem::StatusBarMiddle ||
                        entry.item == MenuItem::StatusBarRight || entry.item == MenuItem::StatusBarFullStyle;
  if (dropdown) {
    drawSettingsDropdown(renderer, valueAreaLeft, valueColumnRight, itemY, itemHeight,
                         entry.getValueText(settings), isSelected);
  } else if (entry.item == MenuItem::FontSize || entry.item == MenuItem::LineHeight ||
             entry.item == MenuItem::TextSpace || entry.item == MenuItem::ScreenMargin) {
    drawValueStepper(renderer, entry.getValueText(settings), valueAreaLeft, valueColumnRight, itemY, itemHeight,
                     isSelected);
  } else if (entry.item == MenuItem::Alignment) {
    int alignment = settings.paragraphAlignment;
    if (alignment < 0 || alignment > 4) alignment = 0;
    drawJustificationSegments(renderer, valueAreaLeft, valueColumnRight, itemY, itemHeight, alignment, isSelected);
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
        const int valW = renderer.text.getWidth(MONTSERRAT_10_FONT_ID, val);
        renderer.text.render(MONTSERRAT_10_FONT_ID, valueColumnRight - valW, textY, val, isSelected ? 0 : 1);
      }
    }
  }

  if (hasNextRow) {
    renderer.line.render(drawerX, itemY + itemHeight - 1, drawerX + drawerWidth, itemY + itemHeight - 1, true,
                         LineRender::Style::Dotted);
  }
}

bool SettingsDrawer::isDropdownItem(const MenuItem item) const {
  switch (item) {
    case MenuItem::FontFamily:
    case MenuItem::PresetPicker:
    case MenuItem::ReadingOrientation:
    case MenuItem::ReadingGuideLines:
    case MenuItem::StatusBarLeft:
    case MenuItem::StatusBarMiddle:
    case MenuItem::StatusBarRight:
    case MenuItem::StatusBarFullStyle:
      return true;
    default:
      return false;
  }
}

void SettingsDrawer::openSelector(const int menuIndex) {
  if (menuIndex < 0 || menuIndex >= static_cast<int>(menuItems.size()) ||
      !isDropdownItem(menuItems[static_cast<size_t>(menuIndex)].item)) {
    return;
  }

  selectorOptions_.clear();
  selectorPresetIndices_.clear();
  const MenuItem item = menuItems[static_cast<size_t>(menuIndex)].item;
  int current = 0;
  if (item == MenuItem::FontFamily) {
    selectorOptions_ = FontManager::readerFontFamilyEnumLabels();
    current = settings.fontFamily;
  } else if (item == MenuItem::PresetPicker) {
    const int count = READER_PRESETS.count();
    selectorOptions_.reserve(static_cast<size_t>(count));
    selectorPresetIndices_.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
      const std::string name = READER_PRESETS.nameOf(i);
      if (name.empty()) continue;
      selectorOptions_.push_back(name);
      selectorPresetIndices_.push_back(i);
    }
    const int currentStoreIndex =
        settings.readerPresetIndex == BookSettings::kNoReaderPreset ? 0 : settings.readerPresetIndex;
    current = 0;
    for (size_t i = 0; i < selectorPresetIndices_.size(); ++i) {
      if (selectorPresetIndices_[i] == currentStoreIndex) {
        current = static_cast<int>(i);
        break;
      }
    }
  } else if (item == MenuItem::ReadingOrientation) {
    selectorOptions_ = {"Portrait", "Landscape CW", "Inverted", "Landscape CCW"};
    current = settings.orientation;
  } else if (item == MenuItem::ReadingGuideLines) {
    selectorOptions_ = {"Off", "Grid", "Notebook"};
    current = settings.readingGuideLinesEnabled;
  } else if (item == MenuItem::StatusBarFullStyle) {
    for (int i = 0; i < StatusBar::kFullBarStyleCount; ++i) {
      selectorOptions_.emplace_back(statusBarItemName(StatusBar::kFullBarStyles[i]));
      if (StatusBar::kFullBarStyles[i] == static_cast<StatusBarItem>(READER_SETTINGS.statusBarFullStyle)) {
        current = i;
      }
    }
  } else {
    for (int i = 0; i < static_cast<int>(StatusBarItem::STATUS_BAR_ITEM_COUNT); ++i) {
      selectorOptions_.emplace_back(statusBarItemName(static_cast<StatusBarItem>(i)));
    }
    if (item == MenuItem::StatusBarLeft) current = static_cast<int>(settings.statusBarLeft.item);
    if (item == MenuItem::StatusBarMiddle) current = static_cast<int>(settings.statusBarMiddle.item);
    if (item == MenuItem::StatusBarRight) current = static_cast<int>(settings.statusBarRight.item);
  }

  if (selectorOptions_.empty()) return;
  selectorMenuIndex_ = menuIndex;
  selectorSelected_ = std::max(0, std::min(current, static_cast<int>(selectorOptions_.size()) - 1));
  const int selectedRow = selectorMenuIndex_ - scrollOffset;
  const int fieldY = drawerY + contentListTop() + selectedRow * itemHeight;
  const bool openUpward = selectorOpensUpward(drawerY, drawerHeight, fieldY, itemHeight);
  const int selectorRows = std::min(kSelectorRows, static_cast<int>(selectorOptions_.size()));
  const SelectorBounds box = selectorBounds(drawerX, drawerY, drawerWidth, drawerHeight, fieldY, itemHeight,
                                             selectorRows, openUpward);
  selectorScroll_ = std::max(0, selectorSelected_ - (box.rows - 1));
  selectorOpen_ = true;
}

void SettingsDrawer::closeSelector() {
  selectorOpen_ = false;
  selectorMenuIndex_ = -1;
  selectorSelected_ = 0;
  selectorScroll_ = 0;
  selectorOptions_.clear();
  selectorPresetIndices_.clear();
}

void SettingsDrawer::commitSelectorSelection() {
  if (!selectorOpen_ || selectorMenuIndex_ < 0 || selectorMenuIndex_ >= static_cast<int>(menuItems.size()) ||
      selectorSelected_ < 0 || selectorSelected_ >= static_cast<int>(selectorOptions_.size())) {
    closeSelector();
    return;
  }

  const MenuItem item = menuItems[static_cast<size_t>(selectorMenuIndex_)].item;
  if (item == MenuItem::FontFamily) {
    settings.fontFamily = static_cast<uint8_t>(selectorSelected_);
    FontManager::clampReaderFontFamilySlot(settings.fontFamily);
    settings.markCustomSettings();
    settingsUpdated = true;
  } else if (item == MenuItem::PresetPicker) {
    const int presetIndex = selectorPresetIndices_.empty()
                                ? selectorSelected_
                                : selectorPresetIndices_[static_cast<size_t>(selectorSelected_)];
    READER_PRESETS.applyToBook(presetIndex, settings);
    settingsUpdated = true;
    setupMenu();
  } else if (item == MenuItem::ReadingOrientation) {
    settings.orientation = static_cast<uint8_t>(selectorSelected_);
    settings.markCustomSettings();
  } else if (item == MenuItem::ReadingGuideLines) {
    settings.readingGuideLinesEnabled = static_cast<uint8_t>(selectorSelected_);
    settings.markCustomSettings();
  } else if (item == MenuItem::StatusBarLeft) {
    settings.statusBarLeft.item = static_cast<StatusBarItem>(selectorSelected_);
    settings.markCustomSettings();
    settingsUpdated = true;
  } else if (item == MenuItem::StatusBarMiddle) {
    settings.statusBarMiddle.item = static_cast<StatusBarItem>(selectorSelected_);
    settings.markCustomSettings();
    settingsUpdated = true;
  } else if (item == MenuItem::StatusBarRight) {
    settings.statusBarRight.item = static_cast<StatusBarItem>(selectorSelected_);
    settings.markCustomSettings();
    settingsUpdated = true;
  } else if (item == MenuItem::StatusBarFullStyle) {
    if (selectorSelected_ >= 0 && selectorSelected_ < StatusBar::kFullBarStyleCount) {
      READER_SETTINGS.statusBarFullStyle = static_cast<uint8_t>(StatusBar::kFullBarStyles[selectorSelected_]);
      READER_SETTINGS.saveToFile();
    }
  }
  closeSelector();
  if (onSettingsChanged) onSettingsChanged();
}

void SettingsDrawer::drawSelectorPopup() {
  if (!selectorOpen_ || selectorMenuIndex_ < 0 || selectorMenuIndex_ >= static_cast<int>(menuItems.size()) ||
      selectorOptions_.empty()) {
    return;
  }

  const int selectedRow = selectorMenuIndex_ - scrollOffset;
  const int fieldY = drawerY + contentListTop() + selectedRow * itemHeight;
  const bool openUpward = selectorOpensUpward(drawerY, drawerHeight, fieldY, itemHeight);
  const int selectorRows = std::min(kSelectorRows, static_cast<int>(selectorOptions_.size()));
  const SelectorBounds box = selectorBounds(drawerX, drawerY, drawerWidth, drawerHeight, fieldY, itemHeight,
                                             selectorRows, openUpward);
  renderer.rectangle.fill(box.x, box.y, box.width, box.height, false);
  for (int i = 0; i < box.rows; ++i) {
    const int optionIndex = selectorScroll_ + i;
    const int rowY = box.y + i * itemHeight;
    if (optionIndex < static_cast<int>(selectorOptions_.size())) {
      const bool selected = optionIndex == selectorSelected_;
      if (selected) renderer.rectangle.fill(box.x + 1, rowY, box.width - 2, itemHeight, true);
      const int textY = rowY + (itemHeight - renderer.text.getLineHeight(MONTSERRAT_10_FONT_ID)) / 2;
      const std::string shown = renderer.text.truncate(MONTSERRAT_10_FONT_ID,
                                                        selectorOptions_[static_cast<size_t>(optionIndex)].c_str(),
                                                        std::max(1, box.width - 24), EpdFontFamily::REGULAR);
      renderer.text.render(MONTSERRAT_10_FONT_ID, box.x + 12, textY, shown.c_str(), selected ? 0 : 1);
    }
    if (i + 1 < box.rows) {
      renderer.line.render(box.x, rowY + itemHeight, box.x + box.width, rowY + itemHeight, true,
                           LineRender::Style::Dotted);
    }
  }
  if (static_cast<int>(selectorOptions_.size()) > box.rows) {
    const int trackX = box.x + box.width - 5;
    const int trackY = box.y + 2;
    const int trackHeight = std::max(1, box.height - 5);
    const int total = static_cast<int>(selectorOptions_.size());
    const int thumbHeight = std::max(8, trackHeight * box.rows / total);
    const int range = std::max(1, total - box.rows);
    const int thumbRange = std::max(0, trackHeight - thumbHeight);
    renderer.rectangle.fill(trackX, trackY + thumbRange * selectorScroll_ / range, 2, thumbHeight, true);
  }
  renderer.rectangle.render(box.x, box.y, box.width, box.height, true);
}

bool SettingsDrawer::handleSelectorInput(MappedInputManager& input) {
  if (!selectorOpen_) return false;
  if (input.wasReleased(MappedInputManager::Button::Back)) {
    closeSelector();
    renderWithRefresh(HalDisplay::FAST_REFRESH);
    return true;
  }
  if (input.wasPressed(MappedInputManager::Button::Up) || input.wasPressed(MappedInputManager::Button::Down)) {
    const int count = static_cast<int>(selectorOptions_.size());
    const int direction = input.wasPressed(MappedInputManager::Button::Up) ? -1 : 1;
    selectorSelected_ = (selectorSelected_ + direction + count) % count;
    const int selectedRow = selectorMenuIndex_ - scrollOffset;
    const int fieldY = drawerY + contentListTop() + selectedRow * itemHeight;
    const bool openUpward = selectorOpensUpward(drawerY, drawerHeight, fieldY, itemHeight);
    const int selectorRows = std::min(kSelectorRows, static_cast<int>(selectorOptions_.size()));
    const SelectorBounds box = selectorBounds(drawerX, drawerY, drawerWidth, drawerHeight, fieldY, itemHeight,
                                               selectorRows, openUpward);
    if (selectorSelected_ < selectorScroll_) selectorScroll_ = selectorSelected_;
    if (selectorSelected_ >= selectorScroll_ + box.rows) selectorScroll_ = selectorSelected_ - box.rows + 1;
    renderWithRefresh(HalDisplay::FAST_REFRESH);
    return true;
  }
  if (input.wasPressed(MappedInputManager::Button::Confirm)) {
    commitSelectorSelection();
    renderWithRefresh(HalDisplay::FAST_REFRESH);
    return true;
  }
  return true;
}

/**
 * @brief Draws a scroll indicator when content exceeds visible area
 */
void SettingsDrawer::drawScrollIndicator() {
  int totalItems = static_cast<int>(menuItems.size());
  if (totalItems <= itemsPerPage) return;

  int startY = drawerY + contentListTop();
  int listHeight = itemsPerPage * itemHeight;
  int thumbH = (itemsPerPage * listHeight) / totalItems;
  int thumbY = startY + (scrollOffset * listHeight) / totalItems;

  renderer.rectangle.fill(drawerX + drawerWidth - 4, thumbY, 2, thumbH, true);
}

void SettingsDrawer::clearScrollIndicatorArea() {
  const int startY = drawerY + contentListTop();
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
  groupExpanded_[groupIndex(group)] = !groupExpanded_[groupIndex(group)];
  setupMenu();

  for (size_t i = 0; i < menuItems.size(); i++) {
    if (menuItems[i].group == group &&
        (menuItems[i].item == MenuItem::Separator || menuItems[i].item == MenuItem::StatusBarSeparator ||
         menuItems[i].item == MenuItem::StatusBarFullSeparator)) {
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

void SettingsDrawer::selectGroup(const GroupType group) {
  rotateTabFocused_ = false;
  if (selectedGroup_ == group) return;
  selectedGroup_ = group;
  selectedIndex = -1;
  scrollOffset = 0;
  closeSelector();
  setupMenu();
}

/**
 * @brief Handles input for the settings drawer
 * @param input Reference to the input manager
 */
void SettingsDrawer::handleInput(MappedInputManager& input) {
  if (!visible) return;

  uint32_t currentTime = xTaskGetTickCount();
  if (selectorOpen_) {
    handleSelectorInput(input);
    lastInputTime = currentTime;
    return;
  }
  if (currentTime - lastInputTime < pdMS_TO_TICKS(150)) {
    return;
  }

  {
    const bool previousTab = input.wasPressed(MappedInputManager::Button::Left);
    const bool nextTab = input.wasPressed(MappedInputManager::Button::Right);
    if (previousTab || nextTab) {
      // Once a row is focused, Left/Right operate the control in that row. This is
      // how the pro steppers and alignment selector are driven without touch.
      if (selectedIndex >= 0 && selectedIndex < static_cast<int>(menuItems.size())) {
        applyChange(previousTab ? -1 : 1);
        lastInputTime = currentTime;
        renderWithRefresh(HalDisplay::FAST_REFRESH);
        return;
      }

      static constexpr GroupType tabs[] = {
          GroupType::FONT,
          GroupType::LAYOUT,
          GroupType::STATUS_BAR,
          GroupType::CONTROLS,
      };
      int tab = rotateTabFocused_ ? 4 : 0;
      if (!rotateTabFocused_) {
        for (int i = 0; i < static_cast<int>(sizeof(tabs) / sizeof(tabs[0])); ++i) {
          if (tabs[i] == selectedGroup_) {
            tab = i;
            break;
          }
        }
      }
      const int count = embedded_ ? static_cast<int>(sizeof(tabs) / sizeof(tabs[0])) : 5;
      tab = previousTab ? (tab - 1 + count) % count : (tab + 1) % count;
      if (!embedded_ && tab == 4) {
        rotateTabFocused_ = true;
        selectedIndex = -1;
        closeSelector();
      } else {
        selectGroup(tabs[tab]);
      }
      lastInputTime = currentTime;
      renderWithRefresh(HalDisplay::FAST_REFRESH);
      return;
    }

    const bool previousItem = input.wasPressed(MappedInputManager::Button::Up);
    const bool nextItem = input.wasPressed(MappedInputManager::Button::Down);
    if (previousItem || nextItem) {
      // Rotate is a command tab with no rows behind it; keep Up/Down on the
      // header instead of falling through to the currently selected group.
      if (rotateTabFocused_) {
        lastInputTime = currentTime;
        renderWithRefresh(HalDisplay::FAST_REFRESH);
        return;
      }
      const int totalItems = static_cast<int>(menuItems.size());
      if (totalItems > 0) {
        if (selectedIndex < 0) {
          // The header is a real focus target. Down enters the first row; Up
          // keeps focus on the tab strip instead of wrapping to the last row.
          if (previousItem) {
            lastInputTime = currentTime;
            renderWithRefresh(HalDisplay::FAST_REFRESH);
            return;
          }
          selectedIndex = 0;
        } else if (previousItem && selectedIndex == 0) {
          selectedIndex = -1;
          scrollOffset = 0;
        } else {
          selectedIndex = previousItem ? (selectedIndex - 1 + totalItems) % totalItems
                                       : (selectedIndex + 1) % totalItems;
        }
        const int maxScroll = std::max(0, totalItems - itemsPerPage);
        if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
        if (selectedIndex >= scrollOffset + itemsPerPage) scrollOffset = selectedIndex - itemsPerPage + 1;
        scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));
      }
      lastInputTime = currentTime;
      renderWithRefresh(HalDisplay::FAST_REFRESH);
      return;
    }

    if (input.wasPressed(MappedInputManager::Button::Confirm)) {
      if (selectedIndex < 0) {
        static constexpr GroupType tabs[] = {
            GroupType::FONT,
            GroupType::LAYOUT,
            GroupType::STATUS_BAR,
            GroupType::CONTROLS,
        };
        int tab = rotateTabFocused_ ? 4 : 0;
        if (!rotateTabFocused_) {
          for (int i = 0; i < static_cast<int>(sizeof(tabs) / sizeof(tabs[0])); ++i) {
            if (tabs[i] == selectedGroup_) {
              tab = i;
              break;
            }
          }
        }
        const int count = embedded_ ? static_cast<int>(sizeof(tabs) / sizeof(tabs[0])) : 5;
        const int nextTab = (tab + 1) % count;
        if (!embedded_ && rotateTabFocused_) {
          settings.orientation = SystemSetting::LANDSCAPE_CCW;
          settings.markCustomSettings();
          settingsUpdated = true;
          hide();
        } else if (!embedded_ && nextTab == 4) {
          rotateTabFocused_ = true;
          selectedIndex = -1;
          closeSelector();
        } else {
          selectGroup(tabs[nextTab]);
        }
        lastInputTime = currentTime;
        renderWithRefresh(HalDisplay::FAST_REFRESH);
      } else if (selectedIndex < static_cast<int>(menuItems.size())) {
        if (isDropdownItem(menuItems[static_cast<size_t>(selectedIndex)].item)) {
          openSelector(selectedIndex);
        } else {
          applyChange(1);
        }
        lastInputTime = currentTime;
        renderWithRefresh(HalDisplay::FAST_REFRESH);
      }
      return;
    }

    if (input.wasReleased(MappedInputManager::Button::Back)) {
      if (!embedded_ && rotateTabFocused_) {
        settings.orientation = SystemSetting::LANDSCAPE_CCW;
        settings.markCustomSettings();
        settingsUpdated = true;
        hide();
        lastInputTime = currentTime;
        return;
      }
      if (selectedIndex >= 0) {
        // Back first returns focus to the active tab. The dot under its icon
        // then makes the header focus visible before the drawer is dismissed.
        selectedIndex = -1;
        scrollOffset = 0;
        renderWithRefresh(HalDisplay::FAST_REFRESH);
      } else {
        hide();
      }
      lastInputTime = currentTime;
      return;
    }
    return;
  }

  bool needRedraw = false;

  if (readSettingsListPrev(input, renderer)) {
    const int previousIndex = selectedIndex;
    const int totalItems = static_cast<int>(menuItems.size());
    if (totalItems > 0) {
      selectedIndex = (selectedIndex - 1 + totalItems) % totalItems;
      const int maxScroll = std::max(0, totalItems - itemsPerPage);
      const bool scrolled = selectedIndex < scrollOffset || selectedIndex >= scrollOffset + itemsPerPage;
      if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
      } else if (selectedIndex >= scrollOffset + itemsPerPage) {
        scrollOffset = std::min(selectedIndex - itemsPerPage + 1, maxScroll);
      }
      scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));
      if (scrolled) {
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
    const int totalItems = static_cast<int>(menuItems.size());
    if (totalItems > 0) {
      selectedIndex = (selectedIndex + 1) % totalItems;
      const int maxScroll = std::max(0, totalItems - itemsPerPage);
      const bool scrolled = selectedIndex < scrollOffset || selectedIndex >= scrollOffset + itemsPerPage;
      if (selectedIndex < scrollOffset) {
        scrollOffset = selectedIndex;
      } else if (selectedIndex >= scrollOffset + itemsPerPage) {
        scrollOffset = std::min(selectedIndex - itemsPerPage + 1, maxScroll);
      }
      scrollOffset = std::max(0, std::min(scrollOffset, maxScroll));
      if (scrolled) {
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
      if (selected.item == MenuItem::Separator || selected.item == MenuItem::StatusBarSeparator ||
          selected.item == MenuItem::StatusBarFullSeparator) {
        toggleGroup(selected.group);
        needRedraw = true;
      }
    }
  }

  if (input.wasReleased(MappedInputManager::Button::Back)) {
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
  const MenuItem selectedItem = menuItems[selectedIndex].item;
  menuItems[selectedIndex].change(settings, delta);

  if (selectedItem == MenuItem::PresetPicker) {
    settingsUpdated = true;
    setupMenu();
    if (selectedIndex >= static_cast<int>(menuItems.size())) {
      selectedIndex = std::max(0, static_cast<int>(menuItems.size()) - 1);
    }
  } else {
    switch (selectedItem) {
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
      case MenuItem::ReaderPowerButton:
      // Pure visual overlay — never affects text layout/pagination, so it never needs the expensive
      // full-page rebuild that settingsUpdated triggers, just the normal redraw that already happens.
      case MenuItem::ReadingGuideLines:
        break;
      case MenuItem::StatusBarLeft:
      case MenuItem::StatusBarMiddle:
      case MenuItem::StatusBarRight:
      case MenuItem::StatusBarFullStyle:
        settingsUpdated = true;
        break;
      case MenuItem::Hyphenation:
      case MenuItem::RefreshRate:
      case MenuItem::AntiAliasing:
      case MenuItem::ChapterSkip:
      case MenuItem::NavigationLock:
      case MenuItem::Separator:
      case MenuItem::StatusBarSeparator:
      case MenuItem::StatusBarFullSeparator:
      case MenuItem::PresetPicker:
        break;
    }
  }

  if (selectedItem != MenuItem::ReaderPowerButton && onSettingsChanged) onSettingsChanged();
}
