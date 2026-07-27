/**
 * @file ReaderSetting.cpp
 * @brief Definitions for ReaderSetting.
 */

#include "state/ReaderSetting.h"

#include "state/SystemSetting.h"

#ifndef INX_SIMULATOR_WEB_ONLY
#include <GfxRenderer.h>
#include <HalDisplay.h>
#endif
#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

#include <cstring>
#include <string>

#ifndef INX_SIMULATOR_WEB_ONLY
#include "system/FontManager.h"
#include "system/Fonts.h"
#endif

// Defined in SystemSetting.cpp; shared here rather than duplicated.
void readAndValidate(FsFile& file, uint8_t& member, uint8_t maxValue);

ReaderSetting ReaderSetting::instance;

namespace {
constexpr uint8_t READER_SETTINGS_FILE_VERSION = 1;
// Must equal the number of data fields read by the do-while loop in loadFromFile() (currently 39,
// through longPressChapterSkip) - NOT counting the version/count header fields read separately
// before the loop. See SystemSetting.cpp's SETTINGS_COUNT comment for how this header is used.
constexpr uint8_t READER_SETTINGS_COUNT = 39;
constexpr uint8_t LEGACY_IMAGE_PRESENTATION_COUNT = 4;
constexpr char READER_SETTINGS_FILE[] = "/.system/reader_settings.bin";
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
  if (!SdMan.openFileForRead("CPR", path, file)) {
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

bool validRefreshFrequency(const uint8_t value) {
  return value == 1 || value == 5 || value == 10 || value == 15 || value == 30;
}

uint32_t readerSettingsHash(const ReaderSetting& settings, const uint8_t fontFamilyToSave) {
  uint32_t hash = FNV1A_OFFSET;
  hashPod(hash, READER_SETTINGS_FILE_VERSION);
  hashPod(hash, READER_SETTINGS_COUNT);
  hashPod(hash, settings.statusBar);
  hashPod(hash, settings.statusBarLeft);
  hashPod(hash, settings.statusBarMiddle);
  hashPod(hash, settings.statusBarRight);
  hashPod(hash, settings.statusBarFullStyle);
  hashPod(hash, settings.extraParagraphSpacing);
  hashPod(hash, settings.textAntiAliasing);
  hashPod(hash, settings.readerShortPwrBtn);
  hashPod(hash, settings.xtcShortPwrBtn);
  hashPod(hash, settings.xtcPageAutoTurnSeconds);
  hashPod(hash, settings.xtcImageQuality);
  hashPod(hash, settings.xtcRefreshFrequency);
  hashString(hash, settings.dictionaryFolder);
  hashPod(hash, settings.btnUpShortAction);
  hashPod(hash, settings.btnUpLongAction);
  hashPod(hash, settings.btnDownShortAction);
  hashPod(hash, settings.btnDownLongAction);
  hashPod(hash, settings.btnLeftShortAction);
  hashPod(hash, settings.btnLeftLongAction);
  hashPod(hash, settings.btnRightShortAction);
  hashPod(hash, settings.btnRightLongAction);
  hashPod(hash, settings.btnPowerShortAction);
  hashPod(hash, settings.orientation);
  hashPod(hash, settings.readerDirectionMapping);
  hashPod(hash, settings.readerMenuButton);
  hashPod(hash, fontFamilyToSave);
  hashPod(hash, settings.fontSize);
  hashPod(hash, settings.lineHeight);
  hashPod(hash, settings.textSpace);
  hashPod(hash, settings.paragraphAlignment);
  hashPod(hash, settings.paragraphCssIndentEnabled);
  hashPod(hash, settings.refreshFrequency);
  hashPod(hash, settings.hyphenationEnabled);
  hashPod(hash, settings.bionicReadingEnabled);
  hashPod(hash, settings.screenMargin);
  hashPod(hash, settings.pageAutoTurnSeconds);
  hashPod(hash, settings.readerImageGrayscale);
  hashPod(hash, settings.readerSmartRefreshOnImages);
  hashPod(hash, settings.legacyReaderImagePresentation);
  hashPod(hash, settings.readerImageDither);
  hashPod(hash, settings.longPressChapterSkip);
  return hash;
}
}  // namespace

/**
 * @brief Saves all reader settings to file
 * @return true if save successful, false otherwise
 */
bool ReaderSetting::saveToFile() const {
  uint8_t fontFamilyToSave = fontFamily;
#ifndef INX_SIMULATOR_WEB_ONLY
  FontManager::clampReaderFontFamilySlot(fontFamilyToSave);
  if (fontFamilyToSave != fontFamily) {
    const_cast<ReaderSetting*>(this)->fontFamily = fontFamilyToSave;
  }
#endif

  {
    ReaderSetting* mut = const_cast<ReaderSetting*>(this);
    if (mut->xtcImageQuality >= SystemSetting::READER_IMAGE_QUALITY_COUNT) {
      mut->xtcImageQuality = SystemSetting::READER_IMAGE_LOW;
    }
    if (mut->xtcShortPwrBtn >= SystemSetting::XTC_SHORT_PWRBTN_COUNT) mut->xtcShortPwrBtn = SystemSetting::XTC_POWER_NEXT;
    if (mut->xtcPageAutoTurnSeconds > 60 || mut->xtcPageAutoTurnSeconds % 10 != 0) mut->xtcPageAutoTurnSeconds = 0;
    if (!validRefreshFrequency(mut->xtcRefreshFrequency)) mut->xtcRefreshFrequency = 15;
  }

  const uint32_t currentHash = readerSettingsHash(*this, fontFamilyToSave);
  uint32_t storedHash = 0;
  if (hashFile(READER_SETTINGS_FILE, storedHash) && storedHash == currentHash) {
    return true;
  }

  SdMan.mkdir("/.system");

  FsFile outputFile;
  if (!SdMan.openFileForWrite("CPR", READER_SETTINGS_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, READER_SETTINGS_FILE_VERSION);
  serialization::writePod(outputFile, READER_SETTINGS_COUNT);
  serialization::writePod(outputFile, statusBar);
  serialization::writePod(outputFile, statusBarLeft);
  serialization::writePod(outputFile, statusBarMiddle);
  serialization::writePod(outputFile, statusBarRight);
  serialization::writePod(outputFile, statusBarFullStyle);
  serialization::writePod(outputFile, extraParagraphSpacing);
  serialization::writePod(outputFile, textAntiAliasing);
  serialization::writePod(outputFile, readerShortPwrBtn);
  serialization::writePod(outputFile, xtcShortPwrBtn);
  serialization::writePod(outputFile, xtcPageAutoTurnSeconds);
  serialization::writePod(outputFile, xtcImageQuality);
  serialization::writePod(outputFile, xtcRefreshFrequency);
  serialization::writeString(outputFile, std::string(dictionaryFolder));
  serialization::writePod(outputFile, btnUpShortAction);
  serialization::writePod(outputFile, btnUpLongAction);
  serialization::writePod(outputFile, btnDownShortAction);
  serialization::writePod(outputFile, btnDownLongAction);
  serialization::writePod(outputFile, btnLeftShortAction);
  serialization::writePod(outputFile, btnLeftLongAction);
  serialization::writePod(outputFile, btnRightShortAction);
  serialization::writePod(outputFile, btnRightLongAction);
  serialization::writePod(outputFile, btnPowerShortAction);
  serialization::writePod(outputFile, orientation);
  serialization::writePod(outputFile, readerDirectionMapping);
  serialization::writePod(outputFile, readerMenuButton);
  serialization::writePod(outputFile, fontFamilyToSave);
  serialization::writePod(outputFile, fontSize);
  serialization::writePod(outputFile, lineHeight);
  serialization::writePod(outputFile, textSpace);
  serialization::writePod(outputFile, paragraphAlignment);
  serialization::writePod(outputFile, paragraphCssIndentEnabled);
  serialization::writePod(outputFile, refreshFrequency);
  serialization::writePod(outputFile, hyphenationEnabled);
  serialization::writePod(outputFile, bionicReadingEnabled);
  serialization::writePod(outputFile, screenMargin);
  serialization::writePod(outputFile, pageAutoTurnSeconds);
  serialization::writePod(outputFile, readerImageGrayscale);
  serialization::writePod(outputFile, readerSmartRefreshOnImages);
  serialization::writePod(outputFile, legacyReaderImagePresentation);
  serialization::writePod(outputFile, readerImageDither);
  serialization::writePod(outputFile, longPressChapterSkip);

  outputFile.close();

  Serial.printf("[%lu] [CPR] Reader settings saved to file (version %u)\n", millis(), READER_SETTINGS_FILE_VERSION);
  return true;
}

/**
 * @brief Loads all reader settings from file
 * @return true if load successful, false otherwise
 */
bool ReaderSetting::loadFromFile() {
  FsFile inputFile;

  if (!SdMan.openFileForRead("CPR", READER_SETTINGS_FILE, inputFile)) {
    saveToFile();
    return false;
  }

  uint8_t version;
  serialization::readPod(inputFile, version);

  if (version > READER_SETTINGS_FILE_VERSION) {
    Serial.printf("[%lu] [CPR] Deserialization failed: Unknown version %u (expected <= %u)\n", millis(), version,
                  READER_SETTINGS_FILE_VERSION);
    inputFile.close();
    return false;
  }

  uint8_t fileSettingsCount = 0;
  serialization::readPod(inputFile, fileSettingsCount);
  const bool shouldRewriteSettings = version < READER_SETTINGS_FILE_VERSION || fileSettingsCount < READER_SETTINGS_COUNT;
  uint8_t settingsRead = 0;

  do {
    readAndValidate(inputFile, statusBar, SystemSetting::STATUS_BAR_MODE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, statusBarLeft, SystemSetting::STATUS_BAR_ITEM_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, statusBarMiddle, SystemSetting::STATUS_BAR_ITEM_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, statusBarRight, SystemSetting::STATUS_BAR_ITEM_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, statusBarFullStyle, SystemSetting::STATUS_BAR_ITEM_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, extraParagraphSpacing);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, textAntiAliasing);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, readerShortPwrBtn, SystemSetting::READER_SHORT_PWRBTN_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, xtcShortPwrBtn, SystemSetting::XTC_SHORT_PWRBTN_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, xtcPageAutoTurnSeconds);
    if (xtcPageAutoTurnSeconds > 60 || xtcPageAutoTurnSeconds % 10 != 0) xtcPageAutoTurnSeconds = 0;
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, xtcImageQuality, SystemSetting::READER_IMAGE_QUALITY_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, xtcRefreshFrequency);
    if (!validRefreshFrequency(xtcRefreshFrequency)) xtcRefreshFrequency = 15;
    if (++settingsRead >= fileSettingsCount) break;

