/**
 * @file TxtReaderActivity.cpp
 * @brief Definitions for TxtReaderActivity.
 */

#include "TxtReaderActivity.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Serialization.h>
#include <Utf8.h>

#include "state/SystemSetting.h"
#include "system/FontManager.h"
#include "state/Session.h"
#include "system/MappedInputManager.h"
#include "state/RecentBooks.h"
#include "system/ScreenComponents.h"
#include "system/Fonts.h"

namespace {
constexpr unsigned long goHomeMs = 1000;
constexpr int statusBarMargin = 25;
constexpr int progressBarMarginTop = 1;
constexpr size_t CHUNK_SIZE = 8 * 1024;  


constexpr uint32_t CACHE_MAGIC = 0x54585449;  
constexpr uint8_t CACHE_VERSION = 2;          
}  

void TxtReaderActivity::taskTrampoline(void* param) {
  auto* self = static_cast<TxtReaderActivity*>(param);
  self->displayTaskLoop();
}

void TxtReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (!txt) {
    return;
  }

  
  switch (SETTINGS.orientation) {
    case SystemSetting::ORIENTATION::PORTRAIT:
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
      break;
    case SystemSetting::ORIENTATION::LANDSCAPE_CW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeClockwise);
      break;
    case SystemSetting::ORIENTATION::INVERTED:
      renderer.setOrientation(GfxRenderer::Orientation::PortraitInverted);
      break;
    case SystemSetting::ORIENTATION::LANDSCAPE_CCW:
      renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
      break;
    default:
      break;
  }

  mappedInput.setInvertDirectionalAxes180(renderer.getOrientation() == GfxRenderer::Orientation::LandscapeClockwise);

  renderingMutex = xSemaphoreCreateMutex();

  txt->setupCacheDir();

  
  APP_STATE.lastRead = txt->getPath();
  APP_STATE.saveToFile();
  RECENT_BOOKS.addBook(txt->getPath(), txt->getCachePath(), "", "");

  
  updateRequired = true;

  xTaskCreate(&TxtReaderActivity::taskTrampoline, "TxtReaderActivityTask",
              6144,               
              this,               
              1,                  
              &displayTaskHandle  
  );
}

void TxtReaderActivity::onExit() {
  ActivityWithSubactivity::onExit();

  
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  mappedInput.setInvertDirectionalAxes180(false);

  
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
  pageOffsets.clear();
  currentPageLines.clear();
  txt.reset();

  FontManager::unloadAllSDFonts();
}

void TxtReaderActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
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

  if (prevTriggered && currentPage > 0) {
    currentPage--;
    updateRequired = true;
  } else if (nextTriggered && currentPage < totalPages - 1) {
    currentPage++;
    updateRequired = true;
  }
}

void TxtReaderActivity::displayTaskLoop() {
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

void TxtReaderActivity::initializeReader() {
  if (initialized) {
    return;
  }

  
  cachedFontId = SETTINGS.getReaderFontId();
  FontManager::ensureFontReady(cachedFontId, renderer);
  cachedScreenMargin = SETTINGS.screenMargin;
  cachedParagraphAlignment = SETTINGS.paragraphAlignment;

  
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += cachedScreenMargin;
  orientedMarginLeft += cachedScreenMargin;
  orientedMarginRight += cachedScreenMargin;
  orientedMarginBottom += cachedScreenMargin;

  
  if (SETTINGS.statusBar != SystemSetting::STATUS_BAR_MODE::NONE) {
    
    const bool showProgressBar = SETTINGS.statusBar == SystemSetting::STATUS_BAR_MODE::FULL_WITH_PROGRESS_BAR ||
                                 SETTINGS.statusBar == SystemSetting::STATUS_BAR_MODE::ONLY_PROGRESS_BAR;
    orientedMarginBottom += statusBarMargin - cachedScreenMargin +
                            (showProgressBar ? (ScreenComponents::BOOK_PROGRESS_BAR_HEIGHT + progressBarMarginTop) : 0);
  }

  viewportWidth = renderer.getScreenWidth() - orientedMarginLeft - orientedMarginRight;
  const int viewportHeight = renderer.getScreenHeight() - orientedMarginTop - orientedMarginBottom;
  const int lineHeight = renderer.getLineHeight(cachedFontId);

  linesPerPage = viewportHeight / lineHeight;
  if (linesPerPage < 1) linesPerPage = 1;

  Serial.printf("[%lu] [TRS] Viewport: %dx%d, lines per page: %d\n", millis(), viewportWidth, viewportHeight,
                linesPerPage);

  
  if (!loadPageIndexCache()) {
    
    buildPageIndex();
    
    savePageIndexCache();
  }

  
  loadProgress();

  initialized = true;
}

void TxtReaderActivity::buildPageIndex() {
  pageOffsets.clear();
  pageOffsets.push_back(0);  

  size_t offset = 0;
  const size_t fileSize = txt->getFileSize();

  Serial.printf("[%lu] [TRS] Building page index for %zu bytes...\n", millis(), fileSize);

  ScreenComponents::drawPopup(renderer, "Indexing...");

  while (offset < fileSize) {
    std::vector<std::string> tempLines;
    size_t nextOffset = offset;

    if (!loadPageAtOffset(offset, tempLines, nextOffset)) {
      break;
    }

    if (nextOffset <= offset) {
      
      break;
    }

    offset = nextOffset;
    if (offset < fileSize) {
      pageOffsets.push_back(offset);
    }

    
    if (pageOffsets.size() % 20 == 0) {
      vTaskDelay(1);
    }
  }

  totalPages = pageOffsets.size();
  Serial.printf("[%lu] [TRS] Built page index: %d pages\n", millis(), totalPages);
}

bool TxtReaderActivity::loadPageAtOffset(size_t offset, std::vector<std::string>& outLines, size_t& nextOffset) {
  outLines.clear();
  const size_t fileSize = txt->getFileSize();

  if (offset >= fileSize) {
    return false;
  }

  
  size_t chunkSize = std::min(CHUNK_SIZE, fileSize - offset);
  auto* buffer = static_cast<uint8_t*>(malloc(chunkSize + 1));
  if (!buffer) {
    Serial.printf("[%lu] [TRS] Failed to allocate %zu bytes\n", millis(), chunkSize);
    return false;
  }

  if (!txt->readContent(buffer, offset, chunkSize)) {
    free(buffer);
    return false;
  }
  buffer[chunkSize] = '\0';

  
  size_t pos = 0;

  while (pos < chunkSize && static_cast<int>(outLines.size()) < linesPerPage) {
    
    size_t lineEnd = pos;
    while (lineEnd < chunkSize && buffer[lineEnd] != '\n') {
      lineEnd++;
    }

    
    bool lineComplete = (lineEnd < chunkSize) || (offset + lineEnd >= fileSize);

    if (!lineComplete && static_cast<int>(outLines.size()) > 0) {
      
      break;
    }

    
    size_t lineContentLen = lineEnd - pos;

    
    bool hasCR = (lineContentLen > 0 && buffer[pos + lineContentLen - 1] == '\r');
    size_t displayLen = hasCR ? lineContentLen - 1 : lineContentLen;

    
    std::string line(reinterpret_cast<char*>(buffer + pos), displayLen);

    
    size_t lineBytePos = 0;

    
    while (!line.empty() && static_cast<int>(outLines.size()) < linesPerPage) {
      int lineWidth = renderer.getTextWidth(cachedFontId, line.c_str());

      if (lineWidth <= viewportWidth) {
        outLines.push_back(line);
        lineBytePos = displayLen;  
        line.clear();
        break;
      }

      
      size_t breakPos = line.length();
      while (breakPos > 0 && renderer.getTextWidth(cachedFontId, line.substr(0, breakPos).c_str()) > viewportWidth) {
        
        size_t spacePos = line.rfind(' ', breakPos - 1);
        if (spacePos != std::string::npos && spacePos > 0) {
          breakPos = spacePos;
        } else {
          
          breakPos--;
          
          while (breakPos > 0 && (line[breakPos] & 0xC0) == 0x80) {
            breakPos--;
          }
        }
      }

      if (breakPos == 0) {
        breakPos = 1;
      }

      outLines.push_back(line.substr(0, breakPos));

      
      size_t skipChars = breakPos;
      if (breakPos < line.length() && line[breakPos] == ' ') {
        skipChars++;
      }
      lineBytePos += skipChars;
      line = line.substr(skipChars);
    }

    
    if (line.empty()) {
      
      pos = lineEnd + 1;
    } else {
      
      
      pos = pos + lineBytePos;
      break;
    }
  }

  
  if (pos == 0 && !outLines.empty()) {
    
    pos = 1;
  }

  nextOffset = offset + pos;

  
  if (nextOffset > fileSize) {
    nextOffset = fileSize;
  }

  free(buffer);

  return !outLines.empty();
}

void TxtReaderActivity::renderScreen() {
  if (!txt) {
    return;
  }

  
  if (!initialized) {
    initializeReader();
  }

  if (pageOffsets.empty()) {
    renderer.clearScreen();
    renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 300, "Empty file", true, EpdFontFamily::BOLD);
    renderer.displayBuffer();
    return;
  }

  
  if (currentPage < 0) currentPage = 0;
  if (currentPage >= totalPages) currentPage = totalPages - 1;

  
  size_t offset = pageOffsets[currentPage];
  size_t nextOffset;
  currentPageLines.clear();
  loadPageAtOffset(offset, currentPageLines, nextOffset);

  renderer.clearScreen();
  renderPage();

  
  saveProgress();
}

void TxtReaderActivity::renderPage() {
  int orientedMarginTop, orientedMarginRight, orientedMarginBottom, orientedMarginLeft;
  renderer.getOrientedViewableTRBL(&orientedMarginTop, &orientedMarginRight, &orientedMarginBottom,
                                   &orientedMarginLeft);
  orientedMarginTop += cachedScreenMargin;
  orientedMarginLeft += cachedScreenMargin;
  orientedMarginRight += cachedScreenMargin;
  orientedMarginBottom += statusBarMargin;

  const int lineHeight = renderer.getLineHeight(cachedFontId);
  const int contentWidth = viewportWidth;

  
  auto renderLines = [&]() {
    int y = orientedMarginTop;
    for (const auto& line : currentPageLines) {
      if (!line.empty()) {
        int x = orientedMarginLeft;

        
        switch (cachedParagraphAlignment) {
          case SystemSetting::FOLLOW_CSS:
          case SystemSetting::LEFT_ALIGN:
          default:
            
            break;
          case SystemSetting::CENTER_ALIGN: {
            int textWidth = renderer.getTextWidth(cachedFontId, line.c_str());
            x = orientedMarginLeft + (contentWidth - textWidth) / 2;
            break;
          }
          case SystemSetting::RIGHT_ALIGN: {
            int textWidth = renderer.getTextWidth(cachedFontId, line.c_str());
            x = orientedMarginLeft + contentWidth - textWidth;
            break;
          }
          case SystemSetting::JUSTIFIED:
            
            
            break;
        }

        renderer.drawText(cachedFontId, x, y, line.c_str());
      }
      y += lineHeight;
    }
  };

  
  renderLines();
  renderStatusBar(orientedMarginRight, orientedMarginBottom, orientedMarginLeft);

  if (pagesUntilFullRefresh <= 1) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    pagesUntilFullRefresh = SETTINGS.getRefreshFrequency();
  } else {
    renderer.displayBuffer();
    pagesUntilFullRefresh--;
  }

  
  if (SETTINGS.textAntiAliasing) {
    
    renderer.storeBwBuffer();

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderLines();
    renderer.copyGrayscaleLsbBuffers();

    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderLines();
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);

    
    renderer.restoreBwBuffer();
  }
}

void TxtReaderActivity::renderStatusBar(const int orientedMarginRight, const int orientedMarginBottom,
                                        const int orientedMarginLeft) const {
  const bool showProgressPercentage = SETTINGS.statusBar == SystemSetting::STATUS_BAR_MODE::FULL;
  const bool showProgressBar = SETTINGS.statusBar == SystemSetting::STATUS_BAR_MODE::FULL_WITH_PROGRESS_BAR ||
                               SETTINGS.statusBar == SystemSetting::STATUS_BAR_MODE::ONLY_PROGRESS_BAR;
  const bool showProgressText = SETTINGS.statusBar == SystemSetting::STATUS_BAR_MODE::FULL ||
                                SETTINGS.statusBar == SystemSetting::STATUS_BAR_MODE::FULL_WITH_PROGRESS_BAR;
  const bool showBattery = SETTINGS.statusBar == SystemSetting::STATUS_BAR_MODE::NO_PROGRESS ||
                           SETTINGS.statusBar == SystemSetting::STATUS_BAR_MODE::FULL ||
                           SETTINGS.statusBar == SystemSetting::STATUS_BAR_MODE::FULL_WITH_PROGRESS_BAR;
  const bool showTitle = SETTINGS.statusBar == SystemSetting::STATUS_BAR_MODE::NO_PROGRESS ||
                         SETTINGS.statusBar == SystemSetting::STATUS_BAR_MODE::FULL ||
                         SETTINGS.statusBar == SystemSetting::STATUS_BAR_MODE::FULL_WITH_PROGRESS_BAR;
  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage == SystemSetting::HIDE_BATTERY_PERCENTAGE::HIDE_NEVER;

  const auto screenHeight = renderer.getScreenHeight();
  const auto textY = screenHeight - orientedMarginBottom - 4;
  int progressTextWidth = 0;

  const float progress = totalPages > 0 ? (currentPage + 1) * 100.0f / totalPages : 0;

  if (showProgressText || showProgressPercentage) {
    char progressStr[32];
    if (showProgressPercentage) {
      snprintf(progressStr, sizeof(progressStr), "%d/%d %.0f%%", currentPage + 1, totalPages, progress);
    } else {
      snprintf(progressStr, sizeof(progressStr), "%d/%d", currentPage + 1, totalPages);
    }

    progressTextWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, progressStr);
    renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, renderer.getScreenWidth() - orientedMarginRight - progressTextWidth, textY,
                      progressStr);
  }

  if (showProgressBar) {
    
    ScreenComponents::drawBookProgressBar(renderer, static_cast<size_t>(progress));
  }

  if (showBattery) {
    ScreenComponents::drawBattery(renderer, orientedMarginLeft, textY, showBatteryPercentage);
  }

  if (showTitle) {
    const int titleMarginLeft = 50 + 30 + orientedMarginLeft;
    const int titleMarginRight = progressTextWidth + 30 + orientedMarginRight;
    const int availableTextWidth = renderer.getScreenWidth() - titleMarginLeft - titleMarginRight;

    std::string title = txt->getTitle();
    int titleWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, title.c_str());
    if (titleWidth > availableTextWidth) {
      title = renderer.truncatedText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, title.c_str(), availableTextWidth);
      titleWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, title.c_str());
    }

    renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, titleMarginLeft + (availableTextWidth - titleWidth) / 2, textY, title.c_str());
  }
}

void TxtReaderActivity::saveProgress() const {
  FsFile f;
  if (SdMan.openFileForWrite("TRS", txt->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    data[0] = currentPage & 0xFF;
    data[1] = (currentPage >> 8) & 0xFF;
    data[2] = 0;
    data[3] = 0;
    f.write(data, 4);
    f.close();
  }
}

void TxtReaderActivity::loadProgress() {
  FsFile f;
  if (SdMan.openFileForRead("TRS", txt->getCachePath() + "/progress.bin", f)) {
    uint8_t data[4];
    if (f.read(data, 4) == 4) {
      currentPage = data[0] + (data[1] << 8);
      if (currentPage >= totalPages) {
        currentPage = totalPages - 1;
      }
      if (currentPage < 0) {
        currentPage = 0;
      }
      Serial.printf("[%lu] [TRS] Loaded progress: page %d/%d\n", millis(), currentPage, totalPages);
    }
    f.close();
  }
}

bool TxtReaderActivity::loadPageIndexCache() {
  
  
  
  
  
  
  
  
  
  
  

  std::string cachePath = txt->getCachePath() + "/index.bin";
  FsFile f;
  if (!SdMan.openFileForRead("TRS", cachePath, f)) {
    Serial.printf("[%lu] [TRS] No page index cache found\n", millis());
    return false;
  }

  
  uint32_t magic;
  serialization::readPod(f, magic);
  if (magic != CACHE_MAGIC) {
    Serial.printf("[%lu] [TRS] Cache magic mismatch, rebuilding\n", millis());
    f.close();
    return false;
  }

  uint8_t version;
  serialization::readPod(f, version);
  if (version != CACHE_VERSION) {
    Serial.printf("[%lu] [TRS] Cache version mismatch (%d != %d), rebuilding\n", millis(), version, CACHE_VERSION);
    f.close();
    return false;
  }

  uint32_t fileSize;
  serialization::readPod(f, fileSize);
  if (fileSize != txt->getFileSize()) {
    Serial.printf("[%lu] [TRS] Cache file size mismatch, rebuilding\n", millis());
    f.close();
    return false;
  }

  int32_t cachedWidth;
  serialization::readPod(f, cachedWidth);
  if (cachedWidth != viewportWidth) {
    Serial.printf("[%lu] [TRS] Cache viewport width mismatch, rebuilding\n", millis());
    f.close();
    return false;
  }

  int32_t cachedLines;
  serialization::readPod(f, cachedLines);
  if (cachedLines != linesPerPage) {
    Serial.printf("[%lu] [TRS] Cache lines per page mismatch, rebuilding\n", millis());
    f.close();
    return false;
  }

  int32_t fontId;
  serialization::readPod(f, fontId);
  if (fontId != cachedFontId) {
    Serial.printf("[%lu] [TRS] Cache font ID mismatch (%d != %d), rebuilding\n", millis(), fontId, cachedFontId);
    f.close();
    return false;
  }

  int32_t margin;
  serialization::readPod(f, margin);
  if (margin != cachedScreenMargin) {
    Serial.printf("[%lu] [TRS] Cache screen margin mismatch, rebuilding\n", millis());
    f.close();
    return false;
  }

  uint8_t alignment;
  serialization::readPod(f, alignment);
  if (alignment != cachedParagraphAlignment) {
    Serial.printf("[%lu] [TRS] Cache paragraph alignment mismatch, rebuilding\n", millis());
    f.close();
    return false;
  }

  uint32_t numPages;
  serialization::readPod(f, numPages);

  
  pageOffsets.clear();
  pageOffsets.reserve(numPages);

  for (uint32_t i = 0; i < numPages; i++) {
    uint32_t offset;
    serialization::readPod(f, offset);
    pageOffsets.push_back(offset);
  }

  f.close();
  totalPages = pageOffsets.size();
  Serial.printf("[%lu] [TRS] Loaded page index cache: %d pages\n", millis(), totalPages);
  return true;
}

void TxtReaderActivity::savePageIndexCache() const {
  std::string cachePath = txt->getCachePath() + "/index.bin";
  FsFile f;
  if (!SdMan.openFileForWrite("TRS", cachePath, f)) {
    Serial.printf("[%lu] [TRS] Failed to save page index cache\n", millis());
    return;
  }

  
  serialization::writePod(f, CACHE_MAGIC);
  serialization::writePod(f, CACHE_VERSION);
  serialization::writePod(f, static_cast<uint32_t>(txt->getFileSize()));
  serialization::writePod(f, static_cast<int32_t>(viewportWidth));
  serialization::writePod(f, static_cast<int32_t>(linesPerPage));
  serialization::writePod(f, static_cast<int32_t>(cachedFontId));
  serialization::writePod(f, static_cast<int32_t>(cachedScreenMargin));
  serialization::writePod(f, cachedParagraphAlignment);
  serialization::writePod(f, static_cast<uint32_t>(pageOffsets.size()));

  
  for (size_t offset : pageOffsets) {
    serialization::writePod(f, static_cast<uint32_t>(offset));
  }

  f.close();
  Serial.printf("[%lu] [TRS] Saved page index cache: %d pages\n", millis(), totalPages);
}
