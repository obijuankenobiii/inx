#pragma once

/**
 * @file MappedInputManager.h
 * @brief Public interface and types for MappedInputManager.
 */

#include <HalGPIO.h>

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  /** Labels for the physical page (side) buttons, top then bottom, per Side Button Layout setting. */
  struct SideLabels {
    const char* top;
    const char* bottom;
  };

  explicit MappedInputManager(HalGPIO& gpio) : gpio(gpio) {}

  /**
   * When true, Up/Down, Left/Right, and PageBack/PageForward are swapped before GPIO lookup.
   * Use with GfxRenderer::LandscapeClockwise (180° vs panel) so physical directions match the
   * rotated framebuffer; clear when leaving that mode or the reader.
   */
  void setInvertDirectionalAxes180(bool invert) { invertDirectionalAxes180_ = invert; }
  bool invertDirectionalAxes180() const { return invertDirectionalAxes180_; }

  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;

  /** « / » order follows which GPIO is wired as page-back vs page-forward (see Side Button Layout). */
  SideLabels mapSideLabels() const;

 private:
  HalGPIO& gpio;
  bool invertDirectionalAxes180_ = false;

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
};
