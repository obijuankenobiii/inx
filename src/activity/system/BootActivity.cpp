#include "BootActivity.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>

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
 * @brief Advances the boot sequence to the next initialization stage.
 * 
 * Executes the next step in the boot process based on the current bootStage.
 * Each stage loads a different system component and updates the progress bar.
 * Stages progress from settings loading through app state, recent books,
 * book state, and finally marks boot as complete.
 */
void BootActivity::initializeNextStage() {
  switch (bootStage) {
    case 0:
      bootProgress = 10;
      drawProgressBar();
      SETTINGS.loadFromFile();
      bootStage++;
      break;
      
    case 1:
      bootProgress = 50;
      drawProgressBar();
      APP_STATE.loadFromFile();
      bootStage++;
      break;
      
    case 2:
      bootProgress = 70;
      drawProgressBar();
      RECENT_BOOKS.loadFromFile();
      bootStage++;
      break;
      
    case 3:
      bootProgress = 100;
      drawProgressBar();
      BOOK_STATE.loadFromFile();
      bootComplete = true;
      break;
      
    default:
      break;
  }
}

/**
 * @brief Initializes the boot activity when it becomes active.
 * 
 * Clears the screen, displays the Corgi logo centered on the screen, draws
 * the progress bar outline, and resets all boot state variables including
 * progress, timing, and stage tracking.
 */
void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen(0xFF);
  renderer.drawIcon(CorgiWhite, (pageWidth - 256) / 2, (pageHeight - 256) / 2, 256, 256, GfxRenderer::Rotate270CW);
  
  const int barWidth = 150;
  const int barHeight = 10;
  const int barX = (pageWidth - barWidth) / 2;
  const int barY = (pageHeight - 200) / 2 + 200 + 30;
  
  renderer.drawRect(barX, barY, barWidth, barHeight, true);

  renderer.fillRect(barX + 2, barY + 2, barWidth - 4, barHeight - 4, false);
  
  renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, (renderer.getScreenWidth() / 2) - (renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, INX_VERSION) / 2), renderer.getScreenHeight() - 30, INX_VERSION);
  renderer.displayBuffer();
  
  bootProgress = 0;
  lastBootUpdate = millis();
  bootComplete = false;
  bootStage = 0;
}

/**
 * @brief Main update loop for the boot activity.
 * 
 * Handles the boot sequence timing and transition to the next activity.
 * If boot is complete, determines whether to navigate to the recent books
 * view or directly to the last opened book based on app state and settings.
 * If boot is in progress, advances the boot stage at 300ms intervals.
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

  if (millis() - lastBootUpdate > 300) {
    lastBootUpdate = millis();
    initializeNextStage();
  }
}