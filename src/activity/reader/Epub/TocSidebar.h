#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "activity/page/components/global/Button.h"

class Epub;
class GfxRenderer;
class MappedInputManager;

/** Reader-only, full-height sidebar for the EPUB table of contents. */
class TocSidebar {
 public:
  using Select = std::function<void(int spineIndex)>;
  using Dismiss = std::function<void()>;
  using Sync = std::function<void()>;

  TocSidebar(GfxRenderer& renderer, Select onSelect, Dismiss onDismiss, Sync onSync);

  void show(Epub* epub, int spineIndex, bool focusSync = false);
  void hide();
  bool isVisible() const { return visible; }
  void render();
  void handleInput(MappedInputManager& input);

 private:
  int width() const;
  int headerHeight() const;
  int footerHeight() const;
  int rowHeight() const;
  int visibleRows() const;
  int maxScroll() const;
  bool hasChildren(int tocIndex) const;
  void rows();
  void scrollBy(int rows);
  void moveSelection(int rows);
  void select(int tocIndex);
  ButtonBounds syncButtonBounds() const;

  GfxRenderer& renderer;
  Epub* epub = nullptr;
  Select onSelect;
  Dismiss onDismiss;
  Sync onSync;
  bool visible = false;
  bool syncFocused = false;
  bool syncLongPressHandled = false;
  int scroll = 0;
  int currentTocIndex = -1;
  int selectedVisibleIndex = 0;
  std::vector<uint8_t> levels;
  std::vector<uint8_t> collapsed;
  std::vector<int> visibleItems;
};
