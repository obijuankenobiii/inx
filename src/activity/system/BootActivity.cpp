/**
 * @file BootActivity.cpp
 * @brief Definitions for BootActivity.
 */

#include "BootActivity.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>

#include "system/FontManager.h"

#include "state/SystemSetting.h"
#include "state/Session.h"
#include "state/RecentBooks.h"
#include "state/BookState.h"

#include "KOReaderCredentialStore.h"
#include "system/Fonts.h"
#include "images/CorgiWhite.h"

extern void onGoToRecent();
extern void onGoToReader(const std::string&);
extern HalDisplay display;
extern HalGPIO gpio;
extern MappedInputManager mappedInputManager;
extern GfxRenderer renderer;
extern Activity* currentActivity;

BootActivity::BootActivity(GfxRenderer& renderer, MappedInputManager& inputManager)
    : Activity("BootActivity", renderer, inputManager) {}

/**
 * @brief Draws the boot progress bar on the screen.
 *
 * Renders a horizontal progress bar centered on the screen, with the fill width
 * determined by the current bootProgress value (0-100). The bar is drawn with
 * a border and filled portion based on completion percentage.
 */
void BootActivity::drawProgressBar() {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const int barWidth = 150;
  const int barHeight = 10;
  const int barX = (pageWidth - barWidth) / 2;
  const int barY = (pageHeight - 200) / 2 + 200 + 30;

  renderer.fillRect(barX + 2, barY + 2, barWidth - 4, barHeight - 4, false);

  int fillWidth = barWidth * bootProgress / 100;
  if (fillWidth > 4) {
    renderer.fillRect(barX + 2, barY + 2, fillWidth - 4, barHeight - 4, true);
  }

  renderer.displayBuffer();
}

/**
 * @brief Initializes the boot activity when it becomes active.
 *
 * Clears the screen, displays the Corgi logo centered on the screen, draws
 * the progress bar outline, loads persisted state (including KOReader sync
 * settings when present), then marks boot complete. SD reader fonts are not
 * scanned here; that runs when opening a book.
 */
void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen(0xFF);
  renderer.drawIcon(CorgiWhite, (pageWidth - 256) / 2, (pageHeight - 256) / 2, 256, 256);

  const int barWidth = 150;
  const int barHeight = 10;
  const int barX = (pageWidth - barWidth) / 2;
  const int barY = (pageHeight - 200) / 2 + 200 + 30;

  renderer.drawRect(barX, barY, barWidth, barHeight, true);

  renderer.fillRect(barX + 2, barY + 2, barWidth - 4, barHeight - 4, false);

  renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, (renderer.getScreenWidth() / 2) - (renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, INX_VERSION) / 2), renderer.getScreenHeight() - 30, INX_VERSION);
  renderer.displayBuffer();

  bootProgress = 0;
  bootComplete = false;

  SETTINGS.loadFromFile();
  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();
  BOOK_STATE.loadFromFile();

  if (SdMan.ready() && SdMan.exists(KOReaderCredentialStore::SYSTEM_SETTINGS_PATH)) {
    (void)KOREADER_STORE.loadFromFile();
  }

  bootProgress = 100;
  drawProgressBar();

  FontManager::scanSDFonts("/fonts");
  FontManager::printFontStats();
  bootComplete = true;
}

/**
 * @brief Main update loop for the boot activity.
 *
 * When boot is complete, navigates to the recent list or resumes the last book
 * per settings. SD font metadata is not loaded until the reader opens a book.
 */
void BootActivity::loop() {
  if (bootComplete) {
    if (APP_STATE.lastRead.empty() || SETTINGS.bootSetting == SystemSetting::HOME_PAGE) {
      onGoToRecent();
    } else {
      const auto path = APP_STATE.lastRead;
      APP_STATE.lastRead = "";
      APP_STATE.saveToFile();
      onGoToReader(path);
    }
    return;
  }
}
