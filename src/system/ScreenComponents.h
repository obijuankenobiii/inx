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

  struct PopupLayout {
    int x;
    int y;
    int width;
    int height;
  };

  static void drawBattery(const GfxRenderer& renderer, int left, int top, bool showPercentage = true);
  static void drawBookProgressBar(const GfxRenderer& renderer, size_t bookProgress);

  static PopupLayout drawPopup(const GfxRenderer& renderer, const char* message);

  static void fillPopupProgress(const GfxRenderer& renderer, const PopupLayout& layout, int progress);

  
  
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
