#pragma once

#include <functional>
#include "activity/ActivityWithSubactivity.h"

class EpubPercentSelectionActivity final : public ActivityWithSubactivity {
 public:
  using OnCancelCallback = std::function<void()>;
  using OnPercentSelectedCallback = std::function<void(int percent)>;

  explicit EpubPercentSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        int initialPercent,
                                        OnCancelCallback onCancel,
                                        OnPercentSelectedCallback onPercentSelected)
      : ActivityWithSubactivity("EpubPercentSelection", renderer, mappedInput),
        percent(initialPercent),
        onCancel(std::move(onCancel)),
        onPercentSelected(std::move(onPercentSelected)),
        lastStepTime(0) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  int percent;
  OnCancelCallback onCancel;
  OnPercentSelectedCallback onPercentSelected;
  unsigned long lastStepTime;

  void adjustPercent(int delta);
  void render();
};
