/**
 * @file StatusBar.cpp
 * @brief Definitions for StatusBar.
 */

#include "StatusBar.h"
#include "system/Fonts.h"
#include "system/ScreenComponents.h"

#ifdef HAS_BATTERY_MANAGER
#include "BatteryManager.h"
#endif


static const int STATUS_BAR_LEFT = 0;
static const int STATUS_BAR_MIDDLE = 1;
static const int STATUS_BAR_RIGHT = 2;

/**
 * @brief Constructs a new StatusBar
 * @param renderer Reference to the graphics renderer
 * @param epub Reference to the EPUB document
 * @param settings Reference to the book settings
 */
StatusBar::StatusBar(GfxRenderer& renderer, const Epub& epub, const BookSettings& settings)
    : m_renderer(renderer), m_epub(epub), m_settings(settings), m_visible(true) {
}

/**
 * @brief Renders the complete status bar with three configurable sections
 * @param section Current section being read
 * @param currentSpineIndex Current spine index
 * @param orientedMarginRight Right margin
 * @param orientedMarginBottom Bottom margin
 * @param orientedMarginLeft Left margin
 */
void StatusBar::render(const Section* section, int currentSpineIndex,
                      int orientedMarginRight, int orientedMarginBottom, 
                      int orientedMarginLeft) const {
    if (!m_visible || !section || section->pageCount == 0) {
        return;
    }

    const int screenHeight = m_renderer.getScreenHeight();
    const int screenWidth = m_renderer.getScreenWidth();
    const int textY = screenHeight - orientedMarginBottom - 4;
    
    const int availableWidth = screenWidth - orientedMarginLeft - orientedMarginRight;
    const int sectionWidth = availableWidth / 3;
    
    
    const int leftSectionStart = orientedMarginLeft;
    const int leftSectionCenter = leftSectionStart + (sectionWidth / 2);
    
    const int middleThirdStart = orientedMarginLeft + sectionWidth;
    const int middleSectionCenter = middleThirdStart + (sectionWidth / 2);
    
    const int rightThirdStart = middleThirdStart + sectionWidth;
    const int rightSectionCenter = rightThirdStart + (sectionWidth / 2);
    const int rightSectionStart = rightThirdStart;

    
    renderSection(STATUS_BAR_LEFT, leftSectionStart, leftSectionCenter, 
                 sectionWidth, textY, section, currentSpineIndex);
    
    renderSection(STATUS_BAR_MIDDLE, middleThirdStart, middleSectionCenter,
                 sectionWidth, textY, section, currentSpineIndex);
    
    renderSection(STATUS_BAR_RIGHT, rightSectionStart, rightSectionCenter,
                 sectionWidth, textY, section, currentSpineIndex);
}

/**
 * @brief Renders a single status bar section based on its configuration
 * @param position Section position (0=left, 1=middle, 2=right)
 * @param sectionStart Starting X coordinate of the section
 * @param sectionCenter Center X coordinate of the section
 * @param sectionWidth Width of the section
 * @param textY Y coordinate for text rendering
 * @param section Current section
 * @param currentSpineIndex Current spine index
 */
