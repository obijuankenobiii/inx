#pragma once

/**
 * @file BookSetting.h
 * @brief Public interface and types for BookSetting.
 */

#include <SDCardManager.h>

#include <cstdint>
#include <string>

#include "state/SystemSetting.h"
#include "system/FontManager.h"

/**
 * @brief Status bar item types for display sections
 */
enum class StatusBarItem {
  NONE,                       ///< Nothing displayed
  PAGE_NUMBERS,               ///< Current page / total pages (e.g., "5/120")
  PERCENTAGE,                 ///< Reading percentage (e.g., "42%")
  CHAPTER_TITLE,              ///< Current chapter title
  BATTERY_ICON,               ///< Battery icon only
  BATTERY_PERCENTAGE,         ///< Battery percentage text only
  BATTERY_ICON_WITH_PERCENT,  ///< Battery icon with percentage
  PROGRESS_BAR,               ///< Horizontal progress bar
  PROGRESS_BAR_WITH_PERCENT,  ///< Progress bar with percentage
  PAGE_BARS,                  ///< Vertical bars representing pages
  BOOK_TITLE,                 ///< Book title
  AUTHOR_NAME,                ///< Author name
  PAGE_NUMBERS_WITH_PERCENT,  ///< Page numbers and percentage combined (e.g., "12/340 45%")
  STATUS_BAR_ITEM_COUNT
};

/**
 * @brief Configuration for a single status bar section
 */
struct StatusBarSectionConfig {
  StatusBarItem item = StatusBarItem::NONE;

  /**
   * @brief Serializes the config to bytes
   * @param data Output buffer
   * @param offset Current offset in buffer
   */
  void toBytes(uint8_t* data, size_t& offset) const { data[offset++] = static_cast<uint8_t>(item); }

  /**
   * @brief Deserializes the config from bytes
   * @param data Input buffer
   * @param offset Current offset in buffer
   */
  void fromBytes(const uint8_t* data, size_t& offset) { item = static_cast<StatusBarItem>(data[offset++]); }

  /**
   * @brief Equality operator
   * @param other Config to compare with
   * @return true if equal
   */
  bool operator==(const StatusBarSectionConfig& other) const { return item == other.item; }

  /**
   * @brief Inequality operator
   * @param other Config to compare with
   * @return true if not equal
   */
  bool operator!=(const StatusBarSectionConfig& other) const { return !(*this == other); }
};

/**
 * @brief Complete status bar layout with left, middle, and right sections
 */
struct StatusBarLayout {
  StatusBarSectionConfig left;    ///< Left status bar section
  StatusBarSectionConfig middle;  ///< Middle status bar section
  StatusBarSectionConfig right;   ///< Right status bar section
};

/**
 * @brief Per-book reading settings
 */
struct BookSettings {
  
  uint8_t fontFamily = SystemSetting::LITERATA;           ///< Font family
  uint8_t fontSize = SystemSetting::SMALL;                ///< Font size
  uint8_t lineHeight = 100;                              ///< Line height, % of natural (10-200)
  uint8_t textSpace = 100;                               ///< Word spacing, % of natural (10-200)
  uint8_t paragraphAlignment = SystemSetting::JUSTIFIED;  ///< Paragraph alignment
  /** Honor CSS `text-indent` when on (mirrors global "Indent" when unset in per-book file). */
  uint8_t paragraphCssIndentEnabled = 0;

  
  uint8_t extraParagraphSpacing = 1;  ///< Extra paragraph spacing enabled
  uint8_t textAntiAliasing = 0;       ///< Text anti-aliasing enabled
  uint8_t hyphenationEnabled = 1;     ///< Hyphenation enabled
  uint8_t bionicReadingEnabled = 0;   ///< Bionic Reading enabled

  
  uint8_t screenMargin = 20;  ///< Screen margin in pixels

  
  uint8_t orientation = SystemSetting::PORTRAIT;  ///< Screen orientation

  
  /** Same values as SystemSetting::LONG_PRESS_* (0=off, 1=chapter skip, 2=skip 5 pages). */
  uint8_t longPressChapterSkip = SystemSetting::LONG_PRESS_CHAPTER_SKIP;

  
  uint8_t refreshFrequency = 15;  ///< Screen refresh frequency in pages

  
  StatusBarSectionConfig statusBarLeft;    ///< Left status bar section
  StatusBarSectionConfig statusBarMiddle;  ///< Middle status bar section
  StatusBarSectionConfig statusBarRight;   ///< Right status bar section

  /**
   * @brief Page auto-turn interval in seconds
   * @details Values: 0 = off, increments of 10 (10, 20, 30, 40, 50, 60)
   */
  uint8_t pageAutoTurnSeconds = 0;
  uint8_t readerImageGrayscale = SystemSetting::READER_IMAGE_LOW;
  uint8_t readerSmartRefreshOnImages = 1;

