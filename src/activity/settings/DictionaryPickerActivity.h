#pragma once

/**
 * @file DictionaryPickerActivity.h
 * @brief Lists StarDict folders under /dictionaries and assigns language tags.
 *
 * Every installed folder is used at lookup time. Confirm only sets the fallback for when
 * passage-language detection is unsure.
 */

#include <functional>
#include <string>
#include <vector>

#include "activity/ActivityWithSubactivity.h"
#include "dictionary/DictionaryRegistry.h"

class DictionaryPickerActivity final : public ActivityWithSubactivity {
 public:
  explicit DictionaryPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                    const std::function<void()>& goBack)
      : ActivityWithSubactivity("DictionaryPicker", renderer, mappedInput), goBack_(goBack) {}

  void onEnter() override;
  void loop() override;

 private:
  std::vector<DictionaryRegistry::Entry> entries_;
  int selectedIndex_ = 0;
  int scrollOffset_ = 0;
  const std::function<void()> goBack_;

  void scanDictionaryFolders();
  void cycleSelectedLang(int delta);
  void render();
};
