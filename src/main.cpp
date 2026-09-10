/**
 * @file main.cpp
 * @brief Firmware entry point, globals, and activity bootstrap.
 */

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <SDCardManager.h>
#include <SPI.h>

#include <cstring>
#include <functional>
#include <new>
#include <string>

#ifdef SIMULATOR
#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/PageWordIndex.h>
#include <Epub/Section.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "activity/reader/Epub/EpubActivity.h"
#endif

#include "activity/OpdsServerListActivity.h"
#include "activity/network/CalibreConnectActivity.h"
#include "activity/network/HotspotActivity.h"
#include "activity/network/LocalNetworkActivity.h"
#include "activity/page/Home.h"
#include "activity/page/HomeDescription.h"
#include "activity/page/HomeSubPage.h"
#include "activity/page/Library.h"
#include "activity/page/LibraryActivity.h"
#include "activity/page/RecentActivity.h"
#include "activity/page/Search.h"
#include "activity/page/Settings.h"
#include "activity/page/StatisticActivity.h"
#include "activity/page/SyncActivity.h"
#include "activity/reader/ImageViewerActivity.h"
#include "activity/reader/ReaderActivity.h"
#include "activity/system/BootActivity.h"
#include "activity/system/SleepActivity.h"
#include "activity/util/FullScreenMessageActivity.h"
#include "state/OpdsServerStore.h"
#include "state/ReaderSetting.h"
#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/StringUtils.h"

#ifdef SIMULATOR
extern HalDisplay display;
extern HalGPIO gpio;
#else
HalDisplay display;
HalGPIO gpio;
#endif
MappedInputManager input(gpio);
GfxRenderer renderer(display);
GfxRenderer& render = renderer;

Activity* currentActivity = nullptr;
bool sdCardAvailable = false;

unsigned long t1 = 0;
unsigned long t2 = 0;

void verifyPowerButtonDuration();
void waitForPowerRelease();
void normalizeUnavailableClockSettings();
void enterDeepSleep();
void onGoToReader(const std::string& path);
void onGoToDescription(const std::string& path);
void openHomeSubPage(HomeSubPage::Section section);
void onSelectBook(const std::string& path);
void onGoToRecent();
void onGoToStatistics();
void onGoToFileTransfer();
void onGoToSettings();
void onGoToLibrary(const std::string& path = "/");
void openSearchFromCallback(std::function<void()> returnToCaller);
void setupDisplayAndFonts();
void onNetworkModeSelected(NetworkMode mode);
void openReaderFromCallback(const std::string& path);
bool handleGlobalPowerRefresh();

/**
 * @brief Switches the current activity using standard heap allocation.
 * * This uses 'new' and 'delete' which allows the ReaderActivity to utilize
 * the full 360KB of available heap rather than being stuck in a small static buffer.
 */
template <typename T, typename... Args>
void switchTo(Args&&... args) {
  if (currentActivity) {
    currentActivity->onExit();
    delete currentActivity;
    currentActivity = nullptr;
  }

  currentActivity = new T(std::forward<Args>(args)...);
#ifdef SIMULATOR
  Serial.printf("[%lu] [SIM] Activity: %s\n", millis(), currentActivity->getName());
#endif
  currentActivity->onEnter();
}

/**
 * @brief Navigates to the reader activity for a specific book.
 */
void onGoToReader(const std::string& path) {
  switchTo<ReaderActivity>(render, input, path, [](const std::string&) { onGoToRecent(); });
}

void onGoToDescription(const std::string& path) {
  switchTo<HomeDescription>(render, input, path, onGoToRecent);
}

void openHomeSubPage(const HomeSubPage::Section section) {
  switchTo<HomeSubPage>(render, input, section, onGoToRecent);
}

