#pragma once

#include <SDCardManager.h>

#include <cstdint>
#include <string>

#include "state/SystemSetting.h"

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
  // Font and text settings
  uint8_t fontFamily = SystemSetting::LITERATA;           ///< Font family
  uint8_t fontSize = SystemSetting::SMALL;                ///< Font size
  uint8_t lineSpacing = SystemSetting::NORMAL;            ///< Line spacing
  uint8_t paragraphAlignment = SystemSetting::JUSTIFIED;  ///< Paragraph alignment
  /** Honor EPUB/CSS `text-indent` when not using "Use EPUB CSS" alignment (mirrors global when unset in file). */
  uint8_t paragraphCssIndentEnabled = 0;

  // Text rendering settings
  uint8_t extraParagraphSpacing = 1;  ///< Extra paragraph spacing enabled
  uint8_t textAntiAliasing = 1;       ///< Text anti-aliasing enabled
  uint8_t hyphenationEnabled = 1;     ///< Hyphenation enabled

  // Reader screen margin settings
  uint8_t screenMargin = 20;  ///< Screen margin in pixels

  // Reading orientation settings
  uint8_t orientation = SystemSetting::PORTRAIT;  ///< Screen orientation

  // Navigation settings
  uint8_t longPressChapterSkip = 1;  ///< Long press chapter skip enabled

  // Display settings - stores the literal value (1, 5, 10, 15, or 30)
  uint8_t refreshFrequency = 15;  ///< Screen refresh frequency in pages

  // Configurable status bar sections
  StatusBarSectionConfig statusBarLeft;    ///< Left status bar section
  StatusBarSectionConfig statusBarMiddle;  ///< Middle status bar section
  StatusBarSectionConfig statusBarRight;   ///< Right status bar section

  /**
   * @brief Page auto-turn interval in seconds
   * @details Values: 0 = off, increments of 10 (10, 20, 30, 40, 50, 60)
   */
  uint8_t pageAutoTurnSeconds = 0;

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

        if (bytesRead >= 11) {
          size_t offset = 0;

          fontFamily = data[offset++];
          fontSize = data[offset++];
          lineSpacing = data[offset++];
          extraParagraphSpacing = data[offset++];
          paragraphAlignment = data[offset++];
          hyphenationEnabled = data[offset++];
          screenMargin = data[offset++];
          refreshFrequency = data[offset++];
          longPressChapterSkip = data[offset++];
          textAntiAliasing = data[offset++];
          orientation = data[offset++];

          if (bytesRead >= offset + 3) {
            statusBarLeft.fromBytes(data, offset);
            statusBarMiddle.fromBytes(data, offset);
            statusBarRight.fromBytes(data, offset);
          }

          if (bytesRead >= offset + 1) {
            pageAutoTurnSeconds = data[offset++];
            if (pageAutoTurnSeconds > 60 || pageAutoTurnSeconds % 10 != 0) {
              pageAutoTurnSeconds = 0;
            }
          } else {
            pageAutoTurnSeconds = 0;
          }

          if (bytesRead >= offset + 1) {
            paragraphCssIndentEnabled = data[offset++];
            if (paragraphCssIndentEnabled > 1) {
              paragraphCssIndentEnabled = 1;
            }
          } else {
            paragraphCssIndentEnabled = SystemSetting::getInstance().paragraphCssIndentEnabled;
          }

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
    std::string settingsPath = bookCachePath + "/settings.bin";
    FsFile f;
    if (SdMan.openFileForWrite("BST", settingsPath.c_str(), f)) {
      uint8_t data[32];
      size_t offset = 0;

      data[offset++] = fontFamily;
      data[offset++] = fontSize;
      data[offset++] = lineSpacing;
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
    lineSpacing = global.lineSpacing;
    extraParagraphSpacing = global.extraParagraphSpacing;
    paragraphAlignment = global.paragraphAlignment;
    paragraphCssIndentEnabled = global.paragraphCssIndentEnabled;
    hyphenationEnabled = global.hyphenationEnabled;
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

    statusBarLeft.item = static_cast<StatusBarItem>(global.statusBarLeft);
    statusBarMiddle.item = static_cast<StatusBarItem>(global.statusBarMiddle);
    statusBarRight.item = static_cast<StatusBarItem>(global.statusBarRight);
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
   * @brief Gets line compression factor based on current settings
   * @return Line compression multiplier
   */
  float getReaderLineCompression() const {
    SystemSetting& global = SystemSetting::getInstance();
    uint8_t oldFam = global.fontFamily;
    uint8_t oldSpacing = global.lineSpacing;
    global.fontFamily = this->fontFamily;
    global.lineSpacing = this->lineSpacing;
    float comp = global.getReaderLineCompression();
    global.fontFamily = oldFam;
    global.lineSpacing = oldSpacing;
    return comp;
  }

  /**
   * @brief Equality operator for comparison
   * @param other Settings to compare with
   * @return true if all settings match
   */
  bool operator==(const BookSettings& other) const {
    return fontFamily == other.fontFamily && fontSize == other.fontSize && lineSpacing == other.lineSpacing &&
           paragraphAlignment == other.paragraphAlignment &&
           paragraphCssIndentEnabled == other.paragraphCssIndentEnabled &&
           extraParagraphSpacing == other.extraParagraphSpacing &&
           textAntiAliasing == other.textAntiAliasing && hyphenationEnabled == other.hyphenationEnabled &&
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