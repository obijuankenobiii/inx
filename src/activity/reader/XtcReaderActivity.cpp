/**
 * @file XtcReaderActivity.cpp
 * @brief Definitions for XtcReaderActivity.
 */

/**
 * XtcReaderActivity.cpp
 *
 * XTC ebook reader activity implementation
 * Displays pre-rendered XTC pages on e-ink display
 */

#include "XtcReaderActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include "state/SystemSetting.h"
#include "state/Session.h"
#include "system/MappedInputManager.h"
#include "state/RecentBooks.h"
#include "XtcReaderChapterSelectionActivity.h"
#include "system/Fonts.h"

namespace {
constexpr unsigned long skipPageMs = 700;
constexpr unsigned long goHomeMs = 1000;
}  

void XtcReaderActivity::taskTrampoline(void* param) {
  auto* self = static_cast<XtcReaderActivity*>(param);
  self->displayTaskLoop();
}

void XtcReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (!xtc) {
    return;
  }

  renderingMutex = xSemaphoreCreateMutex();

  xtc->setupCacheDir();

  
  loadProgress();

  
  APP_STATE.lastRead = xtc->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(xtc->getPath(),xtc->getCachePath(), xtc->getTitle(), xtc->getAuthor());

  
  updateRequired = true;

  xTaskCreate(&XtcReaderActivity::taskTrampoline, "XtcReaderActivityTask",
              4096,               
              this,               
              1,                  
              &displayTaskHandle  
  );
}

void XtcReaderActivity::onExit() {
  ActivityWithSubactivity::onExit();

  
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
  xtc.reset();
}

void XtcReaderActivity::loop() {
  
  if (subActivity) {
    subActivity->loop();
    return;
  }

  
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (xtc && xtc->hasChapters() && !xtc->getChapters().empty()) {
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      exitActivity();
      enterNewActivity(new XtcReaderChapterSelectionActivity(
          this->renderer, this->mappedInput, xtc, currentPage,
          [this] {
            exitActivity();
            updateRequired = true;
          },
          [this](const uint32_t newPage) {
            currentPage = newPage;
            exitActivity();
            updateRequired = true;
          }));
      xSemaphoreGive(renderingMutex);
    }
  }

  
  if (mappedInput.isPressed(MappedInputManager::Button::Back) && mappedInput.getHeldTime() >= goHomeMs) {
    onGoToRecent();
    return;
  }

  
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && mappedInput.getHeldTime() < goHomeMs) {
    onGoBack();
    return;
  }

  
  const bool usePressForPageTurn = !SETTINGS.longPressChapterSkip;
  const bool prevTriggered = usePressForPageTurn ? (mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
                                                    mappedInput.wasPressed(MappedInputManager::Button::Left))
                                                 : (mappedInput.wasReleased(MappedInputManager::Button::PageBack) ||
                                                    mappedInput.wasReleased(MappedInputManager::Button::Left));
  const bool powerPageTurn = SETTINGS.readerShortPwrBtn == SystemSetting::READER_SHORT_PWRBTN::READER_PAGE_TURN &&
                             mappedInput.wasReleased(MappedInputManager::Button::Power);
  const bool nextTriggered = usePressForPageTurn
                                 ? (mappedInput.wasPressed(MappedInputManager::Button::PageForward) || powerPageTurn ||
                                    mappedInput.wasPressed(MappedInputManager::Button::Right))
                                 : (mappedInput.wasReleased(MappedInputManager::Button::PageForward) || powerPageTurn ||
                                    mappedInput.wasReleased(MappedInputManager::Button::Right));

  if (!prevTriggered && !nextTriggered) {
    return;
  }

  
  if (currentPage >= xtc->getPageCount()) {
    currentPage = xtc->getPageCount() - 1;
    updateRequired = true;
    return;
  }

  const bool skipPages = SETTINGS.longPressChapterSkip && mappedInput.getHeldTime() > skipPageMs;
  const int skipAmount = skipPages ? 10 : 1;

  if (prevTriggered) {
    if (currentPage >= static_cast<uint32_t>(skipAmount)) {
      currentPage -= skipAmount;
    } else {
      currentPage = 0;
    }
    updateRequired = true;
  } else if (nextTriggered) {
    currentPage += skipAmount;
    if (currentPage >= xtc->getPageCount()) {
      currentPage = xtc->getPageCount();  
    }
    updateRequired = true;
  }
}

void XtcReaderActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      renderScreen();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void XtcReaderActivity::renderScreen() {
  if (!xtc) {
    return;
  }

  
  if (currentPage >= xtc->getPageCount()) {
    
    renderer.clearScreen();
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 300, "End of book", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  renderPage();
  saveProgress();
}

void XtcReaderActivity::renderPage() {
  const uint16_t pageWidth = xtc->getPageWidth();
  const uint16_t pageHeight = xtc->getPageHeight();
  const uint8_t bitDepth = xtc->getBitDepth();

  
  
  
  size_t pageBufferSize;
  if (bitDepth == 2) {
    pageBufferSize = ((static_cast<size_t>(pageWidth) * pageHeight + 7) / 8) * 2;
  } else {
    pageBufferSize = ((pageWidth + 7) / 8) * pageHeight;
  }

  
  uint8_t* pageBuffer = static_cast<uint8_t*>(malloc(pageBufferSize));
  if (!pageBuffer) {
    Serial.printf("[%lu] [XTR] Failed to allocate page buffer (%lu bytes)\n", millis(), pageBufferSize);
    renderer.clearScreen();
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 300, "Memory error", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  
  size_t bytesRead = xtc->loadPage(currentPage, pageBuffer, pageBufferSize);
  if (bytesRead == 0) {
    Serial.printf("[%lu] [XTR] Failed to load page %lu\n", millis(), currentPage);
    free(pageBuffer);
    renderer.clearScreen();
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 300, "Page load error", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  
  renderer.clearScreen();

  
  
  const uint16_t maxSrcY = pageHeight;

  if (bitDepth == 2) {
    
    
    
    
    
    

    const size_t planeSize = (static_cast<size_t>(pageWidth) * pageHeight + 7) / 8;
    const uint8_t* plane1 = pageBuffer;              
    const uint8_t* plane2 = pageBuffer + planeSize;  
    const size_t colBytes = (pageHeight + 7) / 8;    

    
    auto getPixelValue = [&](uint16_t x, uint16_t y) -> uint8_t {
      const size_t colIndex = pageWidth - 1 - x;
      const size_t byteInCol = y / 8;
      const size_t bitInByte = 7 - (y % 8);
      const size_t byteOffset = colIndex * colBytes + byteInCol;
      const uint8_t bit1 = (plane1[byteOffset] >> bitInByte) & 1;
      const uint8_t bit2 = (plane2[byteOffset] >> bitInByte) & 1;
      return (bit1 << 1) | bit2;
    };

    
    

    
    uint32_t pixelCounts[4] = {0, 0, 0, 0};
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        pixelCounts[getPixelValue(x, y)]++;
      }
    }
    Serial.printf("[%lu] [XTR] Pixel distribution: White=%lu, DarkGrey=%lu, LightGrey=%lu, Black=%lu\n", millis(),
                  pixelCounts[0], pixelCounts[1], pixelCounts[2], pixelCounts[3]);
    const bool hasActualGrayscale = (pixelCounts[1] > 0 || pixelCounts[2] > 0);

    
    for (uint16_t y = 0; y < pageHeight; y++) {
      for (uint16_t x = 0; x < pageWidth; x++) {
        if (getPixelValue(x, y) >= 1) {
          renderer.drawPixel(x, y, true);
        }
      }
    }

    
    if (pagesUntilFullRefresh <= 1) {
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
    } else {
      renderer.displayBuffer();
      pagesUntilFullRefresh--;
    }

    if (hasActualGrayscale) {
      
      
      renderer.clearScreen(0x00);
      for (uint16_t y = 0; y < pageHeight; y++) {
        for (uint16_t x = 0; x < pageWidth; x++) {
          if (getPixelValue(x, y) == 1) {  
            renderer.drawPixel(x, y, false);
          }
        }
      }
      renderer.copyGrayscaleLsbBuffers();

      
      
      renderer.clearScreen(0x00);
      for (uint16_t y = 0; y < pageHeight; y++) {
        for (uint16_t x = 0; x < pageWidth; x++) {
          const uint8_t pv = getPixelValue(x, y);
          if (pv == 1 || pv == 2) {  
            renderer.drawPixel(x, y, false);
          }
        }
      }
      renderer.copyGrayscaleMsbBuffers();

      
      renderer.displayGrayBuffer();

      
      renderer.clearScreen();
      for (uint16_t y = 0; y < pageHeight; y++) {
        for (uint16_t x = 0; x < pageWidth; x++) {
          if (getPixelValue(x, y) >= 1) {
            renderer.drawPixel(x, y, true);
          }
        }
      }

      
      renderer.cleanupGrayscaleWithFrameBuffer();
    }

    free(pageBuffer);

    Serial.printf("[%lu] [XTR] Rendered page %lu/%lu (2-bit grayscale)\n", millis(), currentPage + 1,
                  xtc->getPageCount());
    return;
  } else {
    
    const size_t srcRowBytes = (pageWidth + 7) / 8;  

    for (uint16_t srcY = 0; srcY < maxSrcY; srcY++) {
      const size_t srcRowStart = srcY * srcRowBytes;

      for (uint16_t srcX = 0; srcX < pageWidth; srcX++) {
        
        const size_t srcByte = srcRowStart + srcX / 8;
        const size_t srcBit = 7 - (srcX % 8);
        const bool isBlack = !((pageBuffer[srcByte] >> srcBit) & 1);  

        if (isBlack) {
          renderer.drawPixel(srcX, srcY, true);
        }
      }
    }
  }
  

  free(pageBuffer);

  

  
  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }

  Serial.printf("[%lu] [XTR] Rendered page %lu/%lu (%u-bit)\n", millis(), currentPage + 1, xtc->getPageCount(),
                bitDepth);
}

void XtcReaderActivity::saveProgress() const {
  FsFile f;
  if (SdMan.openFileForWrite("XTR", xtc->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    data[0] = currentPage & 0xFF;
    data[1] = (currentPage >> 8) & 0xFF;
    data[2] = (currentPage >> 16) & 0xFF;
    data[3] = (currentPage >> 24) & 0xFF;
    f.write(data, 4);
    f.close();
  }
}

void XtcReaderActivity::loadProgress() {
  FsFile f;
  if (SdMan.openFileForRead("XTR", xtc->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
      Serial.printf("[%lu] [XTR] Loaded progress: page %lu\n", millis(), currentPage);

      
      if (currentPage >= xtc->getPageCount()) {
        currentPage = 0;
      }
    }
    f.close();
  }
}
