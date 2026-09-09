#pragma once

/**
 * @file Library.h
 * @brief Indexed grid Library page for the inx-pro UI migration.
 */

#include <string>
#include <unordered_set>
#include <vector>

#include "Page.h"
#include "navigation/Sidebar.h"
#include "util/LibraryIndex.h"
#include "views/Library/Grid.h"
#include "views/Library/List.h"

/** Library page shell; the legacy library body will be migrated into this page incrementally. */
class Library final : public Page {
 public:
  Library(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path = "/");

  const char* name() const override { return "Library"; }

 protected:
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void menu() override;
  void title() const override;
  void center() const override;
  bool showBattery() const override { return false; }
  void content() override;
  void navigateToSelectedMenu() override;

 private:
  static constexpr int buttonSize = UiLayout::LIBRARY_HEADER_BUTTON_SIZE;
  static constexpr int buttonGap = UiLayout::LIBRARY_HEADER_BUTTON_GAP;

  std::string path_;
  std::vector<LibraryIndex::Book> items_;
  std::unordered_set<std::string> favoritePaths_;
  views::library::Grid grid_;
  views::library::List list_;
  bool indexLoaded_ = false;
  enum class ViewMode { GRID, LIST };
  enum class SortMode { TITLE_AZ, TITLE_ZA, GROUP_AZ, GROUP_ZA, AUTHOR_AZ, AUTHOR_ZA };
  enum class FilterTab { TITLE, TYPE, OPTIONS };

  // Header order is view, sort, filter, refresh. Header focus starts on Refresh
  // after a long Back press; it is not selected during normal grid browsing.
  bool headerFocused_ = false;
  int selectedHeaderButton_ = 3;
  bool backLongPressProcessed_ = false;
  bool sortOpen_ = false;
  int sortIndex_ = 0;
  bool filterOpen_ = false;
  int filterPage_ = 0;
  int filterIndex_ = 0;
  FilterTab filterTab_ = FilterTab::TITLE;
  int typeFilterIndex_ = 0;
  std::string typeFilter_;
  bool hideFinished_ = false;
  char letterFilter_ = 0;
  SortMode sortMode_ = SortMode::TITLE_AZ;
  ViewMode viewMode_ = ViewMode::GRID;
  volatile bool isIndexing_ = false;
  volatile bool indexReloadRequested_ = false;
  volatile int indexingProgress_ = 0;
  volatile int indexingTotal_ = 0;
  bool sidebarOpen_ = false;
  int selectedItemIndex_ = 0;
  unsigned long nextItemJumpMs_ = 0;

  void loadIndexedItems();
  void startIndexing();
  void handleHeaderConfirm();
  void toggleViewMode();
  void openSortPicker();
  void handleSortInput();
  void applySortSelection();
  void renderSortPicker() const;
  void openFilterPicker();
  void handleFilterInput();
  void applyFilterSelection();
  void applyTypeFilterSelection();
  void toggleFinishedFilter();
  void moveFilterSelection(int delta);
  void renderFilterPicker() const;
  static char filterLetter(int page, int index);
  static char leadingLetter(const std::string& value);
  static const char* typeFilterLabel(int index);
  static const char* typeFilterCategory(int index);
  static int sortCount();
  static const char* sortLabel(int index);
  static const char* sortDirection(int index);
  static bool matchesTypeFilter(const std::string& path, const std::string& category);
  bool moveSelectedItem(int delta);
  int buttonX(int index) const;
  int buttonY() const;
};
