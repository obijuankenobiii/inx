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
#include <HalDisplay.h>
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
#include "state/ImageBitmapGrayMaps.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/ScreenComponents.h"
#include "util/StringUtils.h"
#include <cmath>
#include <memory>

namespace {

std::string pathForFixedSleepBmp() {
  if (SETTINGS.sleepCustomBmp[0] == '\0') {
    return "";
  }
  if (strcmp(SETTINGS.sleepCustomBmp, "/sleep.bmp") == 0) {
    if (SdMan.exists("/sleep.bmp")) {
      return "/sleep.bmp";
    }
    return "";
  }
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
      if (filename[0] != '.' && StringUtils::checkFileExtension(filename, ".bmp")) {
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
  return "";
}

void applyBookOrientationToRenderer(GfxRenderer& r, const uint8_t orientationByte) {
  using O = SystemSetting::ORIENTATION;
  switch (orientationByte) {
    case O::PORTRAIT:
      r.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case O::LANDSCAPE_CW:
      r.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case O::INVERTED:
      r.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case O::LANDSCAPE_CCW:
      r.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }
}

void applyLastReadBookOrientationToRenderer(GfxRenderer& r, const std::string& lastReadPath) {
  if (lastReadPath.empty()) {
    applyBookOrientationToRenderer(r, SETTINGS.orientation);
    return;
  }
  if (StringUtils::checkFileExtension(lastReadPath, ".epub")) {
    Epub book(lastReadPath, "/.metadata/epub");
    if (book.load()) {
      BookSettings bs;
      bs.loadFromFile(book.getCachePath());
      applyBookOrientationToRenderer(r, bs.orientation);
      return;
    }
  }
  applyBookOrientationToRenderer(r, SETTINGS.orientation);
}

std::string resolveLastReadCoverPathForSleep(const std::string& path) {
  std::string coverPath;

  if (StringUtils::checkFileExtension(path, ".epub")) {
    Epub book(path, "/.metadata/epub");
    if (book.load()) {
      const bool cropped = false;
       coverPath = book.getCoverBmpPath(cropped);
      if (!SdMan.exists(coverPath.c_str())) {
        book.generateCoverBmp(cropped);
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
      const std::string thumbPath = book.getThumbBmpPath();
      if (!SdMan.exists(thumbPath.c_str())) {
        book.generateThumbBmp();
      }
      if (SdMan.exists(thumbPath.c_str())) {
        coverPath = thumbPath;
      } else if (book.generateCoverBmp()) {
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

struct SleepReaderViewport {
  int totalMarginTop = 0;
  int totalMarginBottom = 0;
  int totalMarginLeft = 0;
  int totalMarginRight = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  int fontId = 0;
  float lineCompression = 1.f;
};

SleepReaderViewport computeSleepReaderViewport(GfxRenderer& gfx, const BookSettings& bookSettings) {
  SleepReaderViewport info;
  int oT = 0;
  int oR = 0;
  int oB = 0;
  int oL = 0;
  gfx.getOrientedViewableTRBL(&oT, &oR, &oB, &oL);

  constexpr int statusBarMargin = 19;
  constexpr int progressBarMarginTop = 10;

  info.totalMarginTop = oT + bookSettings.screenMargin;
  info.totalMarginBottom = oB + bookSettings.screenMargin;
  info.totalMarginLeft = oL + bookSettings.screenMargin;
  info.totalMarginRight = oR + bookSettings.screenMargin;

  const bool hasStatusBar = (bookSettings.statusBarLeft.item != StatusBarItem::NONE ||
                             bookSettings.statusBarMiddle.item != StatusBarItem::NONE ||
                             bookSettings.statusBarRight.item != StatusBarItem::NONE);

  const bool showProgressBar = (bookSettings.statusBarMiddle.item == StatusBarItem::PROGRESS_BAR ||
                                bookSettings.statusBarMiddle.item == StatusBarItem::PROGRESS_BAR_WITH_PERCENT);

  if (hasStatusBar) {
    info.totalMarginBottom +=
        statusBarMargin - bookSettings.screenMargin +
        (showProgressBar ? (ScreenComponents::BOOK_PROGRESS_BAR_HEIGHT + progressBarMarginTop) : 0);
  }

  int w = gfx.getScreenWidth() - info.totalMarginLeft - info.totalMarginRight;
  int h = gfx.getScreenHeight() - info.totalMarginTop - info.totalMarginBottom;
  constexpr int kMinViewport = 8;
  if (w < kMinViewport) {
    w = kMinViewport;
  }
  if (h < kMinViewport) {
    h = kMinViewport;
  }
  info.width = static_cast<uint16_t>(w);
  info.height = static_cast<uint16_t>(h);
  info.fontId = bookSettings.getReaderFontId();
  info.lineCompression = bookSettings.getReaderLineCompression();
  return info;
}

/**
 * Renders the last-saved EPUB reading page into the framebuffer (no displayBuffer).
 * Used for transparent sleep so the overlay sits on the real page, not the cover.
 */
bool tryRenderEpubLastReadReadingPage(GfxRenderer& renderer, const std::string& epubPath) {
  std::unique_ptr<Epub> epub(new Epub(epubPath, "/.metadata/epub"));
  if (!epub->load()) {
    return false;
  }
  std::shared_ptr<Epub> epubShared(epub.get(), [](Epub*) {});

  BookSettings bookSettings;
  bookSettings.loadFromFile(epub->getCachePath());
  if (!bookSettings.useCustomSettings) {
    bookSettings.orientation = SETTINGS.orientation;
    bookSettings.paragraphCssIndentEnabled = SETTINGS.paragraphCssIndentEnabled;
  }
  applyBookOrientationToRenderer(renderer, bookSettings.orientation);

  const int spines = epub->getSpineItemsCount();
  if (spines <= 0) {
    return false;
  }

  BookProgress bp(epub->getCachePath());
  BookProgress::Data prog{};
  int spineIndex = epub->getSpineIndexForInitialOpen();
  int pageNum = 0;
  if (bp.load(prog) && bp.validate(prog, spines)) {
    bp.sanitize(prog, spines);
    spineIndex = static_cast<int>(prog.spineIndex);
    pageNum = static_cast<int>(prog.pageNumber);
  }

  const SleepReaderViewport vp = computeSleepReaderViewport(renderer, bookSettings);
  FontManager::ensureReaderLayoutFonts(vp.fontId, renderer);

  Section sec(epubShared, spineIndex, renderer);
  bool loaded = sec.loadSectionFile(vp.fontId, vp.lineCompression, bookSettings.extraParagraphSpacing,
                                    bookSettings.paragraphAlignment, vp.width, vp.height, bookSettings.hyphenationEnabled,
                                    bookSettings.paragraphCssIndentEnabled != 0);
  if (!loaded) {
    esp_task_wdt_reset();
    if (!sec.createSectionFile(vp.fontId, FontManager::getNextFont(vp.fontId), FontManager::getMaxFontId(vp.fontId),
                               vp.lineCompression, bookSettings.extraParagraphSpacing, bookSettings.paragraphAlignment,
                               vp.width, vp.height, bookSettings.hyphenationEnabled,
                               bookSettings.paragraphCssIndentEnabled != 0, nullptr, false)) {
      return false;
    }
    loaded = sec.loadSectionFile(vp.fontId, vp.lineCompression, bookSettings.extraParagraphSpacing,
                                 bookSettings.paragraphAlignment, vp.width, vp.height, bookSettings.hyphenationEnabled,
                                 bookSettings.paragraphCssIndentEnabled != 0);
    if (!loaded) {
      return false;
    }
  }

  if (sec.pageCount == 0) {
    return false;
  }
  if (pageNum < 0 || pageNum >= static_cast<int>(sec.pageCount)) {
    pageNum = 0;
  }
  sec.currentPage = pageNum;
  std::unique_ptr<Page> page = sec.loadPageFromSectionFile();
  if (!page) {
    return false;
  }

  BitmapGrayStyleScope bitmapStyle(renderer, readerImageBitmapGrayStyle());
  renderer.clearScreen(0xFF);
  const int fontId = bookSettings.getReaderFontId();
  const int headerFontId = FontManager::getNextFont(fontId);
    const BitmapDitherMode imageDither = bitmapDitherModeFromSetting(SETTINGS.readerImageDither);
  page->render(renderer, fontId, headerFontId, vp.totalMarginLeft, vp.totalMarginTop, true, imageDither);
  page->renderImages(renderer, fontId, vp.totalMarginLeft, vp.totalMarginTop, imageDither);

  StatusBar statusBar(renderer, *epub, bookSettings);
  statusBar.render(&sec, spineIndex, vp.totalMarginRight, vp.totalMarginBottom, vp.totalMarginLeft);
  return true;
}
}  

/**
 * @brief Initializes and renders the sleep screen when activity becomes active.
 * 
 * Selects and renders the appropriate sleep screen based on the current
 * sleep screen mode setting (transparent, blank, custom, cover, or default).
 */
void SleepActivity::onEnter() {
  Activity::onEnter();

  const GfxRenderer::Orientation orientationBeforeSleep = renderer.getOrientation();

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

  renderer.setOrientation(orientationBeforeSleep);
}

/**
 * @brief Renders a custom sleep screen from user-provided images.
 * 
 * Uses a fixed BMP from settings when set; otherwise picks randomly from /sleep/
 * and /sleep.bmp. Falls back to default sleep screen if no images are found.
 */
void SleepActivity::renderCustomSleepScreen() const {
  const std::string imagePath = pickSleepBmpPath();
  if (!imagePath.empty()) {
    FsFile file;
    if (SdMan.openFileForRead("SLP", imagePath, file)) {
      Bitmap bitmap(file, BitmapDitherMode::Atkinson);
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
  const std::string imagePath = pickSleepBmpPath();
  if (!imagePath.empty()) {
    FsFile file;
    if (SdMan.openFileForRead("SLP", imagePath, file)) {
      Bitmap bitmap(file);
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

  const std::string coverPath = resolveLastReadCoverPathForSleep(APP_STATE.lastRead);

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

  const bool hasGreyscale = bitmap.hasGreyscale() &&
                            SETTINGS.sleepScreenCoverFilter == SystemSetting::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  renderer.drawSleepScreen(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (SETTINGS.sleepScreenCoverFilter == SystemSetting::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  if (hasGreyscale) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawSleepScreen(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawSleepScreen(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
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