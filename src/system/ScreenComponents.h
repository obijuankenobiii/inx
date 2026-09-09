#pragma once

/**
 * @file ScreenComponents.h
 * @brief Public interface and types for ScreenComponents.
 */

#include <cstddef>
#include <cstdint>
#include <vector>

class GfxRenderer;

struct TabInfo {
  const char* label;
  bool selected;
};

class ScreenComponents {
 public:
  static const int BOOK_PROGRESS_BAR_HEIGHT = 4;

  static constexpr int BATTERY_ICON_WIDTH = 28;
  static constexpr int BATTERY_ICON_HEIGHT = 14;
  static constexpr int BATTERY_TEXT_GAP = 5;
  static constexpr int BATTERY_ICON_TOP_OFFSET = 3;

  struct PopupLayout {
    int x;
    int y;
    int width;
    int height;
  };

  static void drawBattery(const GfxRenderer& renderer, int left, int top, bool showPercentage = true);
  static bool drawMenuClock(const GfxRenderer& renderer, int left, int top);
  static void drawMenuClockAndBattery(const GfxRenderer& renderer, int batteryLeft, int top,
                                      bool showBatteryPercentage = true);
  static void drawBookProgressBar(const GfxRenderer& renderer, size_t bookProgress);

  /**
   * Compact bottom status chip for short blocking work (e.g. "Opening dictionary...", "Closing book").
   * Uses a fast refresh so it feels like a toast rather than a full-page modal.
   */
  static PopupLayout drawPopup(const GfxRenderer& renderer, const char* message);

  static void fillPopupProgress(const GfxRenderer& renderer, const PopupLayout& layout, int progress);

  /** Geometry for {@link LoadingProgress}: bottom strip with label + slim progress bar. */
  struct LoadingProgressLayout {
    int panelX = 0;
    int panelY = 0;
    int panelW = 0;
    int panelH = 0;
    int barX = 0;
    int barY = 0;
    int barW = 0;
    int barH = 0;
  };

  /**
   * Bottom status strip with a progress bar (layout rebuilds, chapter loads, stats, etc.).
   * Intentionally not a center modal — keeps the page context visible during longer work.
   */
  struct LoadingProgress {
    static LoadingProgressLayout show(const GfxRenderer& renderer, const char* message, int progressPercent0to100);
    static void setProgress(const GfxRenderer& renderer, const LoadingProgressLayout& layout,
                            int progressPercent0to100);
  };

  static int drawTabBar(const GfxRenderer& renderer, int y, const std::vector<TabInfo>& tabs);

  static void drawScrollIndicator(const GfxRenderer& renderer, int currentPage, int totalPages, int contentTop,
                                  int contentHeight);

  /**
   * Draw a progress bar with percentage text.
   * @param renderer The graphics renderer
   * @param x Left position of the bar
   * @param y Top position of the bar
   * @param width Width of the bar
   * @param height Height of the bar
   * @param current Current progress value
   * @param total Total value for 100% progress
   */
  static void drawProgressBar(const GfxRenderer& renderer, int x, int y, int width, int height, size_t current,
                              size_t total);
};
