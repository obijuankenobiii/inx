#pragma once

#include <EpdFontFamily.h>

#include "Page.h"

#include <functional>
#include <string>
#include <vector>

struct DescriptionLine {
  struct Run {
    std::string text;
    EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  };
  std::vector<Run> runs;
};

/** Full-page, button-navigable view of a recent book's metadata description. */
class HomeDescription final : public Page {
 public:
  HomeDescription(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                  std::function<void()> onBack);

  const char* name() const override { return "Description"; }
  void onEnter() override;
  void loop() override;

 protected:
  void title() const override;
  void menu() override;
  void content() override;
  bool back() override;

 private:
  void loadDescription();
  void paginate();
  void movePage(int delta);

  std::string bookPath_;
  std::function<void()> onBack_;
  std::string description_;
  std::vector<DescriptionLine> lines_;
  int page_ = 0;
  int pageCount_ = 1;
};
