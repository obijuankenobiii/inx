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
constexpr uint8_t SETTINGS_FILE_VERSION = 37;
// Reader-related fields (font/layout, status bar, refresh frequency, page auto-turn, image
// grayscale, per-button reader actions, XTC reader settings, hyphenation, bionic reading, screen
// margin, orientation, dictionary folder...) moved out to ReaderSetting/reader_settings.bin as of
// this version. Files older than this used an incompatible field layout and can't be positionally
// parsed, so loadFromFile() discards them and starts fresh instead of attempting to read them. See
// ReaderSetting.cpp for the new reader_settings.bin format (independently versioned from 1).
constexpr uint8_t MIN_SUPPORTED_SETTINGS_VERSION = 37;
// Must equal the number of data fields read by the do-while loop in loadFromFile() (currently 38,
// through shakePageTurnSensitivity - NOT counting the version/count header fields read separately
// before the loop). This is written into the file as its own "how many fields do I contain" header
// and read back as fileSettingsCount; the loop's `settingsRead < fileSettingsCount` checks use it to
// know whether the tail fields are actually present. If it's wrong, either the tail fields never get
// read back even though they were written (undercount), or the file never triggers the self-healing
// rewrite on old-format files (overcount, since fileSettingsCount can never reach it).
constexpr uint8_t SETTINGS_COUNT = 38;
constexpr uint8_t LEGACY_IMAGE_PRESENTATION_COUNT = 4;
constexpr char SETTINGS_FILE[] = "/.system/settings.bin";
constexpr char UI_THEME_FILE[] = "/.system/ui_theme.bin";
constexpr uint32_t FNV1A_OFFSET = 2166136261UL;
constexpr uint32_t FNV1A_PRIME = 16777619UL;

void hashBytes(uint32_t& hash, const void* data, const size_t len) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < len; ++i) {
    hash ^= bytes[i];
    hash *= FNV1A_PRIME;
  }
}

template <typename T>
void hashPod(uint32_t& hash, const T& value) {
  hashBytes(hash, &value, sizeof(T));
}

void hashString(uint32_t& hash, const char* value) {
  const uint32_t len = value == nullptr ? 0 : static_cast<uint32_t>(strlen(value));
  hashPod(hash, len);
  if (len > 0) {
    hashBytes(hash, value, len);
  }
}

bool hashFile(const char* path, uint32_t& hash) {
  FsFile file;
  if (!SdMan.openFileForRead("CPS", path, file)) {
    return false;
  }
  hash = FNV1A_OFFSET;
  uint8_t buffer[64];
  while (file.available()) {
    const int n = file.read(buffer, sizeof(buffer));
    if (n <= 0) {
      break;
    }
    hashBytes(hash, buffer, static_cast<size_t>(n));
  }
  file.close();
  return true;
}

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

