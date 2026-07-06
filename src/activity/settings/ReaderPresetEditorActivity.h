#pragma once

/**
 * @file ReaderPresetEditorActivity.h
 * @brief Full-screen editor for a single reader settings preset.
 *
 * Top half: a live lorem-ipsum preview rendered with the preset's font/size/spacing/alignment/margins,
 * plus a representative status bar. Bottom half: an embedded SettingsDrawer bound to the working
 * BookSettings (its category groups are the "category selection"); any change live-updates the preview.
 * Naming a brand-new preset is done via the on-screen keyboard when leaving the editor.
 */

#include <functional>
#include <memory>
#include <string>

#include "activity/ActivityWithSubactivity.h"
#include "state/BookSetting.h"

class SettingsDrawer;

class ReaderPresetEditorActivity final : public ActivityWithSubactivity {
 public:
  /**
   * @param presetIndex Store index to edit, or -1 to create a new preset (seeded from Default).
   * @param onDone Invoked after the editor has saved/cancelled and wants the parent to dismiss it.
   */
  ReaderPresetEditorActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, int presetIndex,
                             std::function<void()> onDone);
  ~ReaderPresetEditorActivity() override;

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  void renderPreview();
  void renderPreviewStatusBar(int barTop, int barHeight);
  void beginExit();
  void doSaveAndFinish();
  void promptName();

  int presetIndex_;
  bool isNew_;
  std::function<void()> onDone_;

  BookSettings working_;
  std::string name_;

  std::unique_ptr<SettingsDrawer> drawer_;
  int previewHeight_ = 0;

  bool finishRequested_ = false;  ///< Deferred teardown of the keyboard sub-activity then save
  uint32_t enteredAtMs_ = 0;
};