  bool useCustomSettings = false;  ///< Whether custom settings are active

  /**
   * @brief Complete layout structure
   */
  struct Layout {
    StatusBarSectionConfig left;    ///< Left section config
    StatusBarSectionConfig middle;  ///< Middle section config
    StatusBarSectionConfig right;   ///< Right section config
  };

  /**
   * @brief Gets the complete status bar layout
   * @return Layout containing all three sections
   */
  Layout getStatusBarLayout() const { return {statusBarLeft, statusBarMiddle, statusBarRight}; }

  /**
   * @brief Sets the complete status bar layout
   * @param layout Layout to apply
   */
  void setStatusBarLayout(const Layout& layout) {
    statusBarLeft = layout.left;
    statusBarMiddle = layout.middle;
    statusBarRight = layout.right;
  }

  /**
   * @brief Number of bytes a serialized BookSettings record occupies.
   */
  static constexpr size_t kLegacySerializedSize = 18;
  static constexpr size_t kSerializedSize = 20;

  /**
   * @brief Writes the settings fields into a byte buffer (shared by settings.bin and the preset store).
   * @param data Output buffer (needs at least kSerializedSize bytes free at offset)
   * @param offset Current offset, advanced past the written bytes
   */
  void serialize(uint8_t* data, size_t& offset) const {
    data[offset++] = fontFamily;
    data[offset++] = fontSize;
    data[offset++] = lineHeight;
    data[offset++] = extraParagraphSpacing;
    data[offset++] = paragraphAlignment;
    data[offset++] = hyphenationEnabled;
    data[offset++] = screenMargin;
    data[offset++] = refreshFrequency;
    data[offset++] = longPressChapterSkip;
    data[offset++] = textAntiAliasing;
    data[offset++] = orientation;

    statusBarLeft.toBytes(data, offset);
    statusBarMiddle.toBytes(data, offset);
    statusBarRight.toBytes(data, offset);

    data[offset++] = pageAutoTurnSeconds;
    data[offset++] = paragraphCssIndentEnabled;
    data[offset++] = bionicReadingEnabled;
    data[offset++] = textSpace;
    data[offset++] = readerImageGrayscale;
    data[offset++] = readerSmartRefreshOnImages;
  }

  /**
   * @brief Reads settings fields from a byte buffer, clamping/back-filling like the legacy file loader.
   * @param data Input buffer
   * @param bytesAvailable Number of valid bytes in the buffer
   * @param offset Current offset, advanced past the consumed bytes
   * @return true if at least the 11-byte baseline was parsed
   */
  bool deserialize(const uint8_t* data, size_t bytesAvailable, size_t& offset) {
    if (bytesAvailable < offset + 11) {
      return false;
    }

    fontFamily = data[offset++];
    if (fontFamily < SystemSetting::FONT_FAMILY_BUILTIN_COUNT) {
      /** Legacy enum had Bookerly=0, Atkinson=1, Literata=2; map non-Atkinson to Literata (0). */
      if (fontFamily != SystemSetting::ATKINSON_HYPERLEGIBLE) {
        fontFamily = SystemSetting::LITERATA;
      }
    }
    FontManager::clampReaderFontFamilySlot(fontFamily);
    fontSize = data[offset++];
    // Legacy files stored the lineSpacing enum (0-4) in this slot; migrate those to default 100.
    lineHeight = data[offset++];
    if (lineHeight < 10 || lineHeight > 200) {
      lineHeight = 100;
    }
    extraParagraphSpacing = data[offset++];
    paragraphAlignment = data[offset++];
    hyphenationEnabled = data[offset++];
    screenMargin = data[offset++];
    refreshFrequency = data[offset++];
    longPressChapterSkip = data[offset++];
    if (longPressChapterSkip > SystemSetting::LONG_PRESS_PAGE_SKIP_5) {
      longPressChapterSkip = SystemSetting::LONG_PRESS_CHAPTER_SKIP;
    }
    textAntiAliasing = data[offset++];
    orientation = data[offset++];

    if (bytesAvailable >= offset + 3) {
      statusBarLeft.fromBytes(data, offset);
      statusBarMiddle.fromBytes(data, offset);
      statusBarRight.fromBytes(data, offset);
    }

    if (bytesAvailable >= offset + 1) {
      pageAutoTurnSeconds = data[offset++];
      if (pageAutoTurnSeconds > 60 || pageAutoTurnSeconds % 10 != 0) {
        pageAutoTurnSeconds = 0;
      }
    } else {
      pageAutoTurnSeconds = 0;
    }

    if (bytesAvailable >= offset + 1) {
      paragraphCssIndentEnabled = data[offset++];
      if (paragraphCssIndentEnabled > 1) {
        paragraphCssIndentEnabled = 1;
      }
    } else {
      paragraphCssIndentEnabled = SystemSetting::getInstance().paragraphCssIndentEnabled;
    }

    if (bytesAvailable >= offset + 1) {
      bionicReadingEnabled = data[offset++] ? 1 : 0;
    } else {
      bionicReadingEnabled = 0;
    }

    if (bytesAvailable >= offset + 1) {
      textSpace = data[offset++];
      if (textSpace < 10 || textSpace > 200) {
        textSpace = 100;
      }
    } else {
      textSpace = 100;
    }

    if (bytesAvailable >= offset + 1) {
      readerImageGrayscale = data[offset++];
      if (readerImageGrayscale >= SystemSetting::READER_IMAGE_QUALITY_COUNT) {
        readerImageGrayscale = SystemSetting::READER_IMAGE_LOW;
      }
    } else {
      readerImageGrayscale = SystemSetting::getInstance().readerImageGrayscale;
    }

    if (bytesAvailable >= offset + 1) {
      readerSmartRefreshOnImages = data[offset++] ? 1 : 0;
    } else {
      readerSmartRefreshOnImages = SystemSetting::getInstance().readerSmartRefreshOnImages ? 1 : 0;
    }

    return true;
  }