    {
      std::string dictFolderStr;
      serialization::readString(inputFile, dictFolderStr);
      strncpy(dictionaryFolder, dictFolderStr.c_str(), sizeof(dictionaryFolder) - 1);
      dictionaryFolder[sizeof(dictionaryFolder) - 1] = '\0';
    }
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, btnUpShortAction, SystemSetting::READER_BUTTON_ACTION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, btnUpLongAction, SystemSetting::READER_BUTTON_ACTION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, btnDownShortAction, SystemSetting::READER_BUTTON_ACTION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, btnDownLongAction, SystemSetting::READER_BUTTON_ACTION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, btnLeftShortAction, SystemSetting::READER_BUTTON_ACTION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, btnLeftLongAction, SystemSetting::READER_BUTTON_ACTION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, btnRightShortAction, SystemSetting::READER_BUTTON_ACTION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, btnRightLongAction, SystemSetting::READER_BUTTON_ACTION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, btnPowerShortAction, SystemSetting::READER_BUTTON_ACTION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, orientation, SystemSetting::ORIENTATION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, readerDirectionMapping, SystemSetting::READER_DIRECTION_MAPPING_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, readerMenuButton, SystemSetting::READER_MENU_BUTTON_COUNT);
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

    readAndValidate(inputFile, fontSize, SystemSetting::FONT_SIZE_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, lineHeight);
    if (lineHeight < 10 || lineHeight > 200) lineHeight = 100;
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, textSpace);
    if (textSpace < 10 || textSpace > 200) textSpace = 100;
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, paragraphAlignment, SystemSetting::PARAGRAPH_ALIGNMENT_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, paragraphCssIndentEnabled);
    if (paragraphCssIndentEnabled > 1) paragraphCssIndentEnabled = 1;
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, refreshFrequency, SystemSetting::REFRESH_FREQUENCY_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, hyphenationEnabled);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, bionicReadingEnabled);
    if (bionicReadingEnabled > 1) bionicReadingEnabled = 0;
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, screenMargin);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, pageAutoTurnSeconds);
    if (pageAutoTurnSeconds > 60 || pageAutoTurnSeconds % 10 != 0) pageAutoTurnSeconds = 0;
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, readerImageGrayscale);
    if (readerImageGrayscale >= SystemSetting::READER_IMAGE_QUALITY_COUNT) {
      readerImageGrayscale = SystemSetting::READER_IMAGE_MEDIUM;
    }
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, readerSmartRefreshOnImages);
    if (readerSmartRefreshOnImages > 1) readerSmartRefreshOnImages = 1;
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, legacyReaderImagePresentation, LEGACY_IMAGE_PRESENTATION_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    readAndValidate(inputFile, readerImageDither, SystemSetting::READER_IMAGE_DITHER_COUNT);
    if (++settingsRead >= fileSettingsCount) break;

    serialization::readPod(inputFile, longPressChapterSkip);
    if (longPressChapterSkip > SystemSetting::LONG_PRESS_PAGE_SKIP_5) {
      longPressChapterSkip = SystemSetting::LONG_PRESS_CHAPTER_SKIP;
    }
    ++settingsRead;

  } while (false);

  inputFile.close();

