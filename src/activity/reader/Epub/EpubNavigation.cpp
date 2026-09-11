#include "EpubNavigation.h"

#include <Epub.h>
#include <Epub/Section.h>

#include "EpubActivity.h"
#include "KOReaderSyncActivity.h"
#include "ProgressMapper.h"
#include "TocSidebar.h"

EpubNavigation::EpubNavigation(EpubActivity& activity) : activity_(activity) {}

EpubNavigation::~EpubNavigation() = default;

bool EpubNavigation::isTocOpen() const { return tocSidebar_ && tocSidebar_->isVisible(); }

bool EpubNavigation::handleInput(MappedInputManager& input) {
  if (!isTocOpen()) return false;
  tocSidebar_->handleInput(input);
  return true;
}

void EpubNavigation::render() {
  if (isTocOpen()) tocSidebar_->render();
}

void EpubNavigation::reset() {
  if (tocSidebar_) {
    tocSidebar_->hide();
    tocSidebar_.reset();
  }
}

void EpubNavigation::openTableOfContents(const bool focusSync) {
  if (!activity_.epub) return;

  if (!tocSidebar_) {
    tocSidebar_.reset(new TocSidebar(
        activity_.renderer, [this](const int spineIndex) { onTocChapterSelected(spineIndex); },
        [this]() { onDrawerDismissed(); }, [this]() { onKoreaderSyncRequested(); }));
  }

  activity_.pauseReadingStats();
  tocSidebar_->show(activity_.epub.get(), activity_.currentSpineIndex, focusSync);
}

void EpubNavigation::onKoreaderSyncRequested() {
  if (!activity_.epub || !activity_.section) return;

  const std::shared_ptr<Epub> sharedEpub(activity_.epub.get(), [](Epub*) {});
  PagePosition localPosition{};
  localPosition.spineIndex = activity_.currentSpineIndex;
  localPosition.pageNumber = activity_.section->currentPage;
  localPosition.totalPages = activity_.section->pageCount;
  const KOReaderPosition localProgress = ProgressMapper::toKOReader(sharedEpub, localPosition);

  activity_.enterNewActivity(new KOReaderSyncActivity(
      activity_.renderer, activity_.mappedInput, sharedEpub, activity_.epub->getPath(),
      activity_.currentSpineIndex, activity_.section->currentPage, activity_.section->pageCount, localProgress,
      activity_.getCurrentChapterTitle(),
      [this]() {
        activity_.exitActivity();
        activity_.updateRequired = true;
        activity_.startPageTimer();
      },
      [this](const int spineIndex, const int pageNumber) {
        activity_.exitActivity();
        activity_.currentSpineIndex = spineIndex;
        activity_.nextPageNumber = pageNumber;
        activity_.section.reset();
        activity_.updateRequired = true;
        activity_.startPageTimer();
      }));
}

void EpubNavigation::onDrawerDismissed() {
  activity_.updateRequired = true;
  activity_.startPageTimer();
}

void EpubNavigation::onTocChapterSelected(const int spineIndex) {
  activity_.currentSpineIndex = spineIndex;
  activity_.nextPageNumber = 0;
  activity_.section.reset();
  activity_.updateRequired = true;
  activity_.startPageTimer();
}
