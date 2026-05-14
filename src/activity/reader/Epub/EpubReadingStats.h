#pragma once

#include <cstdint>
#include <string>

#include "state/Statistics.h"

class Epub;
class GfxRenderer;
class Section;

class EpubReadingStats {
 public:
  void init(Epub& epub, const Section* section, int currentSpineIndex);
  void maybeCommitSession(Epub& epub);
  void startPageTimer();
  bool hasActivePageTimer() const;
  void endPageTimer(Epub& epub, const Section* section, int currentSpineIndex);
  void addChapterRead();
  void save(Epub& epub);
  void display(GfxRenderer& renderer, Epub& epub) const;

 private:
  static std::string formatTime(uint32_t timeMs);

  BookReadingStats stats_{};
  uint32_t pageStartTime_ = 0;
  uint32_t lastSaveTime_ = 0;
  uint32_t readerSessionStartMs_ = 0;
  bool readingSessionCountCommitted_ = false;
};
