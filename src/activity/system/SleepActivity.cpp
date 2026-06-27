/**
 * @file SleepActivity.cpp
 * @brief Definitions for SleepActivity.
 */

#include "SleepActivity.h"

#include <Arduino.h>

#include <Bitmap.h>
#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/Section.h>
#include <GfxRenderer.h>
#include <ImageRender.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <SDCardManager.h>
#include <Txt.h>
#include <Xtc.h>
#include <esp_task_wdt.h>

#include "../reader/Epub/StatusBar.h"
#include "images/CorgiSleep.h"
#include "state/BookProgress.h"
#include "state/BookSetting.h"
#include "state/RecentBooks.h"
#include "state/Session.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/ScreenComponents.h"
#include "system/SleepClockRenderer.h"
#include "system/StoredClock.h"
#include "util/StringUtils.h"
#include <cmath>
#include <memory>

namespace {
bool isSleepImagePathJpeg(const std::string& path) {
  return StringUtils::checkFileExtension(path, ".jpg") || StringUtils::checkFileExtension(path, ".jpeg");
}
bool isSupportedSleepImageFile(const std::string& filename) {
  return StringUtils::checkFileExtension(filename, ".bmp") || StringUtils::checkFileExtension(filename, ".jpg") ||
         StringUtils::checkFileExtension(filename, ".jpeg");
}

bool sleepTwoBitEnabled() {
  return SETTINGS.sleepImageQuality != SystemSetting::SLEEP_IMAGE_LOW &&
         SETTINGS.sleepScreenCoverFilter == SystemSetting::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
}

bool sleepImageQualityEnabled() {
  return SETTINGS.sleepImageQuality == SystemSetting::SLEEP_IMAGE_HIGH;
}

ImageRenderMode sleepImageRenderMode() {
  return sleepTwoBitEnabled() ? ImageRenderMode::TwoBit : ImageRenderMode::OneBit;
}

void runSleepImageTwoBitPasses(GfxRenderer& renderer, const std::string& imagePath,
                               const ImageRender::Options& baseOptions, const bool allowQuality = true) {
  if (!sleepTwoBitEnabled()) {
    return;
  }

  ImageRender::Options options = baseOptions;
  options.mode = ImageRenderMode::TwoBit;
  options.useDisplayCache = true;
  // Transparent overlays cap at MEDIUM (allowQuality=false): the HIGH quality LUT can't composite over existing
  // content, so HIGH transparent removes the background instead (see renderTransparentSleepScreen).
  const bool quality = allowQuality && sleepImageQualityEnabled();
  options.quality = quality;
  options.fastQuality = false;

  ImageRender::create(renderer, imagePath)
      .displayGrayscale(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(), options, quality);
}

void recordSleepImageUsed() {
  APP_STATE.lastSleepImage = (APP_STATE.lastSleepImage + 1) & 0xFF;
  APP_STATE.saveToFile();
}

std::string pathForFixedSleepBmp() {
  if (SETTINGS.sleepCustomBmp[0] == '\0') {
    return "";
  }
  if (strcmp(SETTINGS.sleepCustomBmp, "/sleep.bmp") == 0) return SdMan.exists("/sleep.bmp") ? "/sleep.bmp" : "";
  if (strcmp(SETTINGS.sleepCustomBmp, "/sleep.jpg") == 0) return SdMan.exists("/sleep.jpg") ? "/sleep.jpg" : "";
  if (strcmp(SETTINGS.sleepCustomBmp, "/sleep.jpeg") == 0)
    return SdMan.exists("/sleep.jpeg") ? "/sleep.jpeg" : "";
  const std::string path = std::string("/sleep/") + SETTINGS.sleepCustomBmp;
  if (SdMan.exists(path.c_str())) {
    return path;
  }
  return "";
}

std::string pickSleepBmpPath() {
  std::string fixed = pathForFixedSleepBmp();
  if (!fixed.empty()) {
    return fixed;
  }

  std::string selectedPath;
  auto dir = SdMan.open("/sleep");

  if (dir && dir.isDirectory()) {
    size_t matchCount = 0;
    char name[256];
    while (auto file = dir.openNextFile()) {
      file.getName(name, sizeof(name));
      std::string filename = name;
      if (filename[0] != '.' && isSupportedSleepImageFile(filename)) {
        matchCount++;
        
        if (random(matchCount) == 0) {
          selectedPath = "/sleep/" + filename;
        }
      }
      file.close();
    }
    dir.close();
  }

  if (!selectedPath.empty()) {
    return selectedPath;
  }
  if (SdMan.exists("/sleep.bmp")) {
    return "/sleep.bmp";
  }
  if (SdMan.exists("/sleep.jpg")) {
    return "/sleep.jpg";
  }
  if (SdMan.exists("/sleep.jpeg")) {
    return "/sleep.jpeg";
  }
  return "";
}

std::string resolveLastReadCoverPathForSleep(const std::string& path) {
  std::string coverPath;

  if (StringUtils::checkFileExtension(path, ".epub")) {
    Epub book(path, "/.metadata/epub");
    if (book.load()) {
      const bool cropped = false;
      const std::string coverJpegPath = book.getCoverJpegPath(cropped);
      const std::string coverBmpPath = book.getCoverBmpPath(cropped);
      coverPath = SdMan.exists(coverJpegPath.c_str()) ? coverJpegPath : coverBmpPath;
      if (!SdMan.exists(coverPath.c_str())) {
        book.generateCoverBmp(cropped);
        coverPath = SdMan.exists(coverJpegPath.c_str()) ? coverJpegPath : coverBmpPath;
      }
      if (!SdMan.exists(coverPath.c_str())) {
        coverPath.clear();
      }
    }
  }

  if (StringUtils::checkFileExtension(path, ".xtc") || StringUtils::checkFileExtension(path, ".xtch")) {
    Xtc book(path, "/.metadata/xtc");
    if (book.load()) {
      book.setupCacheDir();
      if (!SdMan.exists(book.getCoverBmpPath().c_str())) {
        book.generateCoverBmp();
      }
      if (SdMan.exists(book.getCoverBmpPath().c_str())) {
        coverPath = book.getCoverBmpPath();
      }
    }
  }

  if (StringUtils::checkFileExtension(path, ".txt")) {
    Txt book(path, "/.system");
    if (book.load() && book.generateCoverBmp()) {
      coverPath = book.getCoverBmpPath();
    }
  }

  return coverPath;
}
}  // namespace