#ifndef INX_SIMULATOR_WEB_ONLY
  FontManager::clampReaderFontFamilySlot(fontFamily);
#endif

  if (xtcImageQuality >= SystemSetting::READER_IMAGE_QUALITY_COUNT) xtcImageQuality = SystemSetting::READER_IMAGE_LOW;
  if (xtcShortPwrBtn >= SystemSetting::XTC_SHORT_PWRBTN_COUNT) xtcShortPwrBtn = SystemSetting::XTC_POWER_NEXT;
  if (xtcPageAutoTurnSeconds > 60 || xtcPageAutoTurnSeconds % 10 != 0) xtcPageAutoTurnSeconds = 0;
  if (!validRefreshFrequency(xtcRefreshFrequency)) xtcRefreshFrequency = 15;

  Serial.printf("[%lu] [CPR] Reader settings loaded (version %u, %u items)\n", millis(), version, settingsRead);

  if (shouldRewriteSettings) {
    saveToFile();
  }

  return true;
}

/**
 * @brief Gets reader line compression factor based on font and spacing
 * @return Line compression multiplier
 */
float ReaderSetting::getReaderLineCompression() const {
  // lineHeight is a percentage of the font's natural line height (100 = normal). Clamp 10-200.
  uint8_t lh = lineHeight;
  if (lh < 10 || lh > 200) lh = 100;
  return static_cast<float>(lh) / 100.0f;
}