  /**
   * @brief Loads book settings from file
   * @param bookCachePath Path to book cache directory
   * @return true if load successful
   */
  bool loadFromFile(const std::string& bookCachePath) {
    std::string settingsPath = bookCachePath + "/settings.bin";
    FsFile f;
    if (SdMan.openFileForRead("BST", settingsPath.c_str(), f)) {
      size_t fileSize = f.size();

      if (fileSize >= 11) {
        uint8_t data[64];
        size_t bytesRead = f.read(data, std::min(fileSize, sizeof(data)));
        size_t offset = 0;
        if (deserialize(data, bytesRead, offset)) {
          useCustomSettings = true;
          f.close();
          return true;
        }
      }
      f.close();
    }

    loadFromGlobalSettings();
    useCustomSettings = false;
    return false;
  }

  /**
   * @brief Saves book settings to file
   * @param bookCachePath Path to book cache directory
   * @return true if save successful
   */
  bool saveToFile(const std::string& bookCachePath) {
    FontManager::clampReaderFontFamilySlot(fontFamily);
    if (lineHeight < 10 || lineHeight > 200) lineHeight = 100;
    if (textSpace < 10 || textSpace > 200) textSpace = 100;
    std::string settingsPath = bookCachePath + "/settings.bin";
    FsFile f;
    if (SdMan.openFileForWrite("BST", settingsPath.c_str(), f)) {
      uint8_t data[32];
      size_t offset = 0;
      serialize(data, offset);

      bool success = (f.write(data, offset) == offset);
      f.close();

      if (success) {
        useCustomSettings = true;
      }
      return success;
    }
    return false;
  }

  /**
   * @brief Loads settings from global SystemSetting
   */
  void loadFromGlobalSettings() {
    SystemSetting& global = SystemSetting::getInstance();
    fontFamily = global.fontFamily;
    fontSize = global.fontSize;
    lineHeight = global.lineHeight;
    textSpace = global.textSpace;
    extraParagraphSpacing = global.extraParagraphSpacing;
    paragraphAlignment = global.paragraphAlignment;
    paragraphCssIndentEnabled = global.paragraphCssIndentEnabled;
    hyphenationEnabled = global.hyphenationEnabled;
    bionicReadingEnabled = global.bionicReadingEnabled;
    screenMargin = global.screenMargin;

    switch (global.refreshFrequency) {
      case SystemSetting::REFRESH_1:
        refreshFrequency = 1;
        break;
      case SystemSetting::REFRESH_5:
        refreshFrequency = 5;
        break;
      case SystemSetting::REFRESH_10:
        refreshFrequency = 10;
        break;
      case SystemSetting::REFRESH_15:
        refreshFrequency = 15;
        break;
      case SystemSetting::REFRESH_30:
        refreshFrequency = 30;
        break;
      default:
        refreshFrequency = 15;
        break;
    }

    longPressChapterSkip = global.longPressChapterSkip;
    textAntiAliasing = global.textAntiAliasing;
    orientation = global.orientation;
    pageAutoTurnSeconds = global.pageAutoTurnSeconds;
    readerImageGrayscale = global.readerImageGrayscale;
    readerSmartRefreshOnImages = global.readerSmartRefreshOnImages ? 1 : 0;

    statusBarLeft.item = static_cast<StatusBarItem>(global.statusBarLeft);
    statusBarMiddle.item = static_cast<StatusBarItem>(global.statusBarMiddle);
    statusBarRight.item = static_cast<StatusBarItem>(global.statusBarRight);
  }

