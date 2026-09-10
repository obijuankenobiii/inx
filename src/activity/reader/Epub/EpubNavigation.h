#pragma once

#include <memory>

class EpubActivity;
class TocSidebar;
class MappedInputManager;

/** Owns the reader table-of-contents surface and its KOReader action. */
class EpubNavigation {
 public:
  explicit EpubNavigation(EpubActivity& activity);
  ~EpubNavigation();

  EpubNavigation(const EpubNavigation&) = delete;
  EpubNavigation& operator=(const EpubNavigation&) = delete;

  bool handleInput(MappedInputManager& input);
  void render();
  void reset();
  bool isTocOpen() const;

  void openTableOfContents(bool focusSync = false);

 private:
  void onDrawerDismissed();
  void onTocChapterSelected(int spineIndex);
  void onKoreaderSyncRequested();

  EpubActivity& activity_;
  std::unique_ptr<TocSidebar> tocSidebar_;
};
