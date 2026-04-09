#include "state/SystemSetting.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <cstring>

#include "system/FontManager.h"
#include "system/Fonts.h"

SystemSetting SystemSetting::instance;

/**
 * Reads a string from file
 */
void readString(FsFile& file, std::string& str) {
  uint16_t len;
  serialization::readPod(file, len);
  if (len > 0 && len < 1024) {
    std::vector<char> buffer(len + 1);
    file.read(buffer.data(), len);
    buffer[len] = '\0';
    str = std::string(buffer.data());
  } else {
    str.clear();
  }
}

/**
 * Writes a string to file
 */
void writeString(FsFile& file, const std::string& str) {
  uint16_t len = str.length();
  serialization::writePod(file, len);
  if (len > 0) {
    file.write(str.c_str(), len);
  }
}

namespace {
constexpr uint8_t SETTINGS_FILE_VERSION = 5;
constexpr uint8_t SETTINGS_COUNT = 33;
constexpr char SETTINGS_FILE[] = "/.system/settings.bin";
}  // namespace

/**
 * Saves all settings to file.
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

  // Write font settings as string and int
  writeString(outputFile, fontFamily);
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
  serialization::writePod(outputFile, readerShortPwrBtn);

  outputFile.close();

  Serial.printf("[%lu] [CPS] Settings saved to file (version %u)\n", millis(), SETTINGS_FILE_VERSION);
  return true;
}

/**
 * Loads all settings from file.
 */
bool SystemSetting::loadFromFile() {
  FsFile inputFile;

  if (!SdMan.openFileForRead("CPS", SETTINGS_FILE, inputFile)) {
    // Initialize default values
    fontFamily = "Atkinson Hyperlegible";
    fontSize = 12;
    statusBarLeft = STATUS_ITEM_BATTERY_ICON_WITH_PERCENT;
    statusBarMiddle = STATUS_ITEM_CHAPTER_TITLE;
    statusBarRight = STATUS_ITEM_PAGE_NUMBERS;
    saveToFile();
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);

  if (version != SETTINGS_FILE_VERSION) {
    Serial.printf("[%lu] [CPS] Wrong version %u, expected %u. Using defaults.\n", 
                  millis(), version, SETTINGS_FILE_VERSION);
    inputFile.close();
    fontFamily = "Atkinson Hyperlegible";
    fontSize = 12;
    statusBarLeft = STATUS_ITEM_BATTERY_ICON_WITH_PERCENT;
    statusBarMiddle = STATUS_ITEM_CHAPTER_TITLE;
    statusBarRight = STATUS_ITEM_PAGE_NUMBERS;
    saveToFile();
    return false;
  }

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);
  
  // Read all settings in order
  serialization::readPod(inputFile, sleepScreen);
  serialization::readPod(inputFile, extraParagraphSpacing);
  serialization::readPod(inputFile, shortPwrBtn);
  serialization::readPod(inputFile, statusBar);
  serialization::readPod(inputFile, orientation);
  serialization::readPod(inputFile, frontButtonLayout);
  serialization::readPod(inputFile, sideButtonLayout);
  
  // Read font settings
  readString(inputFile, fontFamily);
  if (fontFamily.empty()) fontFamily = "Atkinson Hyperlegible";
  serialization::readPod(inputFile, fontSize);
  if (fontSize < 8) fontSize = 12;
  if (fontSize > 18) fontSize = 12;
  
  serialization::readPod(inputFile, lineSpacing);
  serialization::readPod(inputFile, paragraphAlignment);
  serialization::readPod(inputFile, sleepTimeout);
  serialization::readPod(inputFile, refreshFrequency);
  serialization::readPod(inputFile, screenMargin);
  serialization::readPod(inputFile, sleepScreenCoverMode);
  
  {
    std::string urlStr;
    serialization::readString(inputFile, urlStr);
    strncpy(opdsServerUrl, urlStr.c_str(), sizeof(opdsServerUrl) - 1);
    opdsServerUrl[sizeof(opdsServerUrl) - 1] = '\0';
  }
  
  serialization::readPod(inputFile, textAntiAliasing);
  serialization::readPod(inputFile, hideBatteryPercentage);
  serialization::readPod(inputFile, longPressChapterSkip);
  serialization::readPod(inputFile, hyphenationEnabled);
  
  {
    std::string usernameStr;
    serialization::readString(inputFile, usernameStr);
    strncpy(opdsUsername, usernameStr.c_str(), sizeof(opdsUsername) - 1);
    opdsUsername[sizeof(opdsUsername) - 1] = '\0';
  }
  
  {
    std::string passwordStr;
    serialization::readString(inputFile, passwordStr);
    strncpy(opdsPassword, passwordStr.c_str(), sizeof(opdsPassword) - 1);
    opdsPassword[sizeof(opdsPassword) - 1] = '\0';
  }
  
  serialization::readPod(inputFile, sleepScreenCoverFilter);
  serialization::readPod(inputFile, useLibraryIndex);
  serialization::readPod(inputFile, recentLibraryMode);
  serialization::readPod(inputFile, readerDirectionMapping);
  serialization::readPod(inputFile, readerMenuButton);
  serialization::readPod(inputFile, bootSetting);
  serialization::readPod(inputFile, statusBarLeft);
  serialization::readPod(inputFile, statusBarMiddle);
  serialization::readPod(inputFile, statusBarRight);
  serialization::readPod(inputFile, readerShortPwrBtn);

  inputFile.close();

  Serial.printf("[%lu] [CPS] Settings loaded (version %u)\n", millis(), version);
  return true;
}

float SystemSetting::getReaderLineCompression() const {
  switch (lineSpacing) {
    case TIGHT: return 0.95f;
    case NORMAL: return 1.0f;
    case WIDE: return 1.1f;
    default: return 1.0f;
  }
}

unsigned long SystemSetting::getSleepTimeoutMs() const {
  switch (sleepTimeout) {
    case SLEEP_1_MIN: return 1UL * 60 * 1000;
    case SLEEP_5_MIN: return 5UL * 60 * 1000;
    case SLEEP_10_MIN: return 10UL * 60 * 1000;
    case SLEEP_15_MIN: return 15UL * 60 * 1000;
    case SLEEP_30_MIN: return 30UL * 60 * 1000;
    default: return 10UL * 60 * 1000;
  }
}

int SystemSetting::getRefreshFrequency() const {
  switch (refreshFrequency) {
    case REFRESH_1: return 1;
    case REFRESH_5: return 5;
    case REFRESH_10: return 10;
    case REFRESH_15: return 15;
    case REFRESH_30: return 30;
    default: return 15;
  }
}

int SystemSetting::getReaderFontId() const { 
  return FontManager::getFontId(fontFamily, fontSize); 
}