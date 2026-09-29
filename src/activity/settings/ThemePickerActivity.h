#pragma once

#include <functional>

#include "activity/ActivityWithSubactivity.h"
#include "activity/page/components/widget/Recent.h"

/** Physical-button picker for the Home Flow/Grid/List presentations. */
class ThemePickerActivity final : public ActivityWithSubactivity {
 public:
  ThemePickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> onBack)
      : ActivityWithSubactivity("ThemePicker", renderer, mappedInput), recent_(renderer), onBack_(std::move(onBack)) {}

  void onEnter() override;
  void loop() override;

 private:
  void render();
  void applySelection();

  widget::Recent recent_;
  widget::Recent::Mode selected_ = widget::Recent::Mode::Flow;
  std::function<void()> onBack_;
};
