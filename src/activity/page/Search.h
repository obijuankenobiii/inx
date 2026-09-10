#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Page.h"
#include "components/search/SearchKeyboard.h"
#include "util/LibraryIndex.h"

/** Pro-style library search adapted for the button-only X3/X4 UI. */
class Search final : public Page {
 public:
  Search(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> returnToCaller);

  const char* name() const override { return "Search"; }
  void onEnter() override;
  void loop() override;

 protected:
  void title() const override;
  void menu() override;
  void content() override;
  bool back() override;

 private:
  static constexpr size_t kMaxResults = 24;

  std::function<void()> returnToCaller_;
  std::string query_;
  std::vector<LibraryIndex::Book> results_;
  SearchKeyboard keyboard_;
  size_t selectedResult_ = 0;
  int scroll_ = 0;
  bool keyboardCollapsed_ = false;
  bool nextButtonActive_ = false;
  bool nextButtonLongFired_ = false;
  unsigned long nextButtonPressStartedMs_ = 0;

  int keyboardTop() const;
  int resultsTop() const;
  int resultHeight() const;
  int maxScroll() const;
  void keepSelectedVisible();
  void updateResults();
  void openSelected();
  void drawResults() const;
};