uint32_t settingsHash(const SystemSetting& settings) {
  uint32_t hash = FNV1A_OFFSET;
  hashPod(hash, SETTINGS_FILE_VERSION);
  hashPod(hash, SETTINGS_COUNT);
  hashPod(hash, settings.sleepScreen);
  hashPod(hash, settings.shortPwrBtn);
  hashPod(hash, settings.frontButtonLayout);
  hashPod(hash, settings.sideButtonLayout);
  hashPod(hash, settings.sleepTimeout);
  hashPod(hash, settings.sleepScreenCoverMode);
  hashString(hash, settings.opdsServerUrl);
  hashPod(hash, settings.hideBatteryPercentage);
  hashString(hash, settings.opdsUsername);
  hashString(hash, settings.opdsPassword);
  hashPod(hash, settings.sleepScreenCoverFilter);
  hashPod(hash, settings.useLibraryIndex);
  hashPod(hash, settings.recentLibraryMode);
  hashPod(hash, settings.bootSetting);
  hashPod(hash, settings.sleepImageQuality);
  hashString(hash, settings.sleepCustomBmp);
  hashPod(hash, settings.displayImageDither);
  hashPod(hash, settings.legacyDisplayImagePresentation);
  hashPod(hash, settings.refreshOnLoadRecent);
  hashPod(hash, settings.refreshOnLoadLibrary);
  hashPod(hash, settings.refreshOnLoadSettings);
  hashPod(hash, settings.refreshOnLoadSync);
  hashPod(hash, settings.refreshOnLoadStatistics);
  hashPod(hash, settings.bitmapRoundedCorners);
  hashPod(hash, settings.recentVisibleCount);
  hashPod(hash, settings.librarySortEnabled);
  hashPod(hash, settings.librarySortMode);
  hashPod(hash, settings.libraryMode);
  hashPod(hash, settings.libraryViewMode);
  hashPod(hash, settings.sleepClockStyle);
  hashPod(hash, settings.sleepClockTimeFormat);
  hashPod(hash, settings.timeZoneQuarterOffset);
  hashPod(hash, settings.mainMenuNav);
  hashPod(hash, settings.sleepClockRefreshInterval);
  hashPod(hash, settings.shakePageTurn);
  hashPod(hash, settings.shakePageTurnSensitivity);
  hashPod(hash, settings.uiTheme);
  hashPod(hash, settings.libraryShelfEnabled);
  return hash;
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
  {
    SystemSetting* mut = const_cast<SystemSetting*>(this);
    if (mut->recentVisibleCount < 1 || mut->recentVisibleCount > 9) mut->recentVisibleCount = 9;
    if (mut->librarySortEnabled > 1) mut->librarySortEnabled = 1;
    if (mut->libraryShelfEnabled > 1) mut->libraryShelfEnabled = 0;
    if (mut->librarySortMode > 7) mut->librarySortMode = 0;
    if (mut->libraryMode >= LIBRARY_MODE_COUNT) mut->libraryMode = LIBRARY_GRID;
    if (mut->libraryViewMode >= LIBRARY_VIEW_MODE_COUNT ||
        (mut->libraryViewMode == LIBRARY_VIEW_SHELF && mut->libraryShelfEnabled == 0))
      mut->libraryViewMode = LIBRARY_VIEW_FOLDERS;
    if (mut->sleepClockStyle >= SLEEP_CLOCK_STYLE_COUNT) mut->sleepClockStyle = CLOCK_CENTERED_DATE;
    if (mut->sleepClockTimeFormat >= CLOCK_TIME_FORMAT_COUNT) mut->sleepClockTimeFormat = CLOCK_24_HOUR;
    if (mut->sleepClockRefreshInterval >= CLOCK_REFRESH_INTERVAL_COUNT)
      mut->sleepClockRefreshInterval = CLOCK_REFRESH_OFF;
    if (mut->sleepImageQuality >= SLEEP_IMAGE_QUALITY_COUNT) mut->sleepImageQuality = SLEEP_IMAGE_HIGH;
    if (mut->timeZoneQuarterOffset > 104) mut->timeZoneQuarterOffset = 80;
    if (mut->shakePageTurn > 2) mut->shakePageTurn = 0;
    if (mut->shakePageTurnSensitivity > 2) mut->shakePageTurnSensitivity = 1;
    if (mut->uiTheme >= UI_THEME_COUNT) mut->uiTheme = UI_THEME_CLASSIC;
  }

  const uint32_t currentHash = settingsHash(*this);
  uint32_t storedHash = 0;
  if (hashFile(SETTINGS_FILE, storedHash) && storedHash == currentHash) {
    return true;
  }

  SdMan.mkdir("/.system");

  FsFile outputFile;

  if (!SdMan.openFileForWrite("CPS", SETTINGS_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, SETTINGS_FILE_VERSION);
  serialization::writePod(outputFile, SETTINGS_COUNT);
  serialization::writePod(outputFile, sleepScreen);
  serialization::writePod(outputFile, shortPwrBtn);
  serialization::writePod(outputFile, frontButtonLayout);
  serialization::writePod(outputFile, sideButtonLayout);
  serialization::writePod(outputFile, sleepTimeout);
  serialization::writePod(outputFile, sleepScreenCoverMode);
  serialization::writeString(outputFile, std::string(opdsServerUrl));
  serialization::writePod(outputFile, hideBatteryPercentage);
  serialization::writeString(outputFile, std::string(opdsUsername));
  serialization::writeString(outputFile, std::string(opdsPassword));
  serialization::writePod(outputFile, sleepScreenCoverFilter);
  serialization::writePod(outputFile, useLibraryIndex);
  serialization::writePod(outputFile, recentLibraryMode);
  serialization::writePod(outputFile, bootSetting);
  serialization::writePod(outputFile, sleepImageQuality);
  serialization::writeString(outputFile, std::string(sleepCustomBmp));
  serialization::writePod(outputFile, displayImageDither);
  serialization::writePod(outputFile, legacyDisplayImagePresentation);
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
  serialization::writePod(outputFile, sleepClockStyle);
  serialization::writePod(outputFile, sleepClockTimeFormat);
  serialization::writePod(outputFile, timeZoneQuarterOffset);
  serialization::writePod(outputFile, mainMenuNav);
  serialization::writePod(outputFile, sleepClockRefreshInterval);
  serialization::writePod(outputFile, shakePageTurn);
  serialization::writePod(outputFile, shakePageTurnSensitivity);
  serialization::writePod(outputFile, uiTheme);
  serialization::writePod(outputFile, libraryShelfEnabled);

  outputFile.close();
  saveUiThemeSetting(uiTheme);

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
    saveToFile();
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);

  if (version > SETTINGS_FILE_VERSION || version < MIN_SUPPORTED_SETTINGS_VERSION) {
    Serial.printf("[%lu] [CPS] Deserialization failed: Unsupported version %u (expected %u-%u)\n", millis(), version,
                  MIN_SUPPORTED_SETTINGS_VERSION, SETTINGS_FILE_VERSION);
    inputFile.close();
    return false;
  }

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);

  const bool shouldRewriteSettings = version < SETTINGS_FILE_VERSION || fileSettingsCount < SETTINGS_COUNT;
  uint8_t settingsRead = 0;

  do {
    readAndValidate(inputFile, sleepScreen, SLEEP_SCREEN_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, shortPwrBtn, SHORT_PWRBTN_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, frontButtonLayout, FRONT_BUTTON_LAYOUT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, sideButtonLayout, SIDE_BUTTON_LAYOUT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, sleepTimeout, SLEEP_TIMEOUT_COUNT);
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

    readAndValidate(inputFile, hideBatteryPercentage, HIDE_BATTERY_PERCENTAGE_COUNT);
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

    readAndValidate(inputFile, bootSetting, BOOT_SETTING_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, sleepImageQuality);
    if (sleepImageQuality >= SLEEP_IMAGE_QUALITY_COUNT) sleepImageQuality = SLEEP_IMAGE_HIGH;
    if (++settingsRead >= fileSettingsCount) break;

    {
      std::string sleepBmpStr;
      serialization::readString(inputFile, sleepBmpStr);
      setSleepCustomBmpFromInput(sleepBmpStr.c_str());
    }
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, displayImageDither, READER_IMAGE_DITHER_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, legacyDisplayImagePresentation, LEGACY_IMAGE_PRESENTATION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, refreshOnLoadRecent);
    if (refreshOnLoadRecent > 1) refreshOnLoadRecent = 0;
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, refreshOnLoadLibrary);
    if (refreshOnLoadLibrary > 1) refreshOnLoadLibrary = 0;
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, refreshOnLoadSettings);
    if (refreshOnLoadSettings > 1) refreshOnLoadSettings = 0;
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, refreshOnLoadSync);
    if (refreshOnLoadSync > 1) refreshOnLoadSync = 0;
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, refreshOnLoadStatistics);
    if (refreshOnLoadStatistics > 1) refreshOnLoadStatistics = 0;
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, bitmapRoundedCorners);
    if (bitmapRoundedCorners > 2) bitmapRoundedCorners = 0;
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, recentVisibleCount);
    if (recentVisibleCount < 1 || recentVisibleCount > 9) recentVisibleCount = 9;
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, librarySortEnabled);
    if (librarySortEnabled > 1) librarySortEnabled = 1;
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, librarySortMode);
    if (librarySortMode > 7) librarySortMode = 0;
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, libraryMode, LIBRARY_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, libraryViewMode, LIBRARY_VIEW_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, sleepClockStyle, SLEEP_CLOCK_STYLE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, sleepClockTimeFormat, CLOCK_TIME_FORMAT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, timeZoneQuarterOffset);
    if (timeZoneQuarterOffset > 104) timeZoneQuarterOffset = 80;
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, mainMenuNav, MAIN_MENU_NAV_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, sleepClockRefreshInterval, CLOCK_REFRESH_INTERVAL_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, shakePageTurn);
    if (shakePageTurn > 2) shakePageTurn = 0;
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, shakePageTurnSensitivity);
    if (shakePageTurnSensitivity > 2) shakePageTurnSensitivity = 1;
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, uiTheme, UI_THEME_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, libraryShelfEnabled);
    if (libraryShelfEnabled > 1) libraryShelfEnabled = 0;
    ++settingsRead;

  } while (false);

  inputFile.close();

  if (recentVisibleCount < 1 || recentVisibleCount > 9) recentVisibleCount = 9;
  if (librarySortEnabled > 1) librarySortEnabled = 1;
  if (libraryShelfEnabled > 1) libraryShelfEnabled = 0;
  if (librarySortMode > 7) librarySortMode = 0;
  if (sleepClockStyle >= SLEEP_CLOCK_STYLE_COUNT) sleepClockStyle = CLOCK_CENTERED_DATE;
  if (sleepClockTimeFormat >= CLOCK_TIME_FORMAT_COUNT) sleepClockTimeFormat = CLOCK_24_HOUR;
  if (sleepClockRefreshInterval >= CLOCK_REFRESH_INTERVAL_COUNT) sleepClockRefreshInterval = CLOCK_REFRESH_OFF;
  if (sleepImageQuality >= SLEEP_IMAGE_QUALITY_COUNT) sleepImageQuality = SLEEP_IMAGE_HIGH;
  if (timeZoneQuarterOffset > 104) timeZoneQuarterOffset = 80;
  if (libraryMode >= LIBRARY_MODE_COUNT) libraryMode = LIBRARY_GRID;
  if (libraryViewMode >= LIBRARY_VIEW_MODE_COUNT ||
      (libraryViewMode == LIBRARY_VIEW_SHELF && libraryShelfEnabled == 0)) {
    libraryViewMode = LIBRARY_VIEW_FOLDERS;
  }
  if (uiTheme >= UI_THEME_COUNT) uiTheme = UI_THEME_CLASSIC;
  loadUiThemeSetting(uiTheme);

  Serial.printf("[%lu] [CPS] Settings loaded (version %u, %u items)\n", millis(), version, settingsRead);

  if (shouldRewriteSettings) {
    saveToFile();
  }

  return true;
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
