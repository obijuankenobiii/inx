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

        // --- LOAD NEW MAPPINGS ---
        readAndValidate(inputFile, readerDirectionMapping, READER_DIRECTION_MAPPING_COUNT);
        if (++settingsRead >= fileSettingsCount) break;

        readAndValidate(inputFile, readerMenuButton, READER_MENU_BUTTON_COUNT);
        if (++settingsRead >= fileSettingsCount) break;
        
        // --- LOAD BOOT SETTING ---
        readAndValidate(inputFile, bootSetting, BOOT_SETTING_COUNT);
        if (++settingsRead >= fileSettingsCount) break;
        
        // --- LOAD NEW STATUS BAR SECTIONS (if version 4) ---
        if (version >= 4) {
            readAndValidate(inputFile, statusBarLeft, STATUS_BAR_ITEM_COUNT);
            if (++settingsRead >= fileSettingsCount) break;
            
            readAndValidate(inputFile, statusBarMiddle, STATUS_BAR_ITEM_COUNT);
            if (++settingsRead >= fileSettingsCount) break;
            
            readAndValidate(inputFile, statusBarRight, STATUS_BAR_ITEM_COUNT);
            if (++settingsRead >= fileSettingsCount) break;
        } else {
            // Migrate from legacy statusBar to new sections
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

    } while (false);

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