/**
 * @file StatusBar.h
 * @brief Public interface and types for StatusBar.
 */

#ifndef STATUS_BAR_H
#define STATUS_BAR_H

#include <Epub/Section.h>

#include <string>

#include "Epub.h"
#include "EpubReadingStats.h"
#include "GfxRenderer.h"
#include "state/BookSetting.h"

/**
 * @brief Manages the status bar rendering for the EPUB reader.
 *
 * Two independent bars: the main bar has its own Left/Middle/Right section config and sits within
 * the page's left/right margin, like a page footer. Full is a second bar stacked below the main bar,
 * spanning the screen edge-to-edge - it is deliberately NOT a 3-section text bar (that would just
 * duplicate the main bar); it's a single full-width loading/progress visualization restricted to
 * kFullBarStyles. A bar with no content (main: all three sections NONE; Full: style NONE) reports
 * zero height so EpubActivity::calculateViewport() reclaims its space for the book page.
 */
class StatusBar {
 public:
  /** The only items Full may hold - "bar" visualizations, not text (see SystemSetting::
   *  statusBarFullStyle). Index into this array is what the Full-bar picker UI cycles through. */
  static constexpr StatusBarItem kFullBarStyles[4] = {StatusBarItem::NONE, StatusBarItem::PROGRESS_BAR,
                                                       StatusBarItem::PROGRESS_BAR_WITH_PERCENT,
                                                       StatusBarItem::PAGE_BARS};
  static constexpr int kFullBarStyleCount = 4;

  /**
   * @brief Constructs a new StatusBar
   * @param renderer Reference to the graphics renderer
   * @param epub Reference to the EPUB document
   * @param settings Reference to the book settings
   */
  StatusBar(GfxRenderer& renderer, const Epub& epub, const BookSettings& settings,
          const EpubReadingStats* readingStats = nullptr);

  /**
   * @brief Renders the complete status bar (main bar plus the Full bar, if either has content)
   * @param section Current section being read
   * @param currentSpineIndex Current spine index
   * @param orientedMarginRight Right margin
   * @param orientedMarginBottom Bottom margin
   * @param orientedMarginLeft Left margin
   */
  void render(const Section* section, int currentSpineIndex, int orientedMarginRight, int orientedMarginBottom,
              int orientedMarginLeft) const;

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

  /** True if the Full bar's style is not NONE. */
  static bool hasFullBarContent();

  /** Pixel height to reserve for the Full bar (0 if hasFullBarContent() is false). Always additive
   *  to the normal bottom margin - see EpubActivity::calculateViewport(). */
  static int reservedFullBarHeight();

 private:
  /**
   * @brief Renders the Full bar: a single loading/progress visualization hugging the very bottom
   * edge of the screen (y = screen height, minus the panel's hardware-safe inset), spanning the
   * full edge-to-edge width - not the padded, vertically-centered text-row layout the main bar uses.
   * @param barHeight Reserved height for the bar (see reservedFullBarHeight())
   * @param section Current section
   * @param currentSpineIndex Current spine index
   */
  void renderFullBar(int barHeight, const Section* section, int currentSpineIndex) const;

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
  void renderSection(int position, int sectionStart, int sectionCenter, int sectionWidth, int textY,
                     const Section* section, int currentSpineIndex) const;

  /**
   * @brief Renders page position bars within a section
   * @param sectionStart Starting X coordinate of the section
   * @param sectionCenter Center X coordinate of the section
   * @param sectionWidth Width of the section
   * @param textY Y coordinate for text rendering
   * @param section Current section
   */
  void renderPageBars(int sectionStart, int sectionCenter, int sectionWidth, int textY, const Section* section) const;

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
  const EpubReadingStats* m_readingStats;
  bool m_visible;
};

#endif