float ReaderSetting::getReaderWordSpacingFactor() const {
  // textSpace is a percentage of the natural inter-word space (100 = normal). Clamp 10-200.
  uint8_t ts = textSpace;
  if (ts < 10 || ts > 200) ts = 100;
  return static_cast<float>(ts) / 100.0f;
}

/**
 * @brief Gets screen refresh frequency in pages
 * @return Number of pages between refreshes
 */
int ReaderSetting::getRefreshFrequency() const {
  switch (refreshFrequency) {
    case SystemSetting::REFRESH_1:
      return 1;
    case SystemSetting::REFRESH_5:
      return 5;
    case SystemSetting::REFRESH_10:
      return 10;
    case SystemSetting::REFRESH_15:
    default:
      return 15;
    case SystemSetting::REFRESH_30:
      return 30;
  }
}

int ReaderSetting::getReaderFontIdForSettingsUi(uint8_t familySlot, uint8_t sizeIndex) const {
#ifdef INX_SIMULATOR_WEB_ONLY
  (void)familySlot;
  (void)sizeIndex;
  return 0;
#else
  if (familySlot < SystemSetting::FONT_FAMILY_BUILTIN_COUNT) {
    return getReaderFontIdForFamilyAndSize(familySlot, sizeIndex);
  }
  return getReaderFontIdForFamilyAndSize(SystemSetting::ATKINSON_HYPERLEGIBLE, sizeIndex);
#endif
}