/**
 * @brief Initializes and renders the sleep screen when activity becomes active.
 * 
 * Selects and renders the appropriate sleep screen based on the current
 * sleep screen mode setting (transparent, blank, custom, cover, or default).
 */
void SleepActivity::onEnter() {
  Activity::onEnter();

  // HIGH-quality cover/custom images: our quality LUT doesn't fully clear to white on its own (whites come out
  // gray without a white baseline), so we pre-clear to white with a HALF refresh. A FAST pre-clear only fully
  // whitens a mostly-white incoming screen (e.g. a book text page); coming from the home/library screen (dark
  // covers) a single FAST pass leaves gray residue and the quality refresh then starts dirty. HALF reliably
  // clears from any screen.
  if (SETTINGS.sleepScreen != SystemSetting::SLEEP_SCREEN_MODE::TRANSPARENT &&
      SETTINGS.sleepScreen != SystemSetting::SLEEP_SCREEN_MODE::DATETIME) {
    renderer.clearScreen(0Xff);
    renderer.displayBuffer();
  }

  switch (SETTINGS.sleepScreen) {
    case SystemSetting::SLEEP_SCREEN_MODE::TRANSPARENT:
      renderTransparentSleepScreen();
      break;
    case SystemSetting::SLEEP_SCREEN_MODE::BLANK:
      renderBlankSleepScreen();
      break;
    case SystemSetting::SLEEP_SCREEN_MODE::CUSTOM:
      renderCustomSleepScreen();
      break;
    case SystemSetting::SLEEP_SCREEN_MODE::COVER:
      renderCoverSleepScreen();
      break;
    case SystemSetting::SLEEP_SCREEN_MODE::DATETIME:
      renderDateTimeSleepScreen();
      break;
    default:
      renderDefaultSleepScreen();
      break;
  }
}

