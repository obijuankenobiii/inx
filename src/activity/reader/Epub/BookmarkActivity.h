#pragma once

#include "../../Activity.h"
#include "GfxRenderer.h"
#include "system/MappedInputManager.h"
#include "EpubActivity.h"
#include <vector>
#include <functional>
#include <memory>

/**
 * @brief Activity for managing bookmarks
 * 
 * Displays a list of bookmarks for the current book and allows
 * navigation to bookmarked positions or deletion of bookmarks.
 */
class BookmarkActivity : public Activity {
public:
    using Bookmark = EpubActivity::Bookmark;
    using SelectCallback = std::function<void(int)>;
    using DeleteCallback = std::function<void(int)>;
    using BackCallback = std::function<void()>;

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
    BookmarkActivity(
        GfxRenderer& renderer,
        MappedInputManager& mappedInput,
        const std::vector<Bookmark>& bookmarks,
        const std::string& bookTitle,
        int currentSpine,
        int currentPage,
        SelectCallback onSelect,
        DeleteCallback onDelete,
        BackCallback onBack);

    void onEnter() override;
    void loop() override;
    void onExit() override;

private:
    /**
     * @brief Renders the bookmark management screen
     */
    void renderScreen();

    GfxRenderer& renderer;
    MappedInputManager& mappedInput;
    std::vector<Bookmark> bookmarks;
    std::string bookTitle;
    int currentSpine;
    int currentPage;
    int selectedIndex;
    
    SelectCallback onSelect;
    DeleteCallback onDelete;
    BackCallback onBack;
};