/**
 * @file MappedInputManager.cpp
 * @brief Definitions for MappedInputManager.
 */

#include "system/MappedInputManager.h"

#include "state/SystemSetting.h"

namespace {
using ButtonIndex = uint8_t;

struct FrontLayoutMap {
  ButtonIndex back;
  ButtonIndex confirm;
  ButtonIndex left;
  ButtonIndex right;
};

struct SideLayoutMap {
  ButtonIndex pageBack;
  ButtonIndex pageForward;
};


constexpr FrontLayoutMap kFrontLayouts[] = {
    {HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM, HalGPIO::BTN_LEFT, HalGPIO::BTN_RIGHT},
    {HalGPIO::BTN_LEFT, HalGPIO::BTN_RIGHT, HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM},
    {HalGPIO::BTN_CONFIRM, HalGPIO::BTN_LEFT, HalGPIO::BTN_BACK, HalGPIO::BTN_RIGHT},
    {HalGPIO::BTN_BACK, HalGPIO::BTN_CONFIRM, HalGPIO::BTN_RIGHT, HalGPIO::BTN_LEFT},
};


constexpr SideLayoutMap kSideLayouts[] = {
    {HalGPIO::BTN_UP, HalGPIO::BTN_DOWN},
    {HalGPIO::BTN_DOWN, HalGPIO::BTN_UP},
};

MappedInputManager::Button remapDirectional180(const MappedInputManager::Button button) {
  switch (button) {
    case MappedInputManager::Button::Up:
      return MappedInputManager::Button::Down;
    case MappedInputManager::Button::Down:
      return MappedInputManager::Button::Up;
    case MappedInputManager::Button::Left:
      return MappedInputManager::Button::Right;
    case MappedInputManager::Button::Right:
      return MappedInputManager::Button::Left;
    case MappedInputManager::Button::PageBack:
      return MappedInputManager::Button::PageForward;
    case MappedInputManager::Button::PageForward:
      return MappedInputManager::Button::PageBack;
    default:
      return button;
  }
}
}  

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const) const {
  const auto frontLayout = static_cast<SystemSetting::FRONT_BUTTON_LAYOUT>(SETTINGS.frontButtonLayout);
  const auto sideLayout = static_cast<SystemSetting::SIDE_BUTTON_LAYOUT>(SETTINGS.sideButtonLayout);
  const auto& front = kFrontLayouts[frontLayout];
  const auto& side = kSideLayouts[sideLayout];

  const Button effective = invertDirectionalAxes180_ ? remapDirectional180(button) : button;

  switch (effective) {
    case Button::Back:
      return (gpio.*fn)(front.back);
    case Button::Confirm:
      return (gpio.*fn)(front.confirm);
    case Button::Left:
      return (gpio.*fn)(front.left);
    case Button::Right:
      return (gpio.*fn)(front.right);
    case Button::Up:
      return (gpio.*fn)(HalGPIO::BTN_UP);
    case Button::Down:
      return (gpio.*fn)(HalGPIO::BTN_DOWN);
    case Button::Power:
      return (gpio.*fn)(HalGPIO::BTN_POWER);
    case Button::PageBack:
      return (gpio.*fn)(side.pageBack);
    case Button::PageForward:
      return (gpio.*fn)(side.pageForward);
  }

  return false;
}

bool MappedInputManager::wasPressed(const Button button) const { return mapButton(button, &HalGPIO::wasPressed); }

bool MappedInputManager::wasReleased(const Button button) const { return mapButton(button, &HalGPIO::wasReleased); }

bool MappedInputManager::isPressed(const Button button) const { return mapButton(button, &HalGPIO::isPressed); }

bool MappedInputManager::wasAnyPressed() const { return gpio.wasAnyPressed(); }

bool MappedInputManager::wasAnyReleased() const { return gpio.wasAnyReleased(); }

unsigned long MappedInputManager::getHeldTime() const { return gpio.getHeldTime(); }

MappedInputManager::SideLabels MappedInputManager::mapSideLabels() const {
  const auto sideLayout = static_cast<SystemSetting::SIDE_BUTTON_LAYOUT>(SETTINGS.sideButtonLayout);
  
  static constexpr const char* kPrev = "\xC2\xAB";
  static constexpr const char* kNext = "\xC2\xBB";
  if (sideLayout == SystemSetting::NEXT_PREV) {
    return {kNext, kPrev};
  }
  return {kPrev, kNext};
}

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  const auto layout = static_cast<SystemSetting::FRONT_BUTTON_LAYOUT>(SETTINGS.frontButtonLayout);

  switch (layout) {
    case SystemSetting::LEFT_RIGHT_BACK_CONFIRM:
      return {previous, next, back, confirm};
    case SystemSetting::LEFT_BACK_CONFIRM_RIGHT:
      return {previous, back, confirm, next};
    case SystemSetting::BACK_CONFIRM_RIGHT_LEFT:
      return {back, confirm, next, previous};
    case SystemSetting::BACK_CONFIRM_LEFT_RIGHT:
    default:
      return {back, confirm, previous, next};
  }
}