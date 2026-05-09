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
#include "util/StringUtils.h"
#include <cmath>
#include <cstdio>
#include <memory>

namespace {
bool isSleepImagePathJpeg(const std::string& path) {
  return StringUtils::checkFileExtension(path, ".jpg") || StringUtils::checkFileExtension(path, ".jpeg");
}
bool isSupportedSleepImageFile(const std::string& filename) {
  return StringUtils::checkFileExtension(filename, ".bmp") || StringUtils::checkFileExtension(filename, ".jpg") ||
         StringUtils::checkFileExtension(filename, ".jpeg");
}

constexpr uint32_t kSleepCacheMagic = 0x43504c53;  // SLPC
constexpr uint16_t kSleepCacheVersion = 1;
constexpr const char* kSleepCacheDir = "/.display-cache/sleep";

struct SleepCacheHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t headerSize;
  uint16_t width;
  uint16_t height;
  uint16_t rowBytes;
  uint16_t reserved;
};

uint32_t fnv1aAdd(uint32_t hash, const uint8_t byte) {
  hash ^= byte;
  return hash * 16777619u;
}

uint32_t fnv1aAddUint32(uint32_t hash, const uint32_t value) {
  hash = fnv1aAdd(hash, static_cast<uint8_t>(value & 0xFF));
  hash = fnv1aAdd(hash, static_cast<uint8_t>((value >> 8) & 0xFF));
  hash = fnv1aAdd(hash, static_cast<uint8_t>((value >> 16) & 0xFF));
  return fnv1aAdd(hash, static_cast<uint8_t>((value >> 24) & 0xFF));
}

uint32_t fnv1aAddString(uint32_t hash, const std::string& value) {
  for (const char c : value) {
    hash = fnv1aAdd(hash, static_cast<uint8_t>(c));
  }
  return hash;
}

uint32_t fileSizeForCache(const std::string& path) {
  if (path.empty() || !SdMan.exists(path.c_str())) {
    return 0;
  }
  FsFile file = SdMan.open(path.c_str());
  const uint32_t size = file ? static_cast<uint32_t>(file.size()) : 0;
  file.close();
  return size;
}

bool isDirectory(const std::string& path) {
  if (!SdMan.exists(path.c_str())) {
    return false;
  }
  FsFile file = SdMan.open(path.c_str());
  const bool ok = file && file.isDirectory();
  file.close();
  return ok;
}

bool ensureSleepCacheDir() {
  if (!isDirectory("/.display-cache") && !SdMan.mkdir("/.display-cache")) {
    return false;
  }
  return isDirectory(kSleepCacheDir) || SdMan.mkdir(kSleepCacheDir);
}

std::string sleepCachePath(GfxRenderer& renderer, const std::string& key) {
  uint32_t hash = 2166136261u;
  hash = fnv1aAddString(hash, key);
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(renderer.getScreenWidth()));
  hash = fnv1aAddUint32(hash, static_cast<uint32_t>(renderer.getScreenHeight()));
  hash = fnv1aAdd(hash, static_cast<uint8_t>(renderer.getOrientation()));
  char name[36];
  snprintf(name, sizeof(name), "/%08lx.irdc", static_cast<unsigned long>(hash));
  return std::string(kSleepCacheDir) + name;
}

std::string sleepRenderKey(const char* mode, const std::string& path = "", const std::string& extra = "") {
  std::string key = "v2|";
  key += mode;
  key += "|screen=" + std::to_string(SETTINGS.sleepScreen);
  key += "|coverMode=" + std::to_string(SETTINGS.sleepScreenCoverMode);
  key += "|filter=" + std::to_string(SETTINGS.sleepScreenCoverFilter);
  key += "|sleep2bit=" + std::to_string(SETTINGS.sleepScreenCoverGrayscale);
  key += "|path=" + path;
  key += "|size=" + std::to_string(fileSizeForCache(path));
  key += "|extra=" + extra;
  return key;
}

bool renderCachedSleepFrame(GfxRenderer& renderer, const std::string& key,
                            const HalDisplay::RefreshMode refreshMode = HalDisplay::HALF_REFRESH) {
  const std::string path = sleepCachePath(renderer, key);
  if (!SdMan.exists(path.c_str())) {
    return false;
  }
  FsFile file;
  if (!SdMan.openFileForRead("SLPC", path, file)) {
    return false;
  }

  SleepCacheHeader header;
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int rowBytes = (screenW + 7) / 8;
  const bool okHeader = file.read(&header, sizeof(header)) == sizeof(header) && header.magic == kSleepCacheMagic &&
                        header.version == kSleepCacheVersion && header.headerSize == sizeof(SleepCacheHeader) &&
                        header.width == screenW && header.height == screenH && header.rowBytes == rowBytes;
  if (!okHeader || rowBytes > 128) {
    file.close();
    return false;
  }

  uint8_t row[128];
  for (int y = 0; y < screenH; y++) {
    if (file.read(row, rowBytes) != rowBytes) {
      file.close();
      return false;
    }
    renderer.drawPackedRow1bpp(0, y, screenW, row);
  }
  file.close();
  renderer.displayBuffer(refreshMode);
  return true;
}

bool storeSleepFrame(GfxRenderer& renderer, const std::string& key) {
  if (renderer.getRenderMode() != GfxRenderer::BW || !ensureSleepCacheDir()) {
    return false;
  }
  const std::string path = sleepCachePath(renderer, key);
  FsFile file;
  if (!SdMan.openFileForWrite("SLPC", path, file)) {
    return false;
  }

  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  const int rowBytes = (screenW + 7) / 8;
  if (rowBytes > 128) {
    file.close();
    SdMan.remove(path.c_str());
    return false;
  }

  const SleepCacheHeader header = {.magic = kSleepCacheMagic,
                                   .version = kSleepCacheVersion,
                                   .headerSize = sizeof(SleepCacheHeader),
                                   .width = static_cast<uint16_t>(screenW),
                                   .height = static_cast<uint16_t>(screenH),
                                   .rowBytes = static_cast<uint16_t>(rowBytes),
                                   .reserved = 0};
  bool ok = file.write(&header, sizeof(header)) == sizeof(header);
  uint8_t row[128];
  for (int y = 0; ok && y < screenH; y++) {
    renderer.readPackedRow1bpp(0, y, screenW, row);
    ok = file.write(row, rowBytes) == static_cast<size_t>(rowBytes);
  }
  file.close();
  if (!ok) {
    SdMan.remove(path.c_str());
  }
  return ok;
}

bool sleepCanUseOneBitFrameCache() {
  return SETTINGS.sleepScreenCoverGrayscale == 0 ||
         SETTINGS.sleepScreenCoverFilter != SystemSetting::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
}

bool sleepTwoBitEnabled() {
  return SETTINGS.sleepScreenCoverGrayscale != 0 &&
         SETTINGS.sleepScreenCoverFilter == SystemSetting::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
}

ImageRenderMode sleepImageRenderMode() {
  return sleepTwoBitEnabled() ? ImageRenderMode::TwoBit : ImageRenderMode::OneBit;
}

