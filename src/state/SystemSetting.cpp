/**
 * @file SystemSetting.cpp
 * @brief Definitions for SystemSetting.
 */

#include "state/SystemSetting.h"

#ifndef INX_SIMULATOR_WEB_ONLY
#include <GfxRenderer.h>
#include <HalDisplay.h>
#endif
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <cstdio>
#include <cstring>
#include <string>

#ifndef INX_SIMULATOR_WEB_ONLY
#include "system/FontManager.h"
#include "system/Fonts.h"
#endif

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
constexpr uint8_t SETTINGS_FILE_VERSION = 30;
constexpr uint8_t SETTINGS_COUNT = 68;
/** Last field index in v9 (1-based count of persisted pods through displayImageDither). */
constexpr uint8_t SETTINGS_COUNT_V9 = 40;
constexpr uint8_t LEGACY_IMAGE_PRESENTATION_COUNT = 4;
constexpr char SETTINGS_FILE[] = "/.system/settings.bin";
constexpr char UI_THEME_FILE[] = "/.system/ui_theme.bin";

void sanitizeSleepCustomBmp(char* buf) {
  if (buf == nullptr || buf[0] == '\0') {
    return;
  }
  if (strcmp(buf, "/sleep.bmp") == 0 || strcmp(buf, "/sleep.jpg") == 0 || strcmp(buf, "/sleep.jpeg") == 0) {
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

bool validRefreshFrequency(const uint8_t value) {
  return value == 1 || value == 5 || value == 10 || value == 15 || value == 30;
}

void saveUiThemeSetting(const uint8_t value) {
  FsFile file;
  if (!SdMan.openFileForWrite("CPS", UI_THEME_FILE, file)) {
    return;
  }
  serialization::writePod(file, value);
  file.close();
}

bool loadUiThemeSetting(uint8_t& value) {
  FsFile file;
  if (!SdMan.openFileForRead("CPS", UI_THEME_FILE, file)) {
    return false;
  }

  uint8_t saved = SystemSetting::UI_THEME_CLASSIC;
  serialization::readPod(file, saved);
  file.close();

  if (saved >= SystemSetting::UI_THEME_COUNT) {
    return false;
  }
  value = saved;
  return true;
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

  uint8_t fontFamilyToSave = fontFamily;
#ifndef INX_SIMULATOR_WEB_ONLY
  FontManager::clampReaderFontFamilySlot(fontFamilyToSave);
  if (fontFamilyToSave != fontFamily) {
    const_cast<SystemSetting*>(this)->fontFamily = fontFamilyToSave;
  }
#endif

  {
    SystemSetting* mut = const_cast<SystemSetting*>(this);
    if (mut->recentVisibleCount < 1 || mut->recentVisibleCount > 8) mut->recentVisibleCount = 8;
    if (mut->librarySortEnabled > 1) mut->librarySortEnabled = 1;
    if (mut->librarySortMode > 7) mut->librarySortMode = 0;
    if (mut->libraryMode >= LIBRARY_MODE_COUNT) mut->libraryMode = LIBRARY_LIST;
    if (mut->libraryViewMode >= LIBRARY_VIEW_MODE_COUNT) mut->libraryViewMode = LIBRARY_VIEW_FOLDERS;
    if (mut->bionicReadingEnabled > 1) mut->bionicReadingEnabled = 0;
    if (mut->sleepClockStyle >= SLEEP_CLOCK_STYLE_COUNT) mut->sleepClockStyle = CLOCK_CENTERED_DATE;
    if (mut->sleepClockTimeFormat >= CLOCK_TIME_FORMAT_COUNT) mut->sleepClockTimeFormat = CLOCK_24_HOUR;
    if (mut->sleepClockRefreshInterval >= CLOCK_REFRESH_INTERVAL_COUNT)
      mut->sleepClockRefreshInterval = CLOCK_REFRESH_OFF;
    if (mut->sleepImageQuality >= SLEEP_IMAGE_QUALITY_COUNT) mut->sleepImageQuality = SLEEP_IMAGE_LOW;
    if (mut->xtcImageQuality >= READER_IMAGE_QUALITY_COUNT) mut->xtcImageQuality = READER_IMAGE_LOW;
    if (mut->xtcShortPwrBtn >= XTC_SHORT_PWRBTN_COUNT) mut->xtcShortPwrBtn = XTC_POWER_NEXT;
    if (mut->xtcPageAutoTurnSeconds > 60 || mut->xtcPageAutoTurnSeconds % 10 != 0) mut->xtcPageAutoTurnSeconds = 0;
    if (!validRefreshFrequency(mut->xtcRefreshFrequency)) mut->xtcRefreshFrequency = 15;
    if (mut->timeZoneQuarterOffset > 104) mut->timeZoneQuarterOffset = 80;
    if (mut->shakePageTurn > 2) mut->shakePageTurn = 0;
    if (mut->shakePageTurnSensitivity > 2) mut->shakePageTurnSensitivity = 1;
    if (mut->uiTheme >= UI_THEME_COUNT) mut->uiTheme = UI_THEME_CLASSIC;
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
  serialization::writePod(outputFile, fontFamilyToSave);
  serialization::writePod(outputFile, fontSize);
  serialization::writePod(outputFile, lineHeight);
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
  serialization::writePod(outputFile, sleepImageQuality);
  serialization::writeString(outputFile, std::string(sleepCustomBmp));
  serialization::writePod(outputFile, legacyReaderImagePresentation);
  serialization::writePod(outputFile, readerImageDither);
  serialization::writePod(outputFile, displayImageDither);
  serialization::writePod(outputFile, legacyDisplayImagePresentation);
  serialization::writePod(outputFile, paragraphCssIndentEnabled);
  serialization::writePod(outputFile, refreshOnLoadRecent);
  serialization::writePod(outputFile, refreshOnLoadLibrary);
  serialization::writePod(outputFile, refreshOnLoadSettings);
  serialization::writePod(outputFile, refreshOnLoadSync);
  serialization::writePod(outputFile, refreshOnLoadStatistics);
  serialization::writePod(outputFile, bitmapRoundedCorners);
  serialization::writePod(outputFile, recentVisibleCount);
  serialization::writePod(outputFile, librarySortEnabled);
  serialization::writePod(outputFile, librarySortMode);
  serialization::writePod(outputFile, libraryMode);
  serialization::writePod(outputFile, libraryViewMode);
  serialization::writePod(outputFile, bionicReadingEnabled);
  serialization::writePod(outputFile, sleepClockStyle);
  serialization::writePod(outputFile, sleepClockTimeFormat);
  serialization::writePod(outputFile, timeZoneQuarterOffset);
  serialization::writePod(outputFile, textSpace);
  serialization::writePod(outputFile, mainMenuNav);
  serialization::writePod(outputFile, xtcImageQuality);
  serialization::writePod(outputFile, xtcShortPwrBtn);
  serialization::writePod(outputFile, xtcPageAutoTurnSeconds);
  serialization::writePod(outputFile, xtcRefreshFrequency);
  serialization::writePod(outputFile, sleepClockRefreshInterval);
  serialization::writePod(outputFile, shakePageTurn);
  serialization::writePod(outputFile, shakePageTurnSensitivity);
  serialization::writePod(outputFile, uiTheme);

  outputFile.close();
  saveUiThemeSetting(uiTheme);

  Serial.printf("[%lu] [CPS] Settings saved to file (version %u)\n", millis(), SETTINGS_FILE_VERSION);
  return true;
}

/**
 * @brief Loads all settings from file
 * @return true if load successful, false otherwise
 */
// cppcheck-suppress checkLevelNormal ; large versioned-field function, exhaustive level not worth the CI runtime cost
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

  if (version > SETTINGS_FILE_VERSION) {
    Serial.printf("[%lu] [CPS] Deserialization failed: Unknown version %u (expected <= %u)\n", millis(), version,
                  SETTINGS_FILE_VERSION);
    inputFile.close();
    statusBarLeft = STATUS_ITEM_BATTERY_ICON_WITH_PERCENT;
    statusBarMiddle = STATUS_ITEM_CHAPTER_TITLE;
    statusBarRight = STATUS_ITEM_PAGE_NUMBERS;
    return false;
  }

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);
  const bool shouldRewriteSettings = version < SETTINGS_FILE_VERSION || fileSettingsCount < SETTINGS_COUNT;
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

    {
      uint8_t rawFontFamily = 0;
      serialization::readPod(inputFile, rawFontFamily);
      fontFamily = rawFontFamily;
#ifndef INX_SIMULATOR_WEB_ONLY
      FontManager::clampReaderFontFamilySlot(fontFamily);
#endif
    }
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, fontSize, FONT_SIZE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    // This slot historically held the lineSpacing enum (0-4). It now holds a numeric line height
    // percentage (10-200). Values below 10 are legacy enums and migrate to the default 100.
    serialization::readPod(inputFile, lineHeight);
    if (lineHeight < 10 || lineHeight > 200) lineHeight = 100;
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
    if (longPressChapterSkip > LONG_PRESS_PAGE_SKIP_5) {
      longPressChapterSkip = LONG_PRESS_CHAPTER_SKIP;
    }
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

    readAndValidate(inputFile, statusBarLeft, STATUS_BAR_ITEM_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, statusBarMiddle, STATUS_BAR_ITEM_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, statusBarRight, STATUS_BAR_ITEM_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, pageAutoTurnSeconds);
    if (pageAutoTurnSeconds > 60 || pageAutoTurnSeconds % 10 != 0) {
      pageAutoTurnSeconds = 0;
    }
    if (++settingsRead >= fileSettingsCount) break;

    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, readerImageGrayscale);
      if (readerImageGrayscale >= READER_IMAGE_QUALITY_COUNT) {
        readerImageGrayscale = READER_IMAGE_MEDIUM;
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
      serialization::readPod(inputFile, sleepImageQuality);
      if (sleepImageQuality >= SLEEP_IMAGE_QUALITY_COUNT) {
        sleepImageQuality = SLEEP_IMAGE_LOW;
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
      readAndValidate(inputFile, legacyReaderImagePresentation, LEGACY_IMAGE_PRESENTATION_COUNT);
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
      readAndValidate(inputFile, legacyDisplayImagePresentation, LEGACY_IMAGE_PRESENTATION_COUNT);
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, paragraphCssIndentEnabled);
      if (paragraphCssIndentEnabled > 1) {
        paragraphCssIndentEnabled = 1;
      }
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, refreshOnLoadRecent);
      if (refreshOnLoadRecent > 1) refreshOnLoadRecent = 0;
      ++settingsRead;
      if (settingsRead < fileSettingsCount) {
        serialization::readPod(inputFile, refreshOnLoadLibrary);
        if (refreshOnLoadLibrary > 1) refreshOnLoadLibrary = 0;
        ++settingsRead;
      }
      if (settingsRead < fileSettingsCount) {
        serialization::readPod(inputFile, refreshOnLoadSettings);
        if (refreshOnLoadSettings > 1) refreshOnLoadSettings = 0;
        ++settingsRead;
      }
      if (settingsRead < fileSettingsCount) {
        serialization::readPod(inputFile, refreshOnLoadSync);
        if (refreshOnLoadSync > 1) refreshOnLoadSync = 0;
        ++settingsRead;
      }
      if (settingsRead < fileSettingsCount) {
        serialization::readPod(inputFile, refreshOnLoadStatistics);
        if (refreshOnLoadStatistics > 1) refreshOnLoadStatistics = 0;
        ++settingsRead;
      }
    }
    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, bitmapRoundedCorners);
      if (bitmapRoundedCorners > 2) {
        bitmapRoundedCorners = 0;
      }
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, recentVisibleCount);
      if (recentVisibleCount < 1 || recentVisibleCount > 8) {
        recentVisibleCount = 8;
      }
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, librarySortEnabled);
      if (librarySortEnabled > 1) {
        librarySortEnabled = 1;
      }
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, librarySortMode);
      if (librarySortMode > 7) {
        librarySortMode = 0;
      }
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      readAndValidate(inputFile, libraryMode, LIBRARY_MODE_COUNT);
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      readAndValidate(inputFile, libraryViewMode, LIBRARY_VIEW_MODE_COUNT);
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, bionicReadingEnabled);
      if (bionicReadingEnabled > 1) {
        bionicReadingEnabled = 0;
      }
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      readAndValidate(inputFile, sleepClockStyle, SLEEP_CLOCK_STYLE_COUNT);
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      readAndValidate(inputFile, sleepClockTimeFormat, CLOCK_TIME_FORMAT_COUNT);
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, timeZoneQuarterOffset);
      if (timeZoneQuarterOffset > 104) {
        timeZoneQuarterOffset = 80;
      }
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, textSpace);
      if (textSpace < 10 || textSpace > 200) textSpace = 100;
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      readAndValidate(inputFile, mainMenuNav, MAIN_MENU_NAV_COUNT);
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      readAndValidate(inputFile, xtcImageQuality, READER_IMAGE_QUALITY_COUNT);
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      readAndValidate(inputFile, xtcShortPwrBtn, XTC_SHORT_PWRBTN_COUNT);
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, xtcPageAutoTurnSeconds);
      if (xtcPageAutoTurnSeconds > 60 || xtcPageAutoTurnSeconds % 10 != 0) {
        xtcPageAutoTurnSeconds = 0;
      }
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, xtcRefreshFrequency);
      if (!validRefreshFrequency(xtcRefreshFrequency)) {
        xtcRefreshFrequency = 15;
      }
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      readAndValidate(inputFile, sleepClockRefreshInterval, CLOCK_REFRESH_INTERVAL_COUNT);
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, shakePageTurn);
      if (shakePageTurn > 2) shakePageTurn = 0;
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      serialization::readPod(inputFile, shakePageTurnSensitivity);
      if (shakePageTurnSensitivity > 2) shakePageTurnSensitivity = 1;
      ++settingsRead;
    }
    if (settingsRead < fileSettingsCount) {
      readAndValidate(inputFile, uiTheme, UI_THEME_COUNT);
      ++settingsRead;
    }

  } while (false);

  inputFile.close();

#ifndef INX_SIMULATOR_WEB_ONLY
  FontManager::clampReaderFontFamilySlot(fontFamily);
#endif

  if (settingsRead < 60) {
    xtcImageQuality = readerImageGrayscale;
  }
  if (settingsRead < 61) {
    xtcShortPwrBtn = readerShortPwrBtn == READER_PAGE_REFRESH ? XTC_POWER_PAGE_REFRESH : XTC_POWER_NEXT;
  }
  if (settingsRead < 62) {
    xtcPageAutoTurnSeconds = pageAutoTurnSeconds;
  }
  if (settingsRead < 63) {
    xtcRefreshFrequency = getRefreshFrequency();
  }
  if (settingsRead < 65) {
    sleepClockRefreshInterval = CLOCK_REFRESH_OFF;
  }
  if (settingsRead < 66) {
    shakePageTurn = 0;
  }
  if (settingsRead < 67) {
    shakePageTurnSensitivity = 1;
  }
  if (settingsRead < 68) {
    uiTheme = UI_THEME_CLASSIC;
  }

  if (recentVisibleCount < 1 || recentVisibleCount > 8) {
    recentVisibleCount = 8;
  }
  if (librarySortEnabled > 1) {
    librarySortEnabled = 1;
  }
  if (librarySortMode > 7) {
    librarySortMode = 0;
  }
  if (sleepClockStyle >= SLEEP_CLOCK_STYLE_COUNT) {
    sleepClockStyle = CLOCK_CENTERED_DATE;
  }
  if (sleepClockTimeFormat >= CLOCK_TIME_FORMAT_COUNT) {
    sleepClockTimeFormat = CLOCK_24_HOUR;
  }
  if (sleepClockRefreshInterval >= CLOCK_REFRESH_INTERVAL_COUNT) {
    sleepClockRefreshInterval = CLOCK_REFRESH_OFF;
  }
  if (sleepImageQuality >= SLEEP_IMAGE_QUALITY_COUNT) {
    sleepImageQuality = SLEEP_IMAGE_LOW;
  }
  if (xtcImageQuality >= READER_IMAGE_QUALITY_COUNT) {
    xtcImageQuality = READER_IMAGE_LOW;
  }
  if (xtcShortPwrBtn >= XTC_SHORT_PWRBTN_COUNT) {
    xtcShortPwrBtn = XTC_POWER_NEXT;
  }
  if (xtcPageAutoTurnSeconds > 60 || xtcPageAutoTurnSeconds % 10 != 0) {
    xtcPageAutoTurnSeconds = 0;
  }
  if (!validRefreshFrequency(xtcRefreshFrequency)) {
    xtcRefreshFrequency = 15;
  }
  if (timeZoneQuarterOffset > 104) {
    timeZoneQuarterOffset = 80;
  }
  if (libraryMode >= LIBRARY_MODE_COUNT) {
    libraryMode = LIBRARY_LIST;
  }
  if (libraryViewMode >= LIBRARY_VIEW_MODE_COUNT) {
    libraryViewMode = LIBRARY_VIEW_FOLDERS;
  }
  if (bionicReadingEnabled > 1) {
    bionicReadingEnabled = 0;
  }
  if (uiTheme >= UI_THEME_COUNT) {
    uiTheme = UI_THEME_CLASSIC;
  }
  loadUiThemeSetting(uiTheme);

  if (settingsRead < SETTINGS_COUNT) {
    if (settingsRead < SETTINGS_COUNT_V9) {
      displayImageDither = readerImageDither;
    }
    legacyDisplayImagePresentation = legacyReaderImagePresentation;
  }

  Serial.printf("[%lu] [CPS] Settings loaded (version %u, %u items)\n", millis(), version, settingsRead);

  if (shouldRewriteSettings) {
    saveToFile();
  }

  return true;
}

/**
 * @brief Gets reader line compression factor based on font and spacing
 * @return Line compression multiplier
 */
float SystemSetting::getReaderLineCompression() const {
  // lineHeight is a percentage of the font's natural line height (100 = normal). Clamp 10-200.
  uint8_t lh = lineHeight;
  if (lh < 10 || lh > 200) lh = 100;
  return static_cast<float>(lh) / 100.0f;
}

float SystemSetting::getReaderWordSpacingFactor() const {
  // textSpace is a percentage of the natural inter-word space (100 = normal). Clamp 10-200.
  uint8_t ts = textSpace;
  if (ts < 10 || ts > 200) ts = 100;
  return static_cast<float>(ts) / 100.0f;
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

int SystemSetting::getTimeZoneOffsetMinutes() const {
  const int quarterHours = static_cast<int>(timeZoneQuarterOffset) - 48;
  return quarterHours * 15;
}

void SystemSetting::formatTimeZone(char* out, size_t outSize) const {
  if (out == nullptr || outSize == 0) {
    return;
  }
  const int minutes = getTimeZoneOffsetMinutes();
  const char sign = minutes < 0 ? '-' : '+';
  const int absMinutes = minutes < 0 ? -minutes : minutes;
  std::snprintf(out, outSize, "UTC%c%02d:%02d", sign, absMinutes / 60, absMinutes % 60);
}

int SystemSetting::getReaderFontIdForSettingsUi(uint8_t familySlot, uint8_t sizeIndex) const {
#ifdef INX_SIMULATOR_WEB_ONLY
  (void)familySlot;
  (void)sizeIndex;
  return 0;
#else
  if (familySlot < FONT_FAMILY_BUILTIN_COUNT) {
    return getReaderFontIdForFamilyAndSize(familySlot, sizeIndex);
  }
  return getReaderFontIdForFamilyAndSize(ATKINSON_HYPERLEGIBLE, sizeIndex);
#endif
}

int SystemSetting::getReaderFontIdForFamilyAndSize(uint8_t family, uint8_t size) const {
#ifdef INX_SIMULATOR_WEB_ONLY
  (void)family;
  (void)size;
  return 0;
#else
  if (size >= FONT_SIZE_COUNT) {
    size = MEDIUM;
  }
  static const int kPtBySize[] = {10, 12, 14, 16, 18};
  const int preferredPt = kPtBySize[size];

  if (family >= FONT_FAMILY_BUILTIN_COUNT) {
    const std::string sdName = FontManager::readerFontFamilyLabel(family);
    if (sdName == "Literata" || sdName == "Atkinson Hyperlegible") {
      return getReaderFontIdForFamilyAndSize(sdName == "Atkinson Hyperlegible" ? ATKINSON_HYPERLEGIBLE : LITERATA,
                                             size);
    }
    return FontManager::getFontIdNearestPointSize(sdName, preferredPt);
  }

  switch (family) {
    case ATKINSON_HYPERLEGIBLE:
      switch (size) {
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
    default:
      switch (size) {
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
#endif
}

/**
 * @brief Gets reader font ID based on font family and size
 * @return Font identifier for rendering
 */
int SystemSetting::getReaderFontId() const {
#ifdef INX_SIMULATOR_WEB_ONLY
  return 0;
#else
  return getReaderFontIdForFamilyAndSize(fontFamily, fontSize);
#endif
}

void SystemSetting::runHalfRefreshOnLoadIfEnabled(const GfxRenderer& renderer, const RefreshOnLoadPage page) const {
#ifdef INX_SIMULATOR_WEB_ONLY
  (void)renderer;
  (void)page;
#else
  uint8_t on = 0;
  switch (page) {
    case RefreshOnLoadPage::Recent:
      on = refreshOnLoadRecent;
      break;
    case RefreshOnLoadPage::Library:
      on = refreshOnLoadLibrary;
      break;
    case RefreshOnLoadPage::Settings:
      on = refreshOnLoadSettings;
      break;
    case RefreshOnLoadPage::Sync:
      on = refreshOnLoadSync;
      break;
    case RefreshOnLoadPage::Statistics:
      on = refreshOnLoadStatistics;
      break;
  }
  if (on) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
#endif
}