void StatusBar::renderSection(int position, int sectionStart, int sectionCenter,
                             int sectionWidth, int textY, const Section* section, 
                             int currentSpineIndex) const {
    
    StatusBarSectionConfig config = getConfig(position);
    
    if (config.item == StatusBarItem::NONE) {
        return;
    }
    
    const float bookProgress = calculateBookProgress(section, currentSpineIndex);
    const std::string pageStr = getPageString(section);
    const std::string percentStr = getPercentString(bookProgress);
    const std::string chapterTitle = getChapterTitle(currentSpineIndex);
    const std::string batteryPercentStr = getBatteryPercentString();
    
    auto getRightAlignedX = [&](const char* text) -> int {
        int textWidth = m_renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, text);
        return sectionStart + sectionWidth - textWidth - 5;
    };
    
    auto getCenteredX = [&](const char* text) -> int {
        int textWidth = m_renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, text);
        return sectionCenter - (textWidth / 2);
    };
    
    auto getPositionX = [&](const char* text) -> int {
        switch (position) {
            case STATUS_BAR_LEFT:
                return sectionStart + 5;
            case STATUS_BAR_MIDDLE:
                return getCenteredX(text);
            case STATUS_BAR_RIGHT:
                return getRightAlignedX(text);
            default:
                return sectionStart + 5;
        }
    };
    
    switch (config.item) {
        case StatusBarItem::PAGE_NUMBERS: {
            int xPos = getPositionX(pageStr.c_str());
            m_renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, xPos, textY, pageStr.c_str());
            break;
        }
            
        case StatusBarItem::PERCENTAGE: {
            int xPos = getPositionX(percentStr.c_str());
            m_renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, xPos, textY, percentStr.c_str());
            break;
        }
            
        case StatusBarItem::CHAPTER_TITLE: {
            int maxWidth = sectionWidth - 10;
            std::string truncated = m_renderer.text.truncate(ATKINSON_HYPERLEGIBLE_8_FONT_ID, 
                                                          chapterTitle.c_str(), maxWidth);
            int xPos = getPositionX(truncated.c_str());
            m_renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, xPos, textY, truncated.c_str());
            break;
        }
            
        case StatusBarItem::BATTERY_ICON: {
            int xPos;
            if (position == STATUS_BAR_RIGHT) {
                xPos = getRightAlignedX("   ");
            } else if (position == STATUS_BAR_MIDDLE) {
                xPos = sectionCenter - 10;
            } else {
                xPos = sectionStart + 5;
            }
            ScreenComponents::drawBattery(m_renderer, xPos, textY, false);
            break;
        }
            
        case StatusBarItem::BATTERY_PERCENTAGE: {
            int xPos = getPositionX(batteryPercentStr.c_str());
            m_renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, xPos, textY, batteryPercentStr.c_str());
            break;
        }
            
        case StatusBarItem::BATTERY_ICON_WITH_PERCENT: {
            int xPos;
            if (position == STATUS_BAR_RIGHT) {
                xPos = getRightAlignedX(batteryPercentStr.c_str()) - 25;
            } else if (position == STATUS_BAR_MIDDLE) {
                xPos = sectionCenter - 25;
            } else {
                xPos = sectionStart + 5;
            }
            ScreenComponents::drawBattery(m_renderer, xPos, textY, true);
            break;
        }
            
        case StatusBarItem::PROGRESS_BAR: {
            int barWidth = std::min(100, sectionWidth - 20);
            int barX = sectionCenter - (barWidth / 2);
            int barY = textY + 10;
            
            m_renderer.rectangle.render(barX, barY, barWidth, 6, true);
            int fillWidth = static_cast<int>((bookProgress / 100.0f) * (barWidth - 2));
            if (fillWidth > 0) {
                m_renderer.rectangle.fill(barX + 1, barY + 1, fillWidth, 4, true);
            }
            break;
        }
            
        case StatusBarItem::PROGRESS_BAR_WITH_PERCENT: {
            int percentWidth = m_renderer.text.getWidth(ATKINSON_HYPERLEGIBLE_8_FONT_ID, percentStr.c_str());
            int barWidth = std::min(80, sectionWidth - percentWidth - 20);
            
            int barX, percentX;
            int barY = textY + 8;
            
            if (position == STATUS_BAR_RIGHT) {
                percentX = getRightAlignedX(percentStr.c_str());
                barX = percentX - barWidth - 5;
            } else {
                barX = sectionCenter - (barWidth + percentWidth + 10) / 2;
                percentX = barX + barWidth + 5;
            }
            
            m_renderer.rectangle.render(barX, barY, barWidth, 6, true);
            int fillWidth = static_cast<int>((bookProgress / 100.0f) * (barWidth - 2));
            if (fillWidth > 0) {
                m_renderer.rectangle.fill(barX + 1, barY + 1, fillWidth, 4, true);
            }
            m_renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, percentX, textY, percentStr.c_str());
            break;
        }
            
        case StatusBarItem::PAGE_BARS:
            renderPageBars(sectionStart, sectionCenter, sectionWidth, textY, section);
            break;
                
        case StatusBarItem::BOOK_TITLE: {
            std::string bookTitle = m_epub.getTitle();
            int maxWidth = sectionWidth - 10;
            std::string truncated = m_renderer.text.truncate(ATKINSON_HYPERLEGIBLE_8_FONT_ID, 
                                                          bookTitle.c_str(), maxWidth);
            int xPos = getPositionX(truncated.c_str());
            m_renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, xPos, textY, truncated.c_str());
            break;
        }
            
        case StatusBarItem::AUTHOR_NAME: {
            std::string author = m_epub.getAuthor();
            int maxWidth = sectionWidth - 10;
            std::string truncated = m_renderer.text.truncate(ATKINSON_HYPERLEGIBLE_8_FONT_ID, 
                                                          author.c_str(), maxWidth);
            int xPos = getPositionX(truncated.c_str());
            m_renderer.text.render(ATKINSON_HYPERLEGIBLE_8_FONT_ID, xPos, textY, truncated.c_str());
            break;
        }
            
        default:
            break;
    }
}

/**
 * @brief Renders page position bars visualization within a section
 * @param sectionStart Starting X coordinate of the section
 * @param sectionCenter Center X coordinate of the section
 * @param sectionWidth Width of the section
 * @param textY Y coordinate for text rendering
 * @param section Current section
 */
void StatusBar::renderPageBars(int sectionStart, int sectionCenter, int sectionWidth, 
                              int textY, const Section* section) const {
    if (!section) return;
    
    const int maxBars = 30;
    const int barHeight = 5;
    const int minBarWidth = 2;
    
    int pageCount = static_cast<int>(section->pageCount);
    int barCount = (pageCount < maxBars) ? pageCount : maxBars;
    int pagesPerBar = pageCount / barCount;
    if (pagesPerBar < 1) pagesPerBar = 1;
    
    int barWidth = (sectionWidth - 20) / barCount;
    if (barWidth < minBarWidth) barWidth = minBarWidth;
    
    int totalWidth = barCount * barWidth;
    int barStartX = sectionCenter - (totalWidth / 2);
    
    int currentPage = section->currentPage;
    int barY = textY + 10;
    
    for (int i = 0; i < barCount; i++) {
        int barEndPage = (i == barCount - 1) ? pageCount - 1 : (i + 1) * pagesPerBar - 1;
        int x = barStartX + i * barWidth;
        
        if (x + barWidth > sectionStart + sectionWidth - 5) {
            barWidth = sectionStart + sectionWidth - 5 - x;
            if (barWidth < 1) break;
        }
        
        if (barEndPage < currentPage) {
            m_renderer.rectangle.fill(x, barY, barWidth - 1, barHeight, true);
        } else if (i * pagesPerBar > currentPage) {
            m_renderer.rectangle.render(x, barY, barWidth - 1, barHeight, true);
        } else {
            m_renderer.rectangle.render(x, barY + (barHeight / 2), barWidth - 1, barHeight / 2, true);
        }
    }
}

/**
 * @brief Gets the configuration for a specific status bar position
 * @param position Section position (0=left, 1=middle, 2=right)
 * @return Status bar section configuration
 */
StatusBarSectionConfig StatusBar::getConfig(int position) const {
    switch (position) {
        case STATUS_BAR_LEFT:
            return m_settings.statusBarLeft;
        case STATUS_BAR_MIDDLE:
            return m_settings.statusBarMiddle;
        case STATUS_BAR_RIGHT:
            return m_settings.statusBarRight;
        default:
            return StatusBarSectionConfig();
    }
}

/**
 * @brief Gets formatted page string
 * @param section Current section
 * @return Formatted page string (e.g., "42/100")
 */
std::string StatusBar::getPageString(const Section* section) const {
    if (!section) return "0/0";
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%d/%d", section->currentPage + 1, section->pageCount);
    return std::string(buffer);
}

/**
 * @brief Gets formatted percentage string
 * @param bookProgress Book progress as percentage
 * @return Formatted percentage string (e.g., "42%")
 */
std::string StatusBar::getPercentString(float bookProgress) const {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%.0f%%", bookProgress);
    return std::string(buffer);
}

/**
 * @brief Gets formatted battery percentage string
 * @return Battery percentage string
 */
std::string StatusBar::getBatteryPercentString() const {
    char buffer[16];
#ifdef HAS_BATTERY_MANAGER
    snprintf(buffer, sizeof(buffer), "%d%%", BatteryManager::getBatteryPercent());
#else
    snprintf(buffer, sizeof(buffer), "100%%");
#endif
    return std::string(buffer);
}

/**
 * @brief Gets the current chapter title
 * @param currentSpineIndex Current spine index
 * @return Chapter title
 */
std::string StatusBar::getChapterTitle(int currentSpineIndex) const {
    int tocIndex = m_epub.getTocIndexForSpineIndex(currentSpineIndex);
    if (tocIndex != -1) {
        return m_epub.getTocItem(tocIndex).title;
    }
    return "Chapter " + std::to_string(currentSpineIndex + 1);
}

/**
 * @brief Calculates overall book progress
 * @param section Current section
 * @param currentSpineIndex Current spine index
 * @return Book progress as percentage (0-100)
 */
float StatusBar::calculateBookProgress(const Section* section, int currentSpineIndex) const {
    if (!section || section->pageCount == 0) return 0;
    float spineProgress = static_cast<float>(section->currentPage) / section->pageCount;
    return m_epub.calculateProgress(currentSpineIndex, spineProgress) * 100;
}