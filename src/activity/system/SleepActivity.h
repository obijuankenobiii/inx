#pragma once

/**
 * @file SleepActivity.h
 * @brief Public interface and types for SleepActivity.
 */

#include "../Activity.h"
#include <memory> 

class Bitmap;

/**
 * @brief Sleep activity that displays various sleep screens when the device is idle.
 * 
 * The SleepActivity manages different sleep screen modes including blank screen,
 * transparent overlay, custom images, book covers, and a default Corgi sleep screen.
 * Supports bitmap rendering, scaling, cropping, and grayscale filtering options.
 */
class SleepActivity final : public Activity {
 public:
  /**
   * @brief Constructs the sleep activity.
   * 
   * @param renderer Graphics renderer for drawing the sleep screen
   * @param mappedInput Input manager for handling wake-up events
   */
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Sleep", renderer, mappedInput) {}
  
  /**
   * @brief Initializes and renders the sleep screen when activity becomes active.
   * 
   * Selects and renders the appropriate sleep screen based on the current
   * sleep screen mode setting.
   */
  void onEnter() override;

 private:
  /**
   * @brief Renders the default sleep screen with Corgi logo.
   * 
   * Displays the CorgiSleep logo centered on the screen with optional inversion
   * based on sleep screen mode settings.
   */
  void renderDefaultSleepScreen() const;
  
  /**
   * @brief Renders a custom sleep screen from user-provided images.
   * 
   * Loads BMP/JPG/JPEG from /sleep/ or root sleep image (fixed choice in settings, or random).
   * Falls back to default sleep screen if no images are found.
   */
  void renderCustomSleepScreen() const;
  
  /**
   * @brief Renders the cover of the last opened book as sleep screen.
   * 
   * Extracts and displays the cover image from the most recently opened book
   * (EPUB, XTC, or TXT format). Applies cropping or scaling based on settings.
   */
  void renderCoverSleepScreen() const;
  
  /**
   * @brief Renders a bitmap image as the sleep screen with proper positioning.
   *
   * Handles image scaling, centering, cropping, and optional grayscale rendering
   * on screen dimensions and user settings.
   *
   * @param bitmap The bitmap image to render
   * @param preCroppedEpubCover When true (EPUB + Crop mode), bitmap is the pre-cropped cover; draw at full screen with no aspect crop.
   */
  void renderBitmapSleepScreen(const Bitmap& bitmap, bool preCroppedEpubCover = false) const;

  /** Fill mode: aspect crop + `drawBitmap` (same scaling as reader / CrossPoint-style covers). */
  void renderFill(const Bitmap& bitmap) const;
  
  /**
   * @brief Renders a completely blank sleep screen.
   * 
   * Clears the screen to save power and prevent screen burn-in during sleep.
   */
  void renderBlankSleepScreen() const;
  
  /**
   * @brief Renders a transparent overlay sleep screen.
   *
   * For the last-read EPUB, draws the saved reading page (progress) as the base layer, then the
   * semi-transparent sleep BMP on top when configured. Non-EPUB books still use the cover bitmap as the base.
   */
  void renderTransparentSleepScreen() const;
};