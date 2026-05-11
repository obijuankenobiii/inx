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
 * @brief Initializes the boot activity when it becomes active.
 *
 * Clears the screen, displays the Corgi logo centered on the screen, draws
 * the progress bar outline, loads persisted state (including KOReader sync
 * settings when present), then marks boot complete. SD reader fonts are not
 * scanned here; that runs when opening a book.
 */
void BootActivity::onEnter() {
  Activity::onEnter();

  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();
  BOOK_STATE.loadFromFile();

  if (SdMan.ready() && SdMan.exists(KOReaderCredentialStore::SYSTEM_SETTINGS_PATH)) {
    (void)KOREADER_STORE.loadFromFile();
  }


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
