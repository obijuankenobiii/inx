#pragma once

/**
 * @file ReaderButtonBindings.h
 * @brief Per-button (Up/Down/Left/Right) short-press/long-press action mapping for the EPUB reader.
 *
 * Self-contained wrapper (same shape as EpubDictionaryUi/EpubAnnotationUi: a `friend class` of
 * EpubActivity that reaches back into its private helpers) so this dispatch logic doesn't spread
 * through EpubActivity.cpp. Supersedes the old scattered readerDirectionMapping (which axis is
 * prev/next) / longPressChapterSkip (single global long-press mode) / readerMenuButton (one button
 * hardcoded to open settings) settings - every button now gets an independent, explicit short+long
 * action from SystemSetting::READER_BUTTON_ACTION.
 */

#include <array>
#include <cstdint>

#include "system/MappedInputManager.h"

class EpubActivity;

class ReaderButtonBindings {
 public:
  ReaderButtonBindings() = default;

  /** Call once per loop, after any chord-entry/active-overlay checks (so a held Down that becomes a
   *  dictionary/annotation chord never also fires a plain Down binding). Returns true if a button's
   *  mapped action fired this call. */
  bool handleInput(EpubActivity& act);

 private:
  struct PressState {
    bool active = false;
    unsigned long pressStartMs = 0;
    bool longFired = false;
  };

  bool handleButton(EpubActivity& act, MappedInputManager::Button button, PressState& state, uint8_t shortAction,
                    uint8_t longAction);
  void dispatch(EpubActivity& act, uint8_t action);

  PressState upState_;
  PressState downState_;
  PressState leftState_;
  PressState rightState_;

  // Matches the existing chapter-skip long-press threshold (EpubActivity's skipChapterMs) for a
  // consistent feel with the button this replaces.
  static constexpr unsigned long kLongPressMs = 700;
};