int ReaderSetting::getReaderFontIdForFamilyAndSize(uint8_t family, uint8_t size) const {
#ifdef INX_SIMULATOR_WEB_ONLY
  (void)family;
  (void)size;
  return 0;
#else
  if (size >= SystemSetting::FONT_SIZE_COUNT) {
    size = SystemSetting::MEDIUM;
  }
  static const int kPtBySize[] = {10, 12, 14, 16, 18};
  const int preferredPt = kPtBySize[size];

  if (family >= SystemSetting::FONT_FAMILY_BUILTIN_COUNT) {
    const std::string sdName = FontManager::readerFontFamilyLabel(family);
    if (sdName == "Literata" || sdName == "Atkinson Hyperlegible") {
      return getReaderFontIdForFamilyAndSize(
          sdName == "Atkinson Hyperlegible" ? SystemSetting::ATKINSON_HYPERLEGIBLE : SystemSetting::LITERATA, size);
    }
    return FontManager::getFontIdNearestPointSize(sdName, preferredPt);
  }

  switch (family) {
    case SystemSetting::ATKINSON_HYPERLEGIBLE:
      switch (size) {
        case SystemSetting::EXTRA_SMALL:
          return ATKINSON_HYPERLEGIBLE_10_FONT_ID;
        case SystemSetting::SMALL:
          return ATKINSON_HYPERLEGIBLE_12_FONT_ID;
        case SystemSetting::MEDIUM:
        default:
          return ATKINSON_HYPERLEGIBLE_14_FONT_ID;
        case SystemSetting::LARGE:
          return ATKINSON_HYPERLEGIBLE_16_FONT_ID;
        case SystemSetting::EXTRA_LARGE:
          return ATKINSON_HYPERLEGIBLE_18_FONT_ID;
      }
    case SystemSetting::LITERATA:
    default:
      switch (size) {
        case SystemSetting::EXTRA_SMALL:
          return LITERATA_10_FONT_ID;
        case SystemSetting::SMALL:
          return LITERATA_12_FONT_ID;
        case SystemSetting::MEDIUM:
        default:
          return LITERATA_14_FONT_ID;
        case SystemSetting::LARGE:
          return LITERATA_16_FONT_ID;
        case SystemSetting::EXTRA_LARGE:
          return LITERATA_18_FONT_ID;
      }
  }
#endif
}

/**
 * @brief Gets reader font ID based on font family and size
 * @return Font identifier for rendering
 */
int ReaderSetting::getReaderFontId() const {
#ifdef INX_SIMULATOR_WEB_ONLY
  return 0;
#else
  return getReaderFontIdForFamilyAndSize(fontFamily, fontSize);
#endif
}
