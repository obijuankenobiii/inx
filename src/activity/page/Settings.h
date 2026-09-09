#pragma once

/**
 * @file Settings.h
 * @brief Flat settings page with the inx-pro three-tab shell.
 */

#include "Page.h"

#include <functional>
#include <memory>

class CategorySettingsActivity;
class ReaderPresetsActivity;

enum class SettingsPanel : uint8_t { System, Reader, Presets };

/** Hosts the existing X3/X4 setting handlers inside the new page chrome. */
class Settings final : public Page {
 public:
  Settings(GfxRenderer& renderer, MappedInputManager& mappedInput);

  const char* name() const override { return "Settings"; }
  void onEnter() override;
  void onExit() override;
  void loop() override;

 protected:
  void title() const override;
  void content() override;
  void menu() override;
  bool showBattery() const override { return false; }
  bool back() override;

 private:
  enum class Pending { None, SwitchPanel };

  void openPanel();
  void closePanel();
  void requestPanelSwitch();
  void processPending();
  bool runExternalNavigation();
  void deferExternalNavigation(const std::function<void()>& action);
  bool panelDetailOpen() const;
  bool panelSubPageOpen() const;
  bool panelOverlayOpen() const;
  void panelTabs();

  std::unique_ptr<Activity> panel;
  CategorySettingsActivity* categoryPanel = nullptr;
  ReaderPresetsActivity* readerPanel = nullptr;
  ReaderPresetsActivity* presetsPanel = nullptr;
  SettingsPanel currentPanel = SettingsPanel::System;
  SettingsPanel nextPanel = SettingsPanel::System;
  Pending pending = Pending::None;
  std::function<void()> externalNavigation;
};
