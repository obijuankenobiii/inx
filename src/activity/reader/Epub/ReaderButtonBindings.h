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

  /** Fires a single READER_BUTTON_ACTION immediately, bypassing press-state tracking. Public so
   *  EpubActivity's Power button (short-press only, no long-press pairing so it doesn't need
   *  handleButton()'s state machine) can reuse the same 12-action dispatch as Up/Down/Left/Right
   *  instead of duplicating a subset of it. */
  void dispatch(EpubActivity& act, uint8_t action);

 private:
  struct PressState {
    bool active = false;
    unsigned long pressStartMs = 0;
    bool longFired = false;
  };

  bool handleButton(EpubActivity& act, MappedInputManager::Button button, PressState& state, uint8_t shortAction,
                    uint8_t longAction);

  PressState upState_;
  PressState downState_;
  PressState leftState_;
  PressState rightState_;

  // Matches the existing chapter-skip long-press threshold (EpubActivity's skipChapterMs) for a
  // consistent feel with the button this replaces.
  static constexpr unsigned long kLongPressMs = 700;
};
