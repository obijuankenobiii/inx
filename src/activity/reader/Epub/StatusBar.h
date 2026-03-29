#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include <string>
#include "GfxRenderer.h"
#include "state/BookSetting.h"
#include "Epub.h"
#include <Epub/Section.h>

/**
 * @brief Manages the status bar rendering for the EPUB reader
 */
class StatusBar {
public:
    /**
     * @brief Constructs a new StatusBar
     * @param renderer Reference to the graphics renderer
     * @param epub Reference to the EPUB document
     * @param settings Reference to the book settings
     */
    StatusBar(GfxRenderer& renderer, const Epub& epub, const BookSettings& settings);
    
    /**
     * @brief Renders the complete status bar
     * @param section Current section being read
     * @param currentSpineIndex Current spine index
     * @param orientedMarginRight Right margin
     * @param orientedMarginBottom Bottom margin
     * @param orientedMarginLeft Left margin
     */
    void render(const Section* section, int currentSpineIndex,
                int orientedMarginRight, int orientedMarginBottom, int orientedMarginLeft) const;

    /**
     * @brief Sets whether the status bar is visible
     * @param visible True to show, false to hide
     */
    void setVisible(bool visible) { m_visible = visible; }
    
    /**
     * @brief Checks if the status bar is visible
     * @return True if visible
     */
    bool isVisible() const { return m_visible; }

private:
    /**
     * @brief Renders a single status bar section
     * @param position Section position (0=left, 1=middle, 2=right)
     * @param sectionStart Starting X coordinate of the section
     * @param sectionCenter Center X coordinate of the section
     * @param sectionWidth Width of the section
     * @param textY Y coordinate for text rendering
     * @param section Current section
     * @param currentSpineIndex Current spine index
     */
    void renderSection(int position, int sectionStart, int sectionCenter,
                      int sectionWidth, int textY, const Section* section, 
                      int currentSpineIndex) const;
    
    /**
     * @brief Renders page position bars within a section
     * @param sectionStart Starting X coordinate of the section
     * @param sectionCenter Center X coordinate of the section
     * @param sectionWidth Width of the section
     * @param textY Y coordinate for text rendering
     * @param section Current section
     */
    void renderPageBars(int sectionStart, int sectionCenter, int sectionWidth, 
                       int textY, const Section* section) const;
    
    /**
     * @brief Gets the configuration for a specific status bar position
     * @param position Section position (0=left, 1=middle, 2=right)
     * @return Status bar section configuration
     */
    StatusBarSectionConfig getConfig(int position) const;
    
    /**
     * @brief Gets formatted page string
     * @param section Current section
     * @return Formatted page string (e.g., "42/100")
     */
    std::string getPageString(const Section* section) const;
    
    /**
     * @brief Gets formatted percentage string
     * @param bookProgress Book progress as percentage
     * @return Formatted percentage string (e.g., "42%")
     */
    std::string getPercentString(float bookProgress) const;
    
    /**
     * @brief Gets formatted battery percentage string
     * @return Battery percentage string
     */
    std::string getBatteryPercentString() const;
    
    /**
     * @brief Gets the current chapter title
     * @param currentSpineIndex Current spine index
     * @return Chapter title
     */
    std::string getChapterTitle(int currentSpineIndex) const;
    
    /**
     * @brief Calculates overall book progress
     * @param section Current section
     * @param currentSpineIndex Current spine index
     * @return Book progress as percentage (0-100)
     */
    float calculateBookProgress(const Section* section, int currentSpineIndex) const;
    
    GfxRenderer& m_renderer;
    const Epub& m_epub;
    const BookSettings& m_settings;
    bool m_visible;
};

#endif // STATUS_BAR_H