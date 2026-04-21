#include "state/SystemSetting.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <cstring>

#include "system/Fonts.h"

SystemSetting SystemSetting::instance;

/**
 * @brief Reads a value from file and validates it's within allowed range
 * @param file File to read from
 * @param member Reference to member to update
 * @param maxValue Maximum allowed value
 */
void readAndValidate(FsFile& file, uint8_t& member, const uint8_t maxValue) {
  uint8_t tempValue;
  serialization::readPod(file, tempValue);

  if (tempValue < maxValue) {
    member = tempValue;
  }
}

namespace {
constexpr uint8_t SETTINGS_FILE_VERSION = 12;
constexpr uint8_t SETTINGS_COUNT = 41;
/** Last field index in v9 (1-based count of persisted pods through displayImageDither). */
constexpr uint8_t SETTINGS_COUNT_V9 = 40;
constexpr char SETTINGS_FILE[] = "/.system/settings.bin";

void sanitizeSleepCustomBmp(char* buf) {
  if (buf == nullptr || buf[0] == '\0') {
    return;
  }
  if (strcmp(buf, "/sleep.bmp") == 0) {
    return;
  }
  if (strstr(buf, "..") != nullptr) {
    buf[0] = '\0';
    return;
  }
  for (const char* p = buf; *p != '\0'; ++p) {
    if (*p == '/' || *p == '\\' || *p == ':') {
      buf[0] = '\0';
      return;
    }
  }
}
}  // namespace

void SystemSetting::setSleepCustomBmpFromInput(const char* s) {
  if (s == nullptr || s[0] == '\0') {
    sleepCustomBmp[0] = '\0';
    return;
  }
  strncpy(sleepCustomBmp, s, sizeof(sleepCustomBmp) - 1);
  sleepCustomBmp[sizeof(sleepCustomBmp) - 1] = '\0';
  sanitizeSleepCustomBmp(sleepCustomBmp);
}

/**
 * @brief Saves all settings to file
 * @return true if save successful, false otherwise
 */
bool SystemSetting::saveToFile() const {
  SdMan.mkdir("/.system");

  FsFile outputFile;

  if (!SdMan.openFileForWrite("CPS", SETTINGS_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, SETTINGS_FILE_VERSION);
  serialization::writePod(outputFile, SETTINGS_COUNT);
  serialization::writePod(outputFile, sleepScreen);
  serialization::writePod(outputFile, extraParagraphSpacing);
  serialization::writePod(outputFile, shortPwrBtn);
  serialization::writePod(outputFile, statusBar);
  serialization::writePod(outputFile, orientation);
  serialization::writePod(outputFile, frontButtonLayout);
  serialization::writePod(outputFile, sideButtonLayout);
  serialization::writePod(outputFile, fontFamily);
  serialization::writePod(outputFile, fontSize);
  serialization::writePod(outputFile, lineSpacing);
  serialization::writePod(outputFile, paragraphAlignment);
  serialization::writePod(outputFile, sleepTimeout);
  serialization::writePod(outputFile, refreshFrequency);
  serialization::writePod(outputFile, screenMargin);
  serialization::writePod(outputFile, sleepScreenCoverMode);
  serialization::writeString(outputFile, std::string(opdsServerUrl));
  serialization::writePod(outputFile, textAntiAliasing);
  serialization::writePod(outputFile, hideBatteryPercentage);
  serialization::writePod(outputFile, longPressChapterSkip);
  serialization::writePod(outputFile, hyphenationEnabled);
  serialization::writePod(outputFile, readerShortPwrBtn);
  serialization::writeString(outputFile, std::string(opdsUsername));
  serialization::writeString(outputFile, std::string(opdsPassword));
  serialization::writePod(outputFile, sleepScreenCoverFilter);
  serialization::writePod(outputFile, useLibraryIndex);
  serialization::writePod(outputFile, recentLibraryMode);
  serialization::writePod(outputFile, readerDirectionMapping);
  serialization::writePod(outputFile, readerMenuButton);
  serialization::writePod(outputFile, bootSetting);
  serialization::writePod(outputFile, statusBarLeft);
  serialization::writePod(outputFile, statusBarMiddle);
  serialization::writePod(outputFile, statusBarRight);
  serialization::writePod(outputFile, pageAutoTurnSeconds);
  serialization::writePod(outputFile, readerImageGrayscale);
  serialization::writePod(outputFile, readerSmartRefreshOnImages);
  serialization::writePod(outputFile, sleepScreenCoverGrayscale);
  serialization::writeString(outputFile, std::string(sleepCustomBmp));
  serialization::writePod(outputFile, readerImagePresentation);
  serialization::writePod(outputFile, readerImageDither);
  serialization::writePod(outputFile, displayImageDither);
  serialization::writePod(outputFile, displayImagePresentation);

  outputFile.close();

  Serial.printf("[%lu] [CPS] Settings saved to file (version %u)\n", millis(), SETTINGS_FILE_VERSION);
  return true;
}

/**
 * @brief Loads all settings from file
 * @return true if load successful, false otherwise
 */
bool SystemSetting::loadFromFile() {
  FsFile inputFile;

  if (!SdMan.openFileForRead("CPS", SETTINGS_FILE, inputFile)) {
    statusBarLeft = STATUS_ITEM_BATTERY_ICON_WITH_PERCENT;
    statusBarMiddle = STATUS_ITEM_CHAPTER_TITLE;
    statusBarRight = STATUS_ITEM_PAGE_NUMBERS;
    saveToFile();
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);

  if (version != SETTINGS_FILE_VERSION && version != 3 && version != 6 && version != 7 && version != 8 &&
      version != 9 && version != 10 && version != 11) {
    Serial.printf("[%lu] [CPS] Deserialization failed: Unknown version %u (expected %u, %u, %u, %u, %u, %u, %u, or %u)\n", millis(), version,
                  SETTINGS_FILE_VERSION, 11, 10, 9, 8, 7, 6, 3);
    inputFile.close();
    statusBarLeft = STATUS_ITEM_BATTERY_ICON_WITH_PERCENT;
    statusBarMiddle = STATUS_ITEM_CHAPTER_TITLE;
    statusBarRight = STATUS_ITEM_PAGE_NUMBERS;
    return false;
  }

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);
  uint8_t settingsRead = 0;

  do {
    readAndValidate(inputFile, sleepScreen, SLEEP_SCREEN_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, extraParagraphSpacing);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, shortPwrBtn, SHORT_PWRBTN_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, statusBar, STATUS_BAR_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, orientation, ORIENTATION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, frontButtonLayout, FRONT_BUTTON_LAYOUT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, sideButtonLayout, SIDE_BUTTON_LAYOUT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, fontFamily, FONT_FAMILY_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, fontSize, FONT_SIZE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, lineSpacing, LINE_COMPRESSION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, paragraphAlignment, PARAGRAPH_ALIGNMENT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, sleepTimeout, SLEEP_TIMEOUT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, refreshFrequency, REFRESH_FREQUENCY_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, screenMargin);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, sleepScreenCoverMode, SLEEP_SCREEN_COVER_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    {
      std::string urlStr;
      serialization::readString(inputFile, urlStr);
      strncpy(opdsServerUrl, urlStr.c_str(), sizeof(opdsServerUrl) - 1);
      opdsServerUrl[sizeof(opdsServerUrl) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, textAntiAliasing);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, hideBatteryPercentage, HIDE_BATTERY_PERCENTAGE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, longPressChapterSkip);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, hyphenationEnabled);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, readerShortPwrBtn, READER_SHORT_PWRBTN_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    {
      std::string usernameStr;
      serialization::readString(inputFile, usernameStr);
      strncpy(opdsUsername, usernameStr.c_str(), sizeof(opdsUsername) - 1);
      opdsUsername[sizeof(opdsUsername) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;

    {
      std::string passwordStr;
      serialization::readString(inputFile, passwordStr);
      strncpy(opdsPassword, passwordStr.c_str(), sizeof(opdsPassword) - 1);
      opdsPassword[sizeof(opdsPassword) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, sleepScreenCoverFilter, SLEEP_SCREEN_COVER_FILTER_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, useLibraryIndex);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, recentLibraryMode, RECENT_LIBRARY_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, readerDirectionMapping, READER_DIRECTION_MAPPING_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, readerMenuButton, READER_MENU_BUTTON_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, bootSetting, BOOT_SETTING_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    if (version >= 4) {
      readAndValidate(inputFile, statusBarLeft, STATUS_BAR_ITEM_COUNT);
      if (++settingsRead >= fileSettingsCount) break;

      readAndValidate(inputFile, statusBarMiddle, STATUS_BAR_ITEM_COUNT);
      if (++settingsRead >= fileSettingsCount) break;

      readAndValidate(inputFile, statusBarRight, STATUS_BAR_ITEM_COUNT);
      if (++settingsRead >= fileSettingsCount) break;
    } else {
      switch (statusBar) {
        case NONE:
          statusBarLeft = STATUS_ITEM_NONE;
          statusBarMiddle = STATUS_ITEM_NONE;
          statusBarRight = STATUS_ITEM_NONE;
          break;
        case NO_PROGRESS:
          statusBarLeft = STATUS_ITEM_BATTERY_ICON_WITH_PERCENT;
          statusBarMiddle = STATUS_ITEM_CHAPTER_TITLE;
          statusBarRight = STATUS_ITEM_NONE;
          break;
        case FULL:
        default:
          statusBarLeft = STATUS_ITEM_BATTERY_ICON_WITH_PERCENT;
          statusBarMiddle = STATUS_ITEM_CHAPTER_TITLE;
          statusBarRight = STATUS_ITEM_PAGE_NUMBERS;
          break;
        case FULL_WITH_PROGRESS_BAR:
          statusBarLeft = STATUS_ITEM_BATTERY_ICON_WITH_PERCENT;
          statusBarMiddle = STATUS_ITEM_PROGRESS_BAR_WITH_PERCENT;
          statusBarRight = STATUS_ITEM_CHAPTER_TITLE;
          break;
        case ONLY_PROGRESS_BAR:
          statusBarLeft = STATUS_ITEM_NONE;
          statusBarMiddle = STATUS_ITEM_PROGRESS_BAR;
          statusBarRight = STATUS_ITEM_NONE;
          break;
        case BATTERY_PERCENTAGE:
          statusBarLeft = STATUS_ITEM_BATTERY_PERCENTAGE;
          statusBarMiddle = STATUS_ITEM_NONE;
          statusBarRight = STATUS_ITEM_NONE;
          break;
        case PERCENTAGE:
          statusBarLeft = STATUS_ITEM_NONE;
          statusBarMiddle = STATUS_ITEM_PERCENTAGE;
          statusBarRight = STATUS_ITEM_NONE;
          break;
        case PAGE_BARS:
          statusBarLeft = STATUS_ITEM_NONE;
          statusBarMiddle = STATUS_ITEM_PAGE_BARS;
          statusBarRight = STATUS_ITEM_NONE;
          break;
      }
    }

    if (version >= 6) {
      serialization::readPod(inputFile, pageAutoTurnSeconds);
      if (pageAutoTurnSeconds > 60 || pageAutoTurnSeconds % 10 != 0) {
        pageAutoTurnSeconds = 0;
      }
      if (++settingsRead >= fileSettingsCount) break;
    } else {
      pageAutoTurnSeconds = 0;
    }

    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, readerImageGrayscale);
      if (readerImageGrayscale > 1) {
        readerImageGrayscale = 1;
      }
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, readerSmartRefreshOnImages);
      if (readerSmartRefreshOnImages > 1) {
        readerSmartRefreshOnImages = 1;
      }
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, sleepScreenCoverGrayscale);
      if (sleepScreenCoverGrayscale > 1) {
        sleepScreenCoverGrayscale = 1;
      }
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      std::string sleepBmpStr;
      serialization::readString(inputFile, sleepBmpStr);
      setSleepCustomBmpFromInput(sleepBmpStr.c_str());
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      readAndValidate(inputFile, readerImagePresentation, READER_IMAGE_PRESENTATION_COUNT);
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      readAndValidate(inputFile, readerImageDither, READER_IMAGE_DITHER_COUNT);
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      readAndValidate(inputFile, displayImageDither, READER_IMAGE_DITHER_COUNT);
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      readAndValidate(inputFile, displayImagePresentation, READER_IMAGE_PRESENTATION_COUNT);
      ++settingsRead;
    }

  } while (false);

  inputFile.close();

  if (settingsRead < SETTINGS_COUNT) {
    if (settingsRead < SETTINGS_COUNT_V9) {
      displayImageDither = readerImageDither;
    }
    displayImagePresentation = readerImagePresentation;
  }

  Serial.printf("[%lu] [CPS] Settings loaded (version %u, %u items)\n", millis(), version, settingsRead);

  // v10 stored two-level presentation; byte 1 remapped to same tier as byte 0 for v11 file layout.
  if (version == 10) {
    if (readerImagePresentation == 1u) {
      readerImagePresentation = 0u;
    }
    if (displayImagePresentation == 1u) {
      displayImagePresentation = 0u;
    }
  }

  // v12+: Low (0) / Medium (1) / High (2). v10–v11 files stored old two-level 0/1 (balance / dark).
  if (version == 10 || version == 11) {
    auto mapLegacyPresentation = [](uint8_t& p) {
      if (p == 0u) {
        p = SystemSetting::IMAGE_PRESENTATION_MEDIUM;
      } else if (p == 1u) {
        p = SystemSetting::IMAGE_PRESENTATION_HIGH;
      } else if (p >= SystemSetting::READER_IMAGE_PRESENTATION_COUNT) {
        p = SystemSetting::IMAGE_PRESENTATION_MEDIUM;
      }
    };
    mapLegacyPresentation(readerImagePresentation);
    mapLegacyPresentation(displayImagePresentation);
  }

  return true;
}

/**
 * @brief Gets reader line compression factor based on font and spacing
 * @return Line compression multiplier
 */
float SystemSetting::getReaderLineCompression() const {
  switch (fontFamily) {
    case BOOKERLY:
    default:
      switch (lineSpacing) {
        case TIGHT:
          return 0.95f;
        case NORMAL:
        default:
          return 1.0f;
        case WIDE:
          return 1.1f;
      }
    case ATKINSON_HYPERLEGIBLE:
      switch (lineSpacing) {
        case TIGHT:
          return 0.90f;
        case NORMAL:
        default:
          return 0.95f;
        case WIDE:
          return 1.0f;
      }
    case LITERATA:
      switch (lineSpacing) {
        case TIGHT:
          return 0.95f;
        case NORMAL:
        default:
          return 1.05f;
        case WIDE:
          return 1.15f;
      }
  }
}

/**
 * @brief Gets sleep timeout in milliseconds
 * @return Sleep timeout in milliseconds
 */
unsigned long SystemSetting::getSleepTimeoutMs() const {
  switch (sleepTimeout) {
    case SLEEP_1_MIN:
      return 1UL * 60 * 1000;
    case SLEEP_5_MIN:
      return 5UL * 60 * 1000;
    case SLEEP_10_MIN:
    default:
      return 10UL * 60 * 1000;
    case SLEEP_15_MIN:
      return 15UL * 60 * 1000;
    case SLEEP_30_MIN:
      return 30UL * 60 * 1000;
  }
}

