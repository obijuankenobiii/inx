#pragma once

/**
 * @file MenuNav.h
 * @brief Resolves the physical buttons used for main-menu tab vs item navigation, based on the
 *        SystemSetting::mainMenuNav (front buttons = Left/Right tabs + Up/Down items; side buttons swap them).
 *
 * Every menu screen reads its directional input through these so the whole main menu honors the setting.
 * In FRONT mode these return the identity buttons (no behavior change); SIDE mode swaps the two axes.
 */

#include "MappedInputManager.h"
#include "state/SystemSetting.h"

namespace MenuNav {

inline bool sideButtons() { return SETTINGS.mainMenuNav == SystemSetting::MAIN_MENU_NAV_SIDE; }

/** "Move to previous item" (default Up; side mode Left). */
inline MappedInputManager::Button itemPrev() {
  return sideButtons() ? MappedInputManager::Button::Left : MappedInputManager::Button::Up;
}
/** "Move to next item" (default Down; side mode Right). */
inline MappedInputManager::Button itemNext() {
  return sideButtons() ? MappedInputManager::Button::Right : MappedInputManager::Button::Down;
}
/** "Previous tab / page" (default Left; side mode Up). */
inline MappedInputManager::Button tabPrev() {
  return sideButtons() ? MappedInputManager::Button::Up : MappedInputManager::Button::Left;
}
/** "Next tab / page" (default Right; side mode Down). */
inline MappedInputManager::Button tabNext() {
  return sideButtons() ? MappedInputManager::Button::Down : MappedInputManager::Button::Right;
}

}  // namespace MenuNav
