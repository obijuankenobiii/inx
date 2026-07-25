#pragma once

/**
 * @file OrientationPickerUi.h
 * @brief Popup for the "Change Orientation" button action - lets the reader pick one of the 4 screen
 *        orientations without going through Book Settings, same overlay style as the popup selectors
 *        in ReaderPresetsActivity/CategorySettingsActivity.
 *
 * Self-contained wrapper (same shape as EpubDictionaryUi/EpubAnnotationUi: a `friend class` of
 * EpubActivity that reaches back into its private bookSettings/isToggleClosed) so this popup doesn't
 * spread through EpubActivity.cpp. Unlike EpubDictionaryUi/EpubAnnotationUi, this doesn't capture the
 * framebuffer as a backdrop - the popup box is the only thing that needs repainting while open, and
 * closing it (either applying a new orientation or cancelling) always ends in a full reader redraw
 * that naturally erases it.
 */

class EpubActivity;

class OrientationPickerUi {
 public:
  bool isActive() const { return mode_; }

  void enter(EpubActivity& act);
  void handleInput(EpubActivity& act);

 private:
  void render(EpubActivity& act);

  bool mode_ = false;
  int selected_ = 0;
};
