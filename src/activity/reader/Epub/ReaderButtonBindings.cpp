/**
 * @file ReaderButtonBindings.cpp
 * @brief Definitions for ReaderButtonBindings.
 */

#include "ReaderButtonBindings.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include "EpubActivity.h"
#include "state/SystemSetting.h"

bool ReaderButtonBindings::handleInput(EpubActivity& act) {
  // Stops at the first button whose action fires this frame - if one dispatch enters a different UI
  // mode (dictionary/annotation/menu/settings), the remaining buttons shouldn't also dispatch into it
  // on the same frame. A one-frame delay in starting to track a later button's press is imperceptible
  // against the ~700ms long-press threshold.
  if (handleButton(act, MappedInputManager::Button::Up, upState_, SETTINGS.btnUpShortAction,
                   SETTINGS.btnUpLongAction)) {
    return true;
  }
  if (handleButton(act, MappedInputManager::Button::Down, downState_, SETTINGS.btnDownShortAction,
                   SETTINGS.btnDownLongAction)) {
    return true;
  }
  if (handleButton(act, MappedInputManager::Button::Left, leftState_, SETTINGS.btnLeftShortAction,
                   SETTINGS.btnLeftLongAction)) {
    return true;
  }
  if (handleButton(act, MappedInputManager::Button::Right, rightState_, SETTINGS.btnRightShortAction,
                   SETTINGS.btnRightLongAction)) {
    return true;
  }
  return false;
}

bool ReaderButtonBindings::handleButton(EpubActivity& act, const MappedInputManager::Button button,
                                        PressState& state, const uint8_t shortAction, const uint8_t longAction) {
  const bool isPressed = act.mappedInput.isPressed(button);
  const unsigned long now = millis();

  if (isPressed && !state.active) {
    state.active = true;
    state.pressStartMs = now;
    state.longFired = false;
    return false;
  }

  if (isPressed && state.active && !state.longFired && now - state.pressStartMs >= kLongPressMs) {
    state.longFired = true;
    dispatch(act, longAction);
    return true;
  }

  if (!isPressed && state.active) {
    state.active = false;
    if (!state.longFired) {
      dispatch(act, shortAction);
      return true;
    }
    return false;  // long already fired on this press - don't also fire short on release
  }

  return false;
}

void ReaderButtonBindings::dispatch(EpubActivity& act, const uint8_t action) {
  switch (action) {
    case SystemSetting::BTN_ACTION_NONE:
      break;
    case SystemSetting::BTN_ACTION_PAGE_NEXT:
      act.endPageTimer();
      act.pageTurn(true);
      act.lastAutoPageTurnTime = millis();
      break;
    case SystemSetting::BTN_ACTION_PAGE_PREVIOUS:
      act.endPageTimer();
      act.pageTurn(false);
      act.lastAutoPageTurnTime = millis();
      break;
    case SystemSetting::BTN_ACTION_OPEN_SETTINGS:
      act.pauseReadingStats();
      act.toggleSettingsDrawer();
      break;
    case SystemSetting::BTN_ACTION_TABLE_OF_CONTENTS:
      act.openTableOfContents();
      break;
    case SystemSetting::BTN_ACTION_ANNOTATE:
      act.pauseReadingStats();
      act.annUi_.enter(act);
      break;
    case SystemSetting::BTN_ACTION_DICTIONARY:
      act.pauseReadingStats();
      act.dictUi_.enter(act);
      break;
    case SystemSetting::BTN_ACTION_PAGE_REFRESH:
      act.renderer.displayBuffer(HalDisplay::MANUAL_REFRESH);
      act.updateRequired = true;
      break;
    case SystemSetting::BTN_ACTION_CHAPTER_SKIP_NEXT:
    case SystemSetting::BTN_ACTION_CHAPTER_SKIP_PREVIOUS: {
      const bool forward = action == SystemSetting::BTN_ACTION_CHAPTER_SKIP_NEXT;
      act.endPageTimer();
      bool spineAdvanced = false;
      if (forward) {
        if (act.currentSpineIndex < act.epub->getSpineItemsCount() - 1) {
          act.currentSpineIndex++;
          act.nextPageNumber = 0;
          act.section.reset();
          spineAdvanced = true;
        }
      } else if (act.currentSpineIndex > 0) {
        act.currentSpineIndex--;
        act.nextPageNumber = 0;
        act.section.reset();
        spineAdvanced = true;
      }
      if (spineAdvanced) {
        act.startPageTimer();
        act.lastAutoPageTurnTime = millis();
        act.updateRequired = true;
      }
      break;
    }
    case SystemSetting::BTN_ACTION_BOOKMARK:
      act.pauseReadingStats();
      act.addBookmark();
      act.startPageTimer();
      break;
    default:
      break;
  }
}