void runSleepImageTwoBitPasses(GfxRenderer& renderer, const std::string& imagePath,
                               const ImageRender::Options& baseOptions) {
  if (!sleepTwoBitEnabled()) {
    return;
  }

  ImageRender::Options options = baseOptions;
  options.mode = ImageRenderMode::TwoBit;
  options.useDisplayCache = false;

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  ImageRender::create(renderer, imagePath).render(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(), options);
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  ImageRender::create(renderer, imagePath).render(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(), options);
  renderer.copyGrayscaleMsbBuffers();

  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
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

  renderer.clearScreen(0xFF);
  const int fontId = bookSettings.getReaderFontId();
  const int headerFontId = FontManager::getNextFont(fontId);
  const ImageRenderMode imageMode =
      SETTINGS.readerImageGrayscale != 0 && page->hasImages() ? ImageRenderMode::TwoBit : ImageRenderMode::OneBit;
  page->render(renderer, fontId, headerFontId, vp.totalMarginLeft, vp.totalMarginTop, true, imageMode);
  page->renderImages(renderer, fontId, vp.totalMarginLeft, vp.totalMarginTop, imageMode);

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
 * Uses a fixed image from settings when set; otherwise picks randomly from /sleep/
 * and SD-root sleep.bmp/jpg/jpeg. Falls back to default sleep screen if no images are found.
 */
void SleepActivity::renderCustomSleepScreen() const {
  const std::string imagePath = pickSleepBmpPath();
  if (!imagePath.empty()) {
    const std::string cacheKey = sleepRenderKey("custom", imagePath);
    recordSleepImageUsed();
    if (sleepCanUseOneBitFrameCache() && renderCachedSleepFrame(renderer, cacheKey)) {
      return;
    }

    if (isSleepImagePathJpeg(imagePath)) {
      renderer.clearScreen();
      ImageRender::Options options;
      options.cropToFill = SETTINGS.sleepScreenCoverMode == SystemSetting::SLEEP_SCREEN_COVER_MODE::FIT;
      options.mode = sleepImageRenderMode();
      if (ImageRender::create(renderer, imagePath)
              .render(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(), options)) {
        if (sleepCanUseOneBitFrameCache()) {
          storeSleepFrame(renderer, cacheKey);
        }
        renderer.displayBuffer();
        runSleepImageTwoBitPasses(renderer, imagePath, options);
        return;
      }
    }
    FsFile file;
    if (SdMan.openFileForRead("SLP", imagePath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        if (SETTINGS.sleepScreenCoverMode == SystemSetting::SLEEP_SCREEN_COVER_MODE::FIT) {
          renderFill(bitmap, sleepCanUseOneBitFrameCache() ? cacheKey : "");
        } else {
          renderBitmapSleepScreen(bitmap, false, sleepCanUseOneBitFrameCache() ? cacheKey : "");
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
    if (isSleepImagePathJpeg(imagePath)) {
      ImageRender::Options options;
      options.cropToFill = SETTINGS.sleepScreenCoverMode == SystemSetting::SLEEP_SCREEN_COVER_MODE::FIT;
      options.mode = sleepImageRenderMode();
      if (ImageRender::create(renderer, imagePath)
              .render(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(), options)) {
        renderer.displayBuffer();
        runSleepImageTwoBitPasses(renderer, imagePath, options);
        return;
      }
    }
    FsFile file;
    if (SdMan.openFileForRead("SLP", imagePath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
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
  const std::string cacheKey = sleepRenderKey("cover", coverPath, APP_STATE.lastRead);
  if (!coverPath.empty() && sleepCanUseOneBitFrameCache() && renderCachedSleepFrame(renderer, cacheKey)) {
    return;
  }

  if (!coverPath.empty() && isSleepImagePathJpeg(coverPath)) {
    renderer.clearScreen();
    ImageRender::Options options;
    options.cropToFill = SETTINGS.sleepScreenCoverMode == SystemSetting::SLEEP_SCREEN_COVER_MODE::FIT;
    options.mode = sleepImageRenderMode();
    if (ImageRender::create(renderer, coverPath)
            .render(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(), options)) {
      if (SETTINGS.sleepScreenCoverFilter == SystemSetting::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
        renderer.invertScreen();
      }
      if (sleepCanUseOneBitFrameCache()) {
        storeSleepFrame(renderer, cacheKey);
      }
      renderer.displayBuffer();
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
        renderFill(bitmap, sleepCanUseOneBitFrameCache() ? cacheKey : "");
      } else {
        renderBitmapSleepScreen(bitmap, false, sleepCanUseOneBitFrameCache() ? cacheKey : "");
      }
    }
    file.close();
    return;
  }

  renderCustomSleepScreen();
}

void SleepActivity::renderFill(const Bitmap& bitmap, const std::string& cacheKey) const {
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

  if (!cacheKey.empty()) {
    storeSleepFrame(renderer, cacheKey);
  }
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  if (hasTwoBit) {
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

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap, const bool preCroppedEpubCover,
                                            const std::string& cacheKey) const {
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

  if (!cacheKey.empty()) {
    storeSleepFrame(renderer, cacheKey);
  }
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);

  if (hasTwoBit) {
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
  const std::string cacheKey = sleepRenderKey("default", "", std::to_string(SETTINGS.sleepScreen));
  if (renderCachedSleepFrame(renderer, cacheKey)) {
    return;
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.rectangle.fill(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());
  renderer.clearScreen();
  renderer.bitmap.icon(CorgiSleep, (pageWidth - 256) / 2, (pageHeight - 256) / 2, 256, 256);

  if (SETTINGS.sleepScreen != SystemSetting::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  storeSleepFrame(renderer, cacheKey);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

/**
 * @brief Renders a completely blank sleep screen.
 * 
 * Clears the screen to save power and prevent screen burn-in during sleep.
 */
void SleepActivity::renderBlankSleepScreen() const {
  const std::string cacheKey = sleepRenderKey("blank");
  if (renderCachedSleepFrame(renderer, cacheKey)) {
    return;
  }
  renderer.clearScreen();
  storeSleepFrame(renderer, cacheKey);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
