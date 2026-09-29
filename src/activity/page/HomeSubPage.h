#pragma once

#include "Page.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

/** Pro-style Home shortcut destination for bookmarks, highlights, favorites and saved words. */
class HomeSubPage final : public Page {
 public:
  enum class Section { Bookmarks, Highlights, Favorites, Dictionary };

  HomeSubPage(GfxRenderer& renderer, MappedInputManager& mappedInput, Section section,
              std::function<void()> onBack);

 protected:
  void onEnter() override;
  void loop() override;
  void menu() override;
  void content() override;

 private:
  struct Row {
    Row() = default;
    Row(std::string rowLabel, std::string rowPath = {}, std::string rowCache = {}, int rowSpine = -1,
        int rowPage = -1, bool rowDictionary = false, int rowDictionaryIndex = -1)
        : label(std::move(rowLabel)), path(std::move(rowPath)), cachePath(std::move(rowCache)), spine(rowSpine),
          page(rowPage), dictionary(rowDictionary), dictionaryIndex(rowDictionaryIndex) {}

    std::string label;
    std::string path;
    std::string cachePath;
    std::string annotationText;
    int spine = -1;
    int page = -1;
    bool dictionary = false;
    int dictionaryIndex = -1;
  };

  Section section_;
  std::function<void()> onBack_;
  std::vector<Row> rows_;
  int selected_ = 0;
  int popupAction_ = 0;
  bool popupOpen_ = false;
  bool longPressHandled_ = false;

  void load();
  void deleteSelected();
  void openSelected();
  bool isDeletable() const;
  const char* pageTitle() const;
  const char* emptyLabel() const;
  bool popupInput();
  void renderPopup() const;
};