/**
 * @brief Renders a custom sleep screen from user-provided images.
 * 
 * Uses a fixed image from settings when set; otherwise picks randomly from /sleep/
 * and SD-root sleep.bmp/jpg/jpeg. Falls back to default sleep screen if no images are found.
 */
void SleepActivity::renderCustomSleepScreen() const {
  const std::string imagePath = pickSleepBmpPath();
  if (!imagePath.empty()) {
    recordSleepImageUsed();

    if (isSleepImagePathJpeg(imagePath)) {
      ImageRender::Options options;
      options.cropToFill = SETTINGS.sleepScreenCoverMode == SystemSetting::SLEEP_SCREEN_COVER_MODE::FIT;
      options.mode = sleepImageRenderMode();
      options.useDisplayCache = true;
      if (ImageRender::create(renderer, imagePath)
              .render(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(), options)) {
        if (!sleepImageQualityEnabled()) {
          renderer.displayBuffer();
        }
        runSleepImageTwoBitPasses(renderer, imagePath, options);
        return;
      }
    }
    FsFile file;
    if (SdMan.openFileForRead("SLP", imagePath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        if (SETTINGS.sleepScreenCoverMode == SystemSetting::SLEEP_SCREEN_COVER_MODE::FIT) {
          renderFill(bitmap);
        } else {
          renderBitmapSleepScreen(bitmap);
        }
        file.close();
        return;
      }
      file.close();
    }
  }

  renderDefaultSleepScreen();
}

/**
 * @brief Renders a transparent overlay sleep screen.
 * 
 * Displays a semi-transparent image overlay on top of the current screen content.
 */
void SleepActivity::renderTransparentSleepScreen() const {
  const std::string imagePath = pickSleepBmpPath();
  if (!imagePath.empty()) {
    recordSleepImageUsed();
    // Transparent overlays only work at LOW/MEDIUM. At HIGH the quality LUT can't composite over the existing
    // screen, so remove the background (clear to white) and render the image opaque instead.
    const bool removeBackground = sleepImageQualityEnabled();
    if (isSleepImagePathJpeg(imagePath)) {
      ImageRender::Options options;
      options.cropToFill = SETTINGS.sleepScreenCoverMode == SystemSetting::SLEEP_SCREEN_COVER_MODE::FIT;
      options.mode = sleepImageRenderMode();
      options.useDisplayCache = false;
      if (removeBackground) {
        renderer.clearScreen();
      }
      if (ImageRender::create(renderer, imagePath)
              .render(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(), options)) {
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        runSleepImageTwoBitPasses(renderer, imagePath, options, /*allowQuality=*/false);
        return;
      }
    }
    FsFile file;
    if (SdMan.openFileForRead("SLP", imagePath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        if (removeBackground) {
          renderer.clearScreen();
        }
        renderer.bitmap.transparent(bitmap, 0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(), 1);
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        return;
      }
    }
  }


  renderDefaultSleepScreen();
}

/**
 * @brief Renders the cover of the last opened book as sleep screen.
 * 
 * Extracts and displays the cover image from the most recently opened book
 * (EPUB, XTC, or TXT format). Applies cropping or scaling based on settings.
 */
void SleepActivity::renderCoverSleepScreen() const {
  if (APP_STATE.lastRead.empty()) {
    return renderCustomSleepScreen();
  }

  const std::string coverPath = resolveLastReadCoverPathForSleep(APP_STATE.lastRead);

  if (!coverPath.empty() && isSleepImagePathJpeg(coverPath)) {
    renderer.clearScreen();
    ImageRender::Options options;
    options.cropToFill = SETTINGS.sleepScreenCoverMode == SystemSetting::SLEEP_SCREEN_COVER_MODE::FIT;
    options.mode = sleepImageRenderMode();
    options.useDisplayCache = true;
    if (ImageRender::create(renderer, coverPath)
            .render(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(), options)) {
      if (SETTINGS.sleepScreenCoverFilter == SystemSetting::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
        renderer.invertScreen();
      }
      if (!sleepImageQualityEnabled()) {
        renderer.displayBuffer();
      }
      runSleepImageTwoBitPasses(renderer, coverPath, options);
      return;
    }
    return renderCustomSleepScreen();
  }

  FsFile file;
  if (!coverPath.empty() && SdMan.openFileForRead("SLP", coverPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      if (SETTINGS.sleepScreenCoverMode == SystemSetting::SLEEP_SCREEN_COVER_MODE::FIT) {
        renderFill(bitmap);
      } else {
        renderBitmapSleepScreen(bitmap);
      }
    }
    file.close();
    return;
  }

  renderCustomSleepScreen();
}

