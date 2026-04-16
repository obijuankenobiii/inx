#include "SleepActivity.h"

#include <Epub.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Txt.h>
#include <Xtc.h>

#include "images/CorgiSleep.h"
#include "state/Session.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/ScreenComponents.h"
#include "util/StringUtils.h"

namespace {
std::string pickRandomSleepBmpPath() {
  std::string selectedPath;
  size_t matchCount = 0;
  auto dir = SdMan.open("/sleep");

  if (dir && dir.isDirectory()) {
    char name[256];
    while (auto file = dir.openNextFile()) {
      file.getName(name, sizeof(name));
      std::string filename = name;
      if (filename[0] != '.' && StringUtils::checkFileExtension(filename, ".bmp")) {
        matchCount++;
        // Reservoir sampling keeps memory usage constant.
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
  return "";
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

  if (SETTINGS.sleepScreen != SystemSetting::SLEEP_SCREEN_MODE::TRANSPARENT)
  {
    renderer.clearScreen();
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
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
    default:
      renderDefaultSleepScreen();
      break;
  }
}

/**
 * @brief Renders a custom sleep screen from user-provided images.
 * 
 * Loads random BMP images from the /sleep directory or root sleep.bmp.
 * Falls back to default sleep screen if no images are found.
 */
void SleepActivity::renderCustomSleepScreen() const {
  const std::string imagePath = pickRandomSleepBmpPath();
  if (!imagePath.empty()) {
    FsFile file;
    if (SdMan.openFileForRead("SLP", imagePath, file)) {
      Bitmap bitmap(file, true);
      APP_STATE.lastSleepImage = (APP_STATE.lastSleepImage + 1) & 0xFF;
      APP_STATE.saveToFile();
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderBitmapSleepScreen(bitmap);
        return;
      }
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
  const std::string imagePath = pickRandomSleepBmpPath();
  if (!imagePath.empty()) {
    FsFile file;
    if (SdMan.openFileForRead("SLP", imagePath, file)) {
      Bitmap bitmap(file, true);
      APP_STATE.lastSleepImage = (APP_STATE.lastSleepImage + 1) & 0xFF;
      APP_STATE.saveToFile();
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderer.drawTransparentImage(bitmap, 0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(), 1);
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

  std::string coverPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == SystemSetting::SLEEP_SCREEN_COVER_MODE::CROP;
  const std::string& path = APP_STATE.lastRead;

  if (StringUtils::checkFileExtension(path, ".epub")) {
    Epub book(path, "/.metadata/epub");
    if (book.load()) coverPath = book.getCoverBmpPath(cropped);
  }

  if (StringUtils::checkFileExtension(path, ".xtc") || StringUtils::checkFileExtension(path, ".xtch")) {
    Xtc book(path, "/.system");
    if (book.load() && book.generateCoverBmp()) coverPath = book.getCoverBmpPath();
  }

  if (StringUtils::checkFileExtension(path, ".txt")) {
    Txt book(path, "/.system");
    if (book.load() && book.generateCoverBmp()) coverPath = book.getCoverBmpPath();
  }

  FsFile file;
  if (!coverPath.empty() && SdMan.openFileForRead("SLP", coverPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      renderBitmapSleepScreen(bitmap);
    }
    file.close();
    return;
  }

  renderCustomSleepScreen();
}

/**
 * @brief Renders a bitmap image as the sleep screen with proper positioning.
 * 
 * Handles image scaling, centering, cropping, and grayscale rendering based
 * on screen dimensions and user settings.
 * 
 * @param bitmap The bitmap image to render
 */
void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap) const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float cropX = 0, cropY = 0;
  int x = 0;
  int y = 0;
  int targetWidth = pageWidth;
  int targetHeight = pageHeight;
  const float imageWidth = static_cast<float>(bitmap.getWidth());
  const float imageHeight = static_cast<float>(bitmap.getHeight());
  const float imageRatio = imageWidth / imageHeight;
  const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);
  const bool cropMode = SETTINGS.sleepScreenCoverMode == SystemSetting::SLEEP_SCREEN_COVER_MODE::CROP;

  if (cropMode) {
    // Fill screen: crop excess from the dominant axis.
    if (imageRatio > screenRatio) {
      cropX = 1.0f - (screenRatio / imageRatio);
    } else if (imageRatio < screenRatio) {
      cropY = 1.0f - (imageRatio / screenRatio);
    }
  } else {
    // Fit inside screen while preserving aspect ratio (no upscale).
    const float scaleW = static_cast<float>(pageWidth) / imageWidth;
    const float scaleH = static_cast<float>(pageHeight) / imageHeight;
    float fitScale = std::min(scaleW, scaleH);
    if (fitScale > 1.0f) {
      fitScale = 1.0f;
    }

    targetWidth = std::max(1, static_cast<int>(std::round(imageWidth * fitScale)));
    targetHeight = std::max(1, static_cast<int>(std::round(imageHeight * fitScale)));
    x = (pageWidth - targetWidth) / 2;
    y = (pageHeight - targetHeight) / 2;
  }

  renderer.clearScreen();

  const bool hasGreyscale = bitmap.hasGreyscale() &&
                            SETTINGS.sleepScreenCoverFilter == SystemSetting::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  renderer.resetBitmapGrayscaleDetection();
  renderer.drawBitmap(bitmap, x, y, targetWidth, targetHeight, cropX, cropY);

  if (SETTINGS.sleepScreenCoverFilter == SystemSetting::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  if (hasGreyscale && renderer.needsBitmapGrayscale()) {
    renderGreyscale(bitmap, x, y, targetWidth, targetHeight, cropX, cropY);
  }
}

/**
 * @brief Renders a bitmap with grayscale processing.
 * 
 * Performs two-pass rendering for grayscale images (LSB and MSB) to achieve
 * proper grayscale display on e-ink screens.
 * 
 * @param bitmap The bitmap image to render
 * @param x X-coordinate for image placement
 * @param y Y-coordinate for image placement
 * @param w Target width for rendering
 * @param h Target height for rendering
 * @param cx Horizontal crop factor (0-1)
 * @param cy Vertical crop factor (0-1)
 */
void SleepActivity::renderGreyscale(const Bitmap& bitmap, int x, int y, int w, int h, float cx, float cy) const {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, w, h, cx, cy);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, w, h, cx, cy);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
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

  renderer.fillRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
  renderer.clearScreen();
  renderer.drawIcon(CorgiSleep, (pageWidth - 256) / 2, (pageHeight - 256) / 2, 256, 256);

  if (SETTINGS.sleepScreen != SystemSetting::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

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