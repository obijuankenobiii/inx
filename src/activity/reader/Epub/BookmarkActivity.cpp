#include "BookmarkActivity.h"
#include "system/Fonts.h"
#include "system/ScreenComponents.h"
#include <time.h>

constexpr int LIST_ITEM_HEIGHT = 60;

/**
 * @brief Constructs a new BookmarkActivity
 * @param renderer Reference to the graphics renderer
 * @param mappedInput Reference to the input manager
 * @param bookmarks Vector of bookmarks to display
 * @param bookTitle Title of the current book
 * @param currentSpine Current spine index
 * @param currentPage Current page number
 * @param onSelect Callback when a bookmark is selected
 * @param onDelete Callback when a bookmark is deleted
 * @param onBack Callback when back button is pressed
 */
BookmarkActivity::BookmarkActivity(
    GfxRenderer& renderer,
    MappedInputManager& mappedInput,
    const std::vector<Bookmark>& bookmarks,
    const std::string& bookTitle,
    int currentSpine,
    int currentPage,
    SelectCallback onSelect,
    DeleteCallback onDelete,
    BackCallback onBack)
    : Activity("EpubReaderBookmark", renderer, mappedInput)
    , renderer(renderer)
    , mappedInput(mappedInput)
    , bookmarks(bookmarks)
    , bookTitle(bookTitle)
    , currentSpine(currentSpine)
    , currentPage(currentPage)
    , selectedIndex(0)
    , onSelect(std::move(onSelect))
    , onDelete(std::move(onDelete))
    , onBack(std::move(onBack)) {}

/**
 * @brief Called when entering the activity
 */
void BookmarkActivity::onEnter() {
    renderer.clearScreen();
    renderScreen();
}

/**
 * @brief Main loop function called repeatedly while activity is active
 */
void BookmarkActivity::loop() {
    bool needUpdate = false;
    
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        onBack();
        return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        if (!bookmarks.empty()) {
            onSelect(selectedIndex);
        }
        return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        if (!bookmarks.empty()) {
            onDelete(selectedIndex);
            if (selectedIndex >= static_cast<int>(bookmarks.size())) {
                selectedIndex = bookmarks.size() - 1;
            }
            needUpdate = true;
        }
        return;
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
        if (!bookmarks.empty()) {
            selectedIndex = (selectedIndex - 1 + bookmarks.size()) % bookmarks.size();
            needUpdate = true;
        }
    }

    if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
        if (!bookmarks.empty()) {
            selectedIndex = (selectedIndex + 1) % bookmarks.size();
            needUpdate = true;
        }
    }

    if (needUpdate) {
        renderScreen();
    }
}

/**
 * @brief Called when exiting the activity
 */
void BookmarkActivity::onExit() {
    renderer.clearScreen();
}

/**
 * @brief Renders the bookmark management screen
 */
void BookmarkActivity::renderScreen() {
    renderer.clearScreen();
    const int screenWidth = renderer.getScreenWidth();

    // Draw header
    const int headerY = 20;
    renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 20, headerY, "Bookmarks", true, EpdFontFamily::BOLD);
    
    // Draw book title (truncated if too long)
    std::string displayTitle = bookTitle;
    if (displayTitle.length() > 30) {
        displayTitle = displayTitle.substr(0, 27) + "...";
    }
    
    int subtitleY = headerY + 40;
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, subtitleY, displayTitle.c_str());

    // Draw divider line
    const int dividerY = subtitleY + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID) + 10;
    renderer.drawLine(0, dividerY, screenWidth, dividerY);

    // Show empty state if no bookmarks
    if (bookmarks.empty()) {
        int textY = dividerY + 60;
        renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textY, "No bookmarks yet", true);
        renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textY + 30, "Long press Confirm while reading", true);
        renderer.drawCenteredText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, textY + 60, "to add a bookmark", true);
        
        const auto labels = mappedInput.mapLabels("« Back", "", "Up", "Down");
        renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
        
        renderer.displayBuffer();
        return;
    }

    // Calculate visible items based on screen height
    int drawY = dividerY + 2;
    const int visibleItems = (renderer.getScreenHeight() - drawY - 80) / LIST_ITEM_HEIGHT;
    
    // Calculate start and end indices for scrolling
    int startIndex = selectedIndex - visibleItems / 2;
    if (startIndex < 0) startIndex = 0;
    if (startIndex + visibleItems > static_cast<int>(bookmarks.size())) {
        startIndex = std::max(0, static_cast<int>(bookmarks.size()) - visibleItems);
    }
    int endIndex = std::min(static_cast<int>(bookmarks.size()), startIndex + visibleItems);

    // Display bookmarks in reverse order (newest first)
    for (int i = startIndex; i < endIndex; i++) {
        int displayIndex = bookmarks.size() - 1 - i;
        const auto& b = bookmarks[displayIndex];
        int itemY = drawY + (i - startIndex) * LIST_ITEM_HEIGHT;
        
        bool isSelected = (i == selectedIndex);

        // Highlight selected item
        if (isSelected) {
            renderer.fillRect(0, itemY, screenWidth, LIST_ITEM_HEIGHT);
        }

        // Draw chapter title
        int textX = 20;
        int textY = itemY + (LIST_ITEM_HEIGHT - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_12_FONT_ID)) / 2;
        
        std::string chapterTitle = b.chapterTitle;
        int maxTitleWidth = screenWidth - 150;
        if (renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_12_FONT_ID, chapterTitle.c_str()) > maxTitleWidth) {
            chapterTitle = renderer.truncatedText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, chapterTitle.c_str(), maxTitleWidth);
        }
        
        renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, textX, textY, chapterTitle.c_str(), isSelected ? 0 : 1);

        // Draw page number
        char pageInfo[32];
        snprintf(pageInfo, sizeof(pageInfo), "%d/%d", b.pageNumber + 1, b.pageCount);
        int pageWidth = renderer.getTextWidth(ATKINSON_HYPERLEGIBLE_10_FONT_ID, pageInfo);
        renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, screenWidth - pageWidth - 50, textY + 5, pageInfo, isSelected ? 0 : 1);

        // Draw separator line
        renderer.drawLine(0, itemY + LIST_ITEM_HEIGHT - 1, screenWidth, itemY + LIST_ITEM_HEIGHT - 1);
    }

    // Draw button hints
    const auto labels = mappedInput.mapLabels("« Back", "Go to", "", "Delete");
    renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    
    renderer.displayBuffer();
}