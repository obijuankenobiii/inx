#include "TableOfContentsActivity.h"
#include <GfxRenderer.h>
#include "system/Fonts.h"
#include "system/MappedInputManager.h" 

namespace {
constexpr int SKIP_PAGE_MS = 700;
constexpr int LIST_ITEM_HEIGHT = 60;
}

int TableOfContentsActivity::getPageItems() const {
    const int screenHeight = renderer.getScreenHeight();
    constexpr int reservedHeight = 190;
    int items = (screenHeight - reservedHeight) / LIST_ITEM_HEIGHT;
    int result = (items < 1) ? 1 : items;
    return result;
}

void TableOfContentsActivity::taskTrampoline(void* param) {
    auto* self = static_cast<TableOfContentsActivity*>(param);
    self->displayTaskLoop();
}

void TableOfContentsActivity::onEnter() {
    Serial.println("[DEBUG] TableOfContentsActivity::onEnter called");
    ActivityWithSubactivity::onEnter();
    if (!epub) {
        Serial.println("[DEBUG] epub is null in onEnter");
        return;
    }

    renderingMutex = xSemaphoreCreateMutex();
    int currentTocIdx = epub->getTocIndexForSpineIndex(currentSpineIndex);
    selectorIndex = (currentTocIdx == -1) ? 0 : currentTocIdx;
    Serial.printf("[DEBUG] initial selectorIndex: %d (currentSpineIndex: %d)\n", selectorIndex, currentSpineIndex);

    updateRequired = true;
    xTaskCreate(&TableOfContentsActivity::taskTrampoline, "EpubChapterSelectTask", 4096, this, 1,
                &displayTaskHandle);
}

void TableOfContentsActivity::onExit() {
    // Stop display task first
    if (displayTaskHandle) {
        vTaskDelete(displayTaskHandle);
        displayTaskHandle = nullptr;
    }
    
    // Then delete mutex
    if (renderingMutex) {
        vSemaphoreDelete(renderingMutex);
        renderingMutex = nullptr;
    }
    
    ActivityWithSubactivity::onExit();
}

void TableOfContentsActivity::loop() {
    if (subActivity) {
        subActivity->loop();
        return;
    }

    const bool prevReleased = mappedInput.wasReleased(MappedInputManager::Button::Up) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Left);
    const bool nextReleased = mappedInput.wasReleased(MappedInputManager::Button::Down) ||
                              mappedInput.wasReleased(MappedInputManager::Button::Right);

    const bool skipPage = mappedInput.getHeldTime() > SKIP_PAGE_MS;
    const int pageItems = getPageItems();
    const int totalItems = epub->getTocItemsCount();

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        const int newSpineIndex = epub->getSpineIndexForTocIndex(selectorIndex);
        Serial.printf("[DEBUG] Confirm pressed, selected spine: %d (selectorIndex: %d)\n", newSpineIndex, selectorIndex);
        onSelectSpineIndex(newSpineIndex);

    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
        Serial.println("[DEBUG] Back pressed, returning to currentSpineIndex");
        onSelectSpineIndex(currentSpineIndex);

    } else if (prevReleased) {
        int oldIndex = selectorIndex;
        if (skipPage) {
            selectorIndex = (selectorIndex < pageItems) ? 0 : selectorIndex - pageItems;
        } else {
            selectorIndex = (selectorIndex + totalItems - 1) % totalItems;
        }
        Serial.printf("[DEBUG] prevReleased: selectorIndex changed from %d to %d\n", oldIndex, selectorIndex);
        updateRequired = true;
    } else if (nextReleased) {
        int oldIndex = selectorIndex;
        if (skipPage) {
            selectorIndex = (selectorIndex + pageItems >= totalItems) ? totalItems - 1 : selectorIndex + pageItems;
        } else {
            selectorIndex = (selectorIndex + 1) % totalItems;
        }
        Serial.printf("[DEBUG] nextReleased: selectorIndex changed from %d to %d\n", oldIndex, selectorIndex);
        updateRequired = true;
    }
}

void TableOfContentsActivity::displayTaskLoop() {
    Serial.println("[DEBUG] displayTaskLoop started");
    while (true) {
        if (updateRequired && !subActivity) {
            updateRequired = false;
            if (xSemaphoreTake(renderingMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                Serial.printf("[DEBUG] rendering screen for selectorIndex %d\n", selectorIndex);
                renderScreen();
                xSemaphoreGive(renderingMutex);
            } else {
                Serial.println("[DEBUG] failed to take renderingMutex for renderScreen");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void TableOfContentsActivity::renderScreen() {
    Serial.printf("[DEBUG] renderScreen called (selectorIndex: %d)\n", selectorIndex);
    renderer.clearScreen();
    const int screenWidth = renderer.getScreenWidth();
    const int totalItems = epub->getTocItemsCount();
    const int pageItems = getPageItems();

    // Header
    const int headerY = 20;
    renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, headerY, "Table of Contents", true, EpdFontFamily::BOLD);

    const char* subtitleText = "Select a chapter to begin reading.";
    int subtitleY = headerY + 40;
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, subtitleText);

    const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
    renderer.drawLine(0, dividerY, screenWidth, dividerY);

    // List
    const int pageStartIndex = (selectorIndex / pageItems) * pageItems;
    int drawY = dividerY + 2;

    for (int i = 0; i < pageItems; i++) {
        int itemIndex = pageStartIndex + i;
        if (itemIndex >= totalItems) break;

        int itemY = drawY + (i * LIST_ITEM_HEIGHT);
        bool isSelected = (itemIndex == selectorIndex);

        if (isSelected) {
            renderer.fillRect(0, itemY, screenWidth, LIST_ITEM_HEIGHT);
            Serial.printf("[DEBUG] rendering selected TOC item at index %d\n", itemIndex);
        }

        int textY = itemY + (LIST_ITEM_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;

        auto item = epub->getTocItem(itemIndex);
        int indentSize = 20 + (item.level - 1) * 20;
        const std::string truncatedName =
            renderer.truncatedText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, item.title.c_str(), screenWidth - 60 - indentSize);

        renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, indentSize, textY, truncatedName.c_str(), isSelected ? 0 : 1);
        renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenWidth - 30, textY, "›", isSelected ? 0 : 1);
        renderer.drawLine(0, itemY + LIST_ITEM_HEIGHT - 1, screenWidth, itemY + LIST_ITEM_HEIGHT - 1);
    }

    // Footer
    const int totalPages = (totalItems + pageItems - 1) / pageItems;
    const int currentPageNum = (selectorIndex / pageItems) + 1;
    char pageStr[24];
    snprintf(pageStr, sizeof(pageStr), "Page %d of %d", currentPageNum, totalPages);
    int pageStrWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageStr);
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, (screenWidth - pageStrWidth) / 2, renderer.getScreenHeight() - 75, pageStr);

    if (renderer.getOrientation() != GfxRenderer::LandscapeClockwise) {
        const auto labels = mappedInput.mapLabels("« Back", "Select", "Up", "Down");
        renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }

    renderer.displayBuffer();
}