bool isExportedNoteImage(const std::string& path) {
  constexpr const char* root = "/Bookmarks & Annotations";
  const size_t rootLen = strlen(root);
  const bool inRoot = path.compare(0, rootLen, root) == 0 && (path.size() == rootLen || path[rootLen] == '/');
  return inRoot && (StringUtils::checkFileExtension(path, ".bmp") || StringUtils::checkFileExtension(path, ".jpg") ||
                    StringUtils::checkFileExtension(path, ".jpeg") || StringUtils::checkFileExtension(path, ".png"));
}

/**
 * @brief Opens the reader activity and returns to the library when closed.
 */
void openReaderFromCallback(const std::string& path) {
  // Defensive copy: `path` is typically a reference into the calling activity's own state (e.g.
  // LibraryActivity's currentPageItems), but switchTo() deletes that activity before this function's
  // arguments are used to construct the new one - passing `path` itself through would dangle.
  const std::string pathCopy = path;
  if (isExportedNoteImage(pathCopy)) {
    switchTo<ImageViewerActivity>(render, input, pathCopy, [pathCopy]() {
      std::string folderPath = pathCopy.substr(0, pathCopy.find_last_of('/'));
      if (folderPath.empty()) folderPath = "/";
      onGoToLibrary(folderPath);
    });
    return;
  }
  switchTo<ReaderActivity>(render, input, pathCopy, [pathCopy](const std::string&) {
    std::string folderPath = pathCopy.substr(0, pathCopy.find_last_of('/'));
    if (folderPath.empty()) folderPath = "/";
    onGoToLibrary(folderPath);
  });
}

/**
 * @brief Callback wrapper for selecting a book to read.
 */
void onSelectBook(const std::string& path) { onGoToReader(path); }

/**
 * @brief Navigates to the statistics activity.
 */
void onGoToStatistics() { switchTo<StatisticActivity>(render, input, onGoToRecent, onGoToFileTransfer); }

/**
 * @brief Navigates to the recent books activity.
 */
void onGoToRecent() {
  switchTo<Home>(render, input);
}

void openSearchFromCallback(std::function<void()> returnToCaller) {
  switchTo<Search>(render, input, std::move(returnToCaller));
}

/**
 * @brief Handles network mode selection and navigates to appropriate activity.
 */
void onNetworkModeSelected(NetworkMode mode) {
  switch (mode) {
    case NetworkMode::JOIN_NETWORK:
      switchTo<LocalNetworkActivity>(render, input, onGoToFileTransfer);
      break;
    case NetworkMode::CONNECT_CALIBRE:
      switchTo<CalibreConnectActivity>(render, input, onGoToFileTransfer);
      break;
    case NetworkMode::CREATE_HOTSPOT:
      switchTo<HotspotActivity>(render, input, onGoToFileTransfer);
      break;
    case NetworkMode::OPDS_BROWSER:
      switchTo<OpdsServerListActivity>(render, input, onGoToFileTransfer);
      break;
  }
}

/**
 * @brief Navigates to the file transfer/sync activity.
 */
void onGoToFileTransfer() {
  switchTo<SyncActivity>(render, input, onNetworkModeSelected, onGoToRecent, onGoToStatistics, onGoToSettings);
}

/**
 * @brief Navigates to the settings activity.
 */
void onGoToSettings() {
  switchTo<Settings>(render, input);
}

/**
 * @brief Navigates to the library activity.
 */
void onGoToLibrary(const std::string& path) {
  switchTo<Library>(render, input, path);
}

/**
 * @brief Set up application.
 */
void verifyPowerButtonDuration() {
  if (SETTINGS.shortPwrBtn == SystemSetting::SHORT_PWRBTN::SLEEP) return;
  const auto start = millis();
  bool abort = false;
  gpio.update();
  while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) {
    delay(10);
    gpio.update();
  }

  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getHeldTime() < SETTINGS.getPowerButtonDuration()) {
      delay(10);
      gpio.update();
    }
    abort = gpio.getHeldTime() < SETTINGS.getPowerButtonDuration();
  } else {
    abort = true;
  }

  if (abort) gpio.startDeepSleep();
}

void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

void normalizeUnavailableClockSettings() {
  if (gpio.deviceIsX3()) {
    return;
  }

  bool changed = false;
  if (SETTINGS.sleepScreen == SystemSetting::DATETIME) {
    SETTINGS.sleepScreen = SystemSetting::LIGHT;
    changed = true;
  }
  if (SETTINGS.sleepClockRefreshInterval != SystemSetting::CLOCK_REFRESH_OFF) {
    SETTINGS.sleepClockRefreshInterval = SystemSetting::CLOCK_REFRESH_OFF;
    changed = true;
  }
  if (changed) {
    SETTINGS.saveToFile();
  }
}

void enterDeepSleep() {
  normalizeUnavailableClockSettings();
  const bool fromReader = currentActivity && currentActivity->isReadingActivity();
  switchTo<SleepActivity>(render, input, fromReader);
  display.deepSleep();
  gpio.startDeepSleep();
}

void setupDisplayAndHintsPolicy() {
  // Honor Settings → "Hide button hints" for every call site (hub, settings, reader
  // dictionary/annotation overlays, side-button chrome, etc.).
  UiRender::setHintsHiddenFn([]() { return SETTINGS.hideButtonHints != 0; });
}

void setupDisplayAndFonts() {
  display.begin();
  render.begin();
  FontManager::initialize(render);
  setupDisplayAndHintsPolicy();
}

bool handleGlobalPowerRefresh() {
  if (!currentActivity || !currentActivity->allowGlobalPowerRefresh()) {
    return false;
  }
  if (SETTINGS.shortPwrBtn != SystemSetting::SHORT_PWRBTN::PAGE_REFRESH) {
    return false;
  }
  if (!input.wasReleased(MappedInputManager::Button::Power)) {
    return false;
  }

  renderer.displayBuffer(HalDisplay::MANUAL_REFRESH);
  return true;
}

/**
 * @brief Set up application.
 */
#ifdef SIMULATOR
/**
 * Headless native repro driver, gated by FOOTNOTE_SELFTEST_BOOK (a path relative to CROSSPOINT_SIM_SD).
 * Walks every spine item of a real book end-to-end (fresh parse -> section cache write -> read back
 * every page), the same sequence EpubActivity::pageTurn()/loadCurrentSection() drives on real "next
 * page/chapter" navigation - so a real crash here, under ASan/UBSan, gives an exact file:line instead
 * of a stripped ESP32 panic dump. Not part of the shipped app; exits the process when done.
 */