void SleepActivity::renderFill(const Bitmap& bitmap) const {
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  float cropX = 0.0f;
  float cropY = 0.0f;
  constexpr int x = 0;
  constexpr int y = 0;

  const float iw = static_cast<float>(bitmap.getWidth());
  const float ih = static_cast<float>(bitmap.getHeight());
  if (iw > 0.f && ih > 0.f) {
    const float ir = iw / ih;
    const float tr = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);
    if (ir > tr) {
      cropX = 1.0f - (tr / ir);
    } else if (ir < tr) {
      cropY = 1.0f - (ir / tr);
    }
  }

  renderer.clearScreen();

  const bool hasTwoBit = bitmap.hasGreyscale() && sleepTwoBitEnabled();

  // Use drawSleepScreen (not drawBitmap) so 2-bit off matches Crop mode: sleep uses a
  // stricter BW map; drawBitmap would still dither 2bpp and look grey when Fill is selected.
  constexpr bool kCoverFill = true;
  renderer.bitmap.sleepScreen(bitmap, x, y, pageWidth, pageHeight, cropX, cropY, kCoverFill, sleepImageRenderMode());

  if (SETTINGS.sleepScreenCoverFilter == SystemSetting::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  // Quality grayscale does a single clean+draw refresh; skip the BW pre-flash so it's one flash, not two.
  if (!(hasTwoBit && sleepImageQualityEnabled())) {
    renderer.displayBuffer();
  }

  if (hasTwoBit) {
    const bool quality = sleepImageQualityEnabled();
    if (quality) {
      renderer.renderGrayscalePasses(true, false, [&] {
        bitmap.rewindToData();
        renderer.clearScreen(0xFF);
        renderer.bitmap.sleepScreen(bitmap, x, y, pageWidth, pageHeight, cropX, cropY, kCoverFill,
                                    ImageRenderMode::TwoBit);
      });
    } else {
      bitmap.rewindToData();
      renderer.clearScreen(0x00);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
      renderer.bitmap.sleepScreen(bitmap, x, y, pageWidth, pageHeight, cropX, cropY, kCoverFill, ImageRenderMode::TwoBit);
      renderer.copyGrayscaleLsbBuffers();

      bitmap.rewindToData();
      renderer.clearScreen(0x00);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
      renderer.bitmap.sleepScreen(bitmap, x, y, pageWidth, pageHeight, cropX, cropY, kCoverFill, ImageRenderMode::TwoBit);
      renderer.copyGrayscaleMsbBuffers();

      renderer.displayGrayBuffer(false);
      renderer.setRenderMode(GfxRenderer::BW);
      renderer.cleanupGrayscaleWithFrameBuffer();
    }
  }
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap, const bool preCroppedEpubCover) const {
  (void)preCroppedEpubCover;

  int x, y;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float cropX = 0, cropY = 0;

  Serial.printf("[SLP] bitmap %d x %d, screen %d x %d\n", bitmap.getWidth(), bitmap.getHeight(), pageWidth,
                pageHeight);
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    Serial.printf("[SLP] bitmap ratio: %f, screen ratio: %f\n", ratio, screenRatio);
    if (ratio > screenRatio) {
      if (SETTINGS.sleepScreenCoverMode == SystemSetting::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropX = 1.0f - (screenRatio / ratio);
        Serial.printf("[SLP] Cropping bitmap x: %f\n", cropX);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = static_cast<int>(std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2));
      Serial.printf("[SLP] Centering with ratio %f to y=%d\n", ratio, y);
    } else {
      if (SETTINGS.sleepScreenCoverMode == SystemSetting::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropY = 1.0f - (ratio / screenRatio);
        Serial.printf("[SLP] Cropping bitmap y: %f\n", cropY);
        ratio = static_cast<float>(bitmap.getWidth()) / ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = static_cast<int>(std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2));
      y = 0;
      Serial.printf("[SLP] Centering with ratio %f to x=%d\n", ratio, x);
    }
  } else {
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  Serial.printf("[SLP] drawing to %d x %d\n", x, y);
  renderer.clearScreen();

  const bool coverFill = SETTINGS.sleepScreenCoverMode == SystemSetting::SLEEP_SCREEN_COVER_MODE::FIT;
  const bool hasTwoBit = bitmap.hasGreyscale() && sleepTwoBitEnabled();

  renderer.bitmap.sleepScreen(bitmap, x, y, pageWidth, pageHeight, cropX, cropY, coverFill, sleepImageRenderMode());

  if (SETTINGS.sleepScreenCoverFilter == SystemSetting::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  // Quality grayscale does a single clean+draw refresh; skip the BW pre-flash so it's one flash, not two.
  if (!(hasTwoBit && sleepImageQualityEnabled())) {
    renderer.displayBuffer();
  }

  if (hasTwoBit) {
    const bool quality = sleepImageQualityEnabled();
    if (quality) {
      renderer.renderGrayscalePasses(true, false, [&] {
        bitmap.rewindToData();
        renderer.clearScreen(0xFF);
        renderer.bitmap.sleepScreen(bitmap, x, y, pageWidth, pageHeight, cropX, cropY, coverFill,
                                    ImageRenderMode::TwoBit);
      });
    } else {
      bitmap.rewindToData();
      renderer.clearScreen(0x00);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
      renderer.bitmap.sleepScreen(bitmap, x, y, pageWidth, pageHeight, cropX, cropY, coverFill, ImageRenderMode::TwoBit);
      renderer.copyGrayscaleLsbBuffers();

      bitmap.rewindToData();
      renderer.clearScreen(0x00);
      renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
      renderer.bitmap.sleepScreen(bitmap, x, y, pageWidth, pageHeight, cropX, cropY, coverFill, ImageRenderMode::TwoBit);
      renderer.copyGrayscaleMsbBuffers();

      renderer.displayGrayBuffer(false);
      renderer.setRenderMode(GfxRenderer::BW);
      renderer.cleanupGrayscaleWithFrameBuffer();
    }
  }
}

/**
 * @brief Renders the default sleep screen with Corgi logo.
 * 
 * Displays the CorgiSleep logo centered on the screen with optional inversion
 * based on sleep screen mode settings.
 */
void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.rectangle.fill(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
  renderer.clearScreen();
  renderer.bitmap.icon(CorgiSleep, (pageWidth - 256) / 2, (pageHeight - 256) / 2, 256, 256);

  if (SETTINGS.sleepScreen != SystemSetting::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

/**
 * @brief Renders a minimal date/time sleep screen using the X3 RTC.
 */
void SleepActivity::renderDateTimeSleepScreen() const {
  HalGPIO::DateTime dateTime;
  bool hasClock = false;
  if (gpio.deviceIsX3()) {
    hasClock = gpio.readDateTime(dateTime);
  } else {
    hasClock = StoredClock::load(dateTime);
  }

  renderer.clearScreen(0xff);
  SleepClockRenderer::DateTimeView view;
  if (hasClock) {
    view.year = dateTime.year;
    view.month = dateTime.month;
    view.day = dateTime.day;
    view.hour = dateTime.hour;
    view.minute = dateTime.minute;
    view.weekday = dateTime.weekday;
  }
  SleepClockRenderer::render(renderer, SETTINGS.sleepClockStyle, view, hasClock, 0, 0, renderer.getScreenWidth(),
                             renderer.getScreenHeight());

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

/**
 * @brief Renders a completely blank sleep screen.
 * 
 * Clears the screen to save power and prevent screen burn-in during sleep.
 */
void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
