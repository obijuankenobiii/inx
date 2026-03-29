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

/**
 * @brief Initializes and renders the sleep screen when activity becomes active.
 * 
 * Selects and renders the appropriate sleep screen based on the current
 * sleep screen mode setting (transparent, blank, custom, cover, or default).
 */
void SleepActivity::onEnter() {
  Activity::onEnter();
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
  std::vector<std::string> files;
  auto dir = SdMan.open("/sleep");

  if (dir && dir.isDirectory()) {
    char name[256];
    while (auto file = dir.openNextFile()) {
      file.getName(name, sizeof(name));
      std::string filename = name;

      if (filename[0] != '.' && StringUtils::checkFileExtension(filename, ".bmp")) {
        files.push_back(filename);
      }
      file.close();
    }
    dir.close();
  }

  if (!files.empty()) {
    size_t idx = random(files.size());
    if (files.size() > 1 && idx == APP_STATE.lastSleepImage) {
      idx = (idx + 1) % files.size();
    }

    APP_STATE.lastSleepImage = idx;
    APP_STATE.saveToFile();

    FsFile file;
    if (SdMan.openFileForRead("SLP", "/sleep/" + files[idx], file)) {
      Bitmap bitmap(file, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderBitmapSleepScreen(bitmap);
        return;
      }
    }
  }

  FsFile rootFile;
  if (SdMan.openFileForRead("SLP", "/sleep.bmp", rootFile)) {
    Bitmap bitmap(rootFile, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      renderBitmapSleepScreen(bitmap);
      return;
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
  std::vector<std::string> files;
  auto dir = SdMan.open("/sleep");

  if (dir && dir.isDirectory()) {
    char name[256];
    while (auto file = dir.openNextFile()) {
      file.getName(name, sizeof(name));
      std::string filename = name;

      if (filename[0] != '.' && StringUtils::checkFileExtension(filename, ".bmp")) {
        files.push_back(filename);
      }
      file.close();
    }
    dir.close();
  }

  if (!files.empty()) {
    size_t idx = random(files.size());
    if (files.size() > 1 && idx == APP_STATE.lastSleepImage) {
      idx = (idx + 1) % files.size();
    }

    APP_STATE.lastSleepImage = idx;
    APP_STATE.saveToFile();

    FsFile file;
    if (SdMan.openFileForRead("SLP", "/sleep/" + files[idx], file)) {
      Bitmap bitmap(file, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        renderer.drawTransparentImage(bitmap, 0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(),
                                      1);
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        return;
      }
    }
  }

  FsFile rootFile;
  if (SdMan.openFileForRead("SLP", "/sleep.bmp", rootFile)) {
    Bitmap bitmap(rootFile, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      renderer.clearScreen();
      renderer.drawTransparentImage(bitmap, 0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(),
                                    1);
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      return;
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
    return renderDefaultSleepScreen();
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
      return renderBitmapSleepScreen(bitmap);
    }
  }

  renderDefaultSleepScreen();
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
  int x, y;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float cropX = 0, cropY = 0;

  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    if (ratio > screenRatio) {
      if (SETTINGS.sleepScreenCoverMode == SystemSetting::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropX = 1.0f - (screenRatio / ratio);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      if (SETTINGS.sleepScreenCoverMode == SystemSetting::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropY = 1.0f - (ratio / screenRatio);
        ratio = static_cast<float>(bitmap.getWidth()) / ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
    }
  } else {
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  renderer.clearScreen();

  const bool hasGreyscale = bitmap.hasGreyscale() &&
                            SETTINGS.sleepScreenCoverFilter == SystemSetting::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (SETTINGS.sleepScreenCoverFilter == SystemSetting::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  if (hasGreyscale) {
    renderGreyscale(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
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
  auto pass = [&](GfxRenderer::RenderMode mode) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(mode);
    renderer.drawBitmap(bitmap, x, y, w, h, cx, cy);
    if (mode == GfxRenderer::GRAYSCALE_LSB)
      renderer.copyGrayscaleLsbBuffers();
    else
      renderer.copyGrayscaleMsbBuffers();
  };

  pass(GfxRenderer::GRAYSCALE_LSB);
  pass(GfxRenderer::GRAYSCALE_MSB);
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
  renderer.drawIcon(CorgiSleep, (pageWidth - 256) / 2, (pageHeight - 256) / 2, 256, 256, GfxRenderer::Rotate270CW);

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