  /**
   * @brief Writes these settings back into the global SystemSetting reader fields (inverse of
   *        loadFromGlobalSettings). Does NOT persist — caller should SETTINGS.saveToFile().
   */
  void applyToGlobalSettings() const {
    SystemSetting& global = SystemSetting::getInstance();
    global.fontFamily = fontFamily;
    global.fontSize = fontSize;
    global.lineHeight = lineHeight;
    global.textSpace = textSpace;
    global.extraParagraphSpacing = extraParagraphSpacing;
    global.paragraphAlignment = paragraphAlignment;
    global.paragraphCssIndentEnabled = paragraphCssIndentEnabled;
    global.hyphenationEnabled = hyphenationEnabled;
    global.bionicReadingEnabled = bionicReadingEnabled;
    global.screenMargin = screenMargin;

    switch (refreshFrequency) {
      case 1:
        global.refreshFrequency = SystemSetting::REFRESH_1;
        break;
      case 5:
        global.refreshFrequency = SystemSetting::REFRESH_5;
        break;
      case 10:
        global.refreshFrequency = SystemSetting::REFRESH_10;
        break;
      case 30:
        global.refreshFrequency = SystemSetting::REFRESH_30;
        break;
      case 15:
      default:
        global.refreshFrequency = SystemSetting::REFRESH_15;
        break;
    }

    global.longPressChapterSkip = longPressChapterSkip;
    global.textAntiAliasing = textAntiAliasing;
    global.orientation = orientation;
    global.pageAutoTurnSeconds = pageAutoTurnSeconds;
    global.readerImageGrayscale = readerImageGrayscale;
    global.readerSmartRefreshOnImages = readerSmartRefreshOnImages ? 1 : 0;

    global.statusBarLeft = static_cast<uint8_t>(statusBarLeft.item);
    global.statusBarMiddle = static_cast<uint8_t>(statusBarMiddle.item);
    global.statusBarRight = static_cast<uint8_t>(statusBarRight.item);
  }

  /**
   * @brief Gets reader font ID based on current settings
   * @return Font identifier for rendering
   */
  int getReaderFontId() const {
    SystemSetting& global = SystemSetting::getInstance();
    uint8_t oldFam = global.fontFamily;
    uint8_t oldSize = global.fontSize;
    global.fontFamily = this->fontFamily;
    global.fontSize = this->fontSize;
    int id = global.getReaderFontId();
    global.fontFamily = oldFam;
    global.fontSize = oldSize;
    return id;
  }

  /**
   * @brief Line compression factor (lineHeight/100, 100 = the font's natural line height).
   */
  float getReaderLineCompression() const {
    uint8_t lh = lineHeight;
    if (lh < 10 || lh > 200) lh = 100;
    return static_cast<float>(lh) / 100.0f;
  }

  /**
   * @brief Word-spacing factor (textSpace/100, 100 = the natural inter-word space).
   */
  float getReaderWordSpacingFactor() const {
    uint8_t ts = textSpace;
    if (ts < 10 || ts > 200) ts = 100;
    return static_cast<float>(ts) / 100.0f;
  }

  /**
   * @brief Equality operator for comparison
   * @param other Settings to compare with
   * @return true if all settings match
   */
  bool operator==(const BookSettings& other) const {
    return fontFamily == other.fontFamily && fontSize == other.fontSize && lineHeight == other.lineHeight &&
           textSpace == other.textSpace &&
           paragraphAlignment == other.paragraphAlignment &&
           paragraphCssIndentEnabled == other.paragraphCssIndentEnabled &&
           extraParagraphSpacing == other.extraParagraphSpacing &&
           textAntiAliasing == other.textAntiAliasing && hyphenationEnabled == other.hyphenationEnabled &&
           bionicReadingEnabled == other.bionicReadingEnabled &&
           screenMargin == other.screenMargin && orientation == other.orientation &&
           longPressChapterSkip == other.longPressChapterSkip && refreshFrequency == other.refreshFrequency &&
           pageAutoTurnSeconds == other.pageAutoTurnSeconds && statusBarLeft == other.statusBarLeft &&
           statusBarMiddle == other.statusBarMiddle && statusBarRight == other.statusBarRight;
  }

  /**
   * @brief Inequality operator for comparison
   * @param other Settings to compare with
   * @return true if any setting differs
   */
  bool operator!=(const BookSettings& other) const { return !(*this == other); }
};
