#pragma once

#include <string>

class GfxRenderer;

/**
 * Small, bounded on-device keyboard for the X3/X4 search page.
 *
 * It deliberately stores only cursor/mode state. Text is kept by Search and
 * capped there, so opening search cannot allocate a second library-sized data
 * structure.
 */
class SearchKeyboard {
 public:
  enum class Action { None, Collapse, Go };

  int height(const GfxRenderer& renderer) const;
  void reset();
  void render(const GfxRenderer& renderer, int top, int bottom) const;
  void moveHorizontal(int delta);
  void moveVertical(int delta);
  Action activate(std::string& value);

 private:
  int row_ = 0;
  int column_ = 0;
  bool caps_ = false;
  int mode_ = 0;
};