void runFootnoteSelftestIfRequested() {
  const char* bookPathC = std::getenv("FOOTNOTE_SELFTEST_BOOK");
  if (!bookPathC) {
    return;
  }
  const std::string bookPath = bookPathC;
  const bool useFootnotes = std::getenv("FOOTNOTE_SELFTEST_USE_FOOTNOTES") != nullptr;

  auto epub = std::make_unique<Epub>(bookPath);
  if (!epub->load(true)) {
    printf("SELFTEST FAILED: epub->load() returned false for %s\n", bookPath.c_str());
    std::exit(2);
  }
  epub->clearCache();
  if (!epub->load(true)) {
    printf("SELFTEST FAILED: epub->load() (post clearCache) returned false\n");
    std::exit(2);
  }

  EpubActivity act(renderer, input, std::move(epub), [] {}, [] {});
  act.currentSpineIndex = 0;
  act.nextPageNumber = 0;

  const int totalSpines = act.epub->getSpineItemsCount();
  printf("SELFTEST: book loaded, spines=%d, driving via EpubActivity (footnotes=%d)\n", totalSpines, useFootnotes);
  fflush(stdout);

  int pagesWalked = 0;
  int footnotesOpened = 0;
  int lastSpine = -1;
  while (act.currentSpineIndex < totalSpines) {
    if (!act.section) {
      act.loadCurrentSection(false);
      if (!act.section) {
        printf("SELFTEST FAILED: loadCurrentSection produced no section, spine=%d\n", act.currentSpineIndex);
        std::exit(3);
      }
    }
    if (act.currentSpineIndex != lastSpine) {
      lastSpine = act.currentSpineIndex;
      printf("SELFTEST: === spine %d/%d pages=%d ===\n", act.currentSpineIndex, totalSpines, act.section->pageCount);
      fflush(stdout);
    }
    ++pagesWalked;

    if (useFootnotes) {
      const ViewportInfo info = act.calculateViewport();
      const int fontId = info.fontId;
      const int headerFontId = FontManager::getNextFont(fontId);
      auto page = act.section->loadPageFromSectionFile();
      if (page) {
        std::vector<PageWordHit> hits;
        buildPageWordIndex(*page, act.renderer, fontId, headerFontId, info.totalMarginLeft, info.totalMarginTop,
                           hits, nullptr, false);
        const bool hasFootnote =
            std::any_of(hits.begin(), hits.end(), [](const PageWordHit& h) { return !h.footnoteTarget.empty(); });
        if (hasFootnote) {
          ++footnotesOpened;
          printf("SELFTEST:   page %d has a footnote marker - opening it (#%d)\n", act.section->currentPage,
                 footnotesOpened);
          fflush(stdout);
          act.footnoteUi_.enter(act);
          if (act.footnoteUi_.isActive() && act.footnoteUi_.debugWordCountForSelftest() > 0) {
            act.footnoteUi_.debugResolveFootnoteBodyForSelftest(act);
          }
          act.footnoteUi_.exit(act);
          printf("SELFTEST:   footnote closed, resuming\n");
          fflush(stdout);
        }
      }
    }

    act.pageTurn(true);
  }

  printf("SELFTEST: ALL DONE - no crash across %d spines, %d pages walked, %d footnotes opened\n", totalSpines,
        pagesWalked, footnotesOpened);
  std::exit(0);
}
#endif

void setup() {
  t1 = millis();
  gpio.begin();
  setupDisplayAndFonts();

#ifdef SIMULATOR
  runFootnoteSelftestIfRequested();
#endif

  if (gpio.isUsbConnected()) {
    Serial.begin(115200);
    unsigned long start = millis();
    while (!Serial && (millis() - start) < 3000) delay(10);
  }

  sdCardAvailable = SdMan.begin();

  if (sdCardAvailable) {
    SETTINGS.loadFromFile();
    READER_SETTINGS.loadFromFile();
    OPDS_STORE.loadOrMigrate({"Default", SETTINGS.opdsServerUrl, SETTINGS.opdsUsername, SETTINGS.opdsPassword});
  }
  normalizeUnavailableClockSettings();

  switch (gpio.getWakeupReason()) {
    case HalGPIO::WakeupReason::PowerButton:
      verifyPowerButtonDuration();
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      gpio.startDeepSleep();
      break;
    default:
      break;
  }

  switchTo<BootActivity>(render, input);
  waitForPowerRelease();
}

/**
 * @brief All activity loop.
 */
void loop() {
  gpio.update();
  static unsigned long lastActivityTime = millis();

  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || (currentActivity && currentActivity->preventAutoSleep())) {
    lastActivityTime = millis();
  }

  if (millis() - lastActivityTime >= SETTINGS.getSleepTimeoutMs()) {
    enterDeepSleep();
    return;
  }

  const bool powerHeld = gpio.isPressed(HalGPIO::BTN_POWER);
  if (powerHeld && gpio.getHeldTime() > SETTINGS.getPowerButtonDuration()) {
    enterDeepSleep();
    return;
  }

  // Ignore other input while power is held so a long-press-to-sleep cannot also
  // open the selected home/library book (GPIO bounce / Confirm during the hold).
  if (powerHeld) {
    delay(10);
    return;
  }

  if (handleGlobalPowerRefresh()) {
    delay(10);
    return;
  }

  if (currentActivity) {
    currentActivity->loop();
  }

  if (currentActivity && currentActivity->skipLoopDelay()) {
    yield();
  } else {
    delay(10);
  }
}
