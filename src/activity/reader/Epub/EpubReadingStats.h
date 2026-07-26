#pragma once

#include <cstdint>
#include <string>

#include "state/Statistics.h"

class Epub;
class GfxRenderer;
class Section;

class EpubReadingStats {
 public:
  void init(const Epub& epub, const Section* section, int currentSpineIndex);
  void maybeCommitSession(const Epub& epub);
  void startPageTimer();
  bool hasActivePageTimer() const;
  void pausePageTimer(const Epub& epub, const Section* section, int currentSpineIndex);
  void endPageTimer(const Epub& epub, const Section* section, int currentSpineIndex);
  void addChapterRead();
  void save(const Epub& epub);
  void display(GfxRenderer& renderer, const Epub& epub) const;

  /**
   * @brief Returns a compact "time left in chapter" string for the status bar.
   * @param section Current section being read.
   * @return Formatted string (e.g. "1h 20m", "12m"), or "-" when no reliable estimate exists.
   */
  std::string chapterTimeLeftString(const Section* section) const;

  /**
   * @brief Returns a compact "time left in book" string for the status bar.
   * @return Formatted string (e.g. "3h 45m", "1h"), or "-" when no reliable estimate exists.
   */
  std::string bookTimeLeftString() const;

 private:
  static constexpr uint32_t kMinPageDwellMs = 5000;
  static constexpr uint32_t kMinPaceSamples = 3;
  static std::string formatTime(uint32_t timeMs);

  BookReadingStats stats_{};
  uint32_t pageStartTime_ = 0;
  uint32_t lastSaveTime_ = 0;
  uint32_t activeSessionTimeMs_ = 0;
  bool readingSessionCountCommitted_ = false;
};