/**
 * @brief Gets screen refresh frequency in pages
 * @return Number of pages between refreshes
 */
int SystemSetting::getRefreshFrequency() const {
  switch (refreshFrequency) {
    case REFRESH_1:
      return 1;
    case REFRESH_5:
      return 5;
    case REFRESH_10:
      return 10;
    case REFRESH_15:
    default:
      return 15;
    case REFRESH_30:
      return 30;
  }
}

/**
 * @brief Gets reader font ID based on font family and size
 * @return Font identifier for rendering
 */
int SystemSetting::getReaderFontId() const {
  switch (fontFamily) {
    case BOOKERLY:
    default:
      switch (fontSize) {
        case EXTRA_SMALL:
          return BOOKERLY_10_FONT_ID;
        case SMALL:
          return BOOKERLY_12_FONT_ID;
        case MEDIUM:
        default:
          return BOOKERLY_14_FONT_ID;
        case LARGE:
          return BOOKERLY_16_FONT_ID;
        case EXTRA_LARGE:
          return BOOKERLY_18_FONT_ID;
      }
    case ATKINSON_HYPERLEGIBLE:
      switch (fontSize) {
        case EXTRA_SMALL:
          return ATKINSON_HYPERLEGIBLE_10_FONT_ID;
        case SMALL:
          return ATKINSON_HYPERLEGIBLE_12_FONT_ID;
        case MEDIUM:
        default:
          return ATKINSON_HYPERLEGIBLE_14_FONT_ID;
        case LARGE:
          return ATKINSON_HYPERLEGIBLE_16_FONT_ID;
        case EXTRA_LARGE:
          return ATKINSON_HYPERLEGIBLE_18_FONT_ID;
      }
    case LITERATA:
      switch (fontSize) {
        case EXTRA_SMALL:
          return LITERATA_10_FONT_ID;
        case SMALL:
          return LITERATA_12_FONT_ID;
        case MEDIUM:
        default:
          return LITERATA_14_FONT_ID;
        case LARGE:
          return LITERATA_16_FONT_ID;
        case EXTRA_LARGE:
          return LITERATA_18_FONT_ID;
      }
  }
}