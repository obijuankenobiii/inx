/**
 * @file Settings.cpp
 * @brief Flat settings page with the inx-pro three-tab shell.
 */

#include "Settings.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "activity/page/navigation/Menu.h"
#include "activity/page/components/global/Button.h"
#include "activity/settings/CategorySettingsActivity.h"
#include "activity/settings/ReaderPresetsActivity.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/UiLayout.h"

extern void onGoToRecent();
extern void onGoToLibrary(const std::string& path);
extern void onGoToFileTransfer();
extern void openSearchFromCallback(std::function<void()> returnToCaller);

namespace {

constexpr int kPanelTabY = navigation::Menu::height + 20;
constexpr int kPanelTabHeight = Button::height - 10;
constexpr int kPanelTabWidth = 120;
constexpr int kPanelTabRight = 20;
constexpr int kPanelTabCorner = 4;

int presetsX(const GfxRenderer& renderer) { return renderer.getScreenWidth() - kPanelTabRight - kPanelTabWidth; }
int readerX(const GfxRenderer& renderer) { return presetsX(renderer) - kPanelTabWidth; }
int systemX(const GfxRenderer& renderer) { return readerX(renderer) - kPanelTabWidth; }

void renderPanelTab(const GfxRenderer& renderer, int x, int y, int width, int height, const char* label,
                    bool selected, bool roundLeft, bool roundRight) {
  const int tone = selected ? static_cast<int>(GfxRenderer::FillTone::Ink)
                            : static_cast<int>(GfxRenderer::FillTone::Paper);
  renderer.rectangle.fill(x, y, width, height, tone, false);
  renderer.rectangle.render(x, y, width, height, true, false);

  // Match the Pro tab geometry, including the small antialiased-looking arc pixels at each outer corner.
  auto clearOutsideCorner = [&](const int cornerX, const int cornerY, const bool left, const bool top) {
    for (int dy = 0; dy <= kPanelTabCorner; ++dy) {
      for (int dx = 0; dx <= kPanelTabCorner; ++dx) {
        const int distanceX = left ? dx - kPanelTabCorner : dx;
        const int distanceY = top ? dy - kPanelTabCorner : dy;
        if (distanceX * distanceX + distanceY * distanceY > kPanelTabCorner * kPanelTabCorner) {
          renderer.drawPixel(cornerX + dx, cornerY + dy, false);
        }
      }
    }

    for (int offset = 0; offset <= kPanelTabCorner; ++offset) {
      const int span = static_cast<int>(std::sqrt(kPanelTabCorner * kPanelTabCorner -
                                                   (kPanelTabCorner - offset) * (kPanelTabCorner - offset)));
      const int arcX = left ? cornerX + kPanelTabCorner - span : cornerX + span;
      const int arcY = top ? cornerY + offset : cornerY + kPanelTabCorner - offset;
      renderer.drawPixel(arcX, arcY, true);
    }
  };
  if (roundLeft) {
    clearOutsideCorner(x, y, true, true);
    clearOutsideCorner(x, y + height - kPanelTabCorner - 1, true, false);
  }
  if (roundRight) {
    clearOutsideCorner(x + width - kPanelTabCorner - 1, y, false, true);
    clearOutsideCorner(x + width - kPanelTabCorner - 1, y + height - kPanelTabCorner - 1, false, false);
  }

  const int font = systemFontId();
  const int textWidth = renderer.text.getWidth(font, label, EpdFontFamily::REGULAR);
  const int textY = y + (height - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, x + (width - textWidth) / 2, textY, label, !selected, EpdFontFamily::REGULAR);
}

std::vector<SettingInfo> buildSystemSettings(const bool x3) {
  std::vector<SettingInfo> settings;
  settings.reserve(x3 ? 42 : 35);

  // This list intentionally matches the current inx device schema. Pro-only fields are not added here.
  settings.push_back(SettingInfo::Separator("Display ", GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Enum(
      "Sleep Screen", &SystemSetting::sleepScreen,
      x3 ? std::vector<std::string>{"Dark", "Light", "Custom", "Recent Book", "Transparent Cover", "None", "Date Time"}
         : std::vector<std::string>{"Dark", "Light", "Custom", "Recent Book", "Transparent Cover", "None"},
      GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Action("Choose sleep image", GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Enum("Hide Battery %", &SystemSetting::hideBatteryPercentage,
                                       {"Never", "In Reader", "Always"}, GroupType::DEVICE_DISPLAY));
  settings.push_back(SettingInfo::Enum("Text size", &SystemSetting::systemTextSize,
                                       {"Small", "Medium", "Large"}, GroupType::DEVICE_DISPLAY));
  // Theme is a separate top-level settings page, not a Display option.
  settings.push_back(SettingInfo::Separator("Theme", GroupType::THEME));

  if (x3) {
    settings.push_back(SettingInfo::Separator("Clock", GroupType::CLOCK));
    settings.push_back(SettingInfo::Toggle("Show Clock", &SystemSetting::showMenuClock, GroupType::CLOCK));
    settings.push_back(SettingInfo::Action("Face", GroupType::CLOCK));
    settings.push_back(SettingInfo::Enum("Format", &SystemSetting::sleepClockTimeFormat, {"12 hour", "24 hour"},
                                         GroupType::CLOCK));
    settings.push_back(SettingInfo::Action("Sync", GroupType::CLOCK));
  }

  settings.push_back(SettingInfo::Separator("Image", GroupType::IMAGE));
  settings.push_back(SettingInfo::Enum("Cover Mode", &SystemSetting::sleepScreenCoverMode, {"Fill", "Crop"},
                                       GroupType::IMAGE));
  settings.push_back(SettingInfo::Enum("Cover Filter", &SystemSetting::sleepScreenCoverFilter,
                                       {"None", "Contrast", "Inverted"}, GroupType::IMAGE));
  settings.push_back(SettingInfo::Enum("Sleep Image Quality", &SystemSetting::sleepImageQuality,
                                       {"Low", "Medium", "High"}, GroupType::IMAGE));
  settings.push_back(SettingInfo::Enum("Thumbnail corners", &SystemSetting::bitmapRoundedCorners,
                                       {"Square", "Rounded", "Subtle"}, GroupType::IMAGE));

  settings.push_back(SettingInfo::Separator("Buttons", GroupType::DEVICE_BUTTONS));
  settings.push_back(SettingInfo::Enum("Front Button", &SystemSetting::frontButtonLayout,
                                       {"Back, Confirm, Left, Right", "Left, Right, Back, Confirm",
                                        "Left, Back, Confirm, Right", "Back, Confirm, Right, Left",
                                        "Left, Right, Confirm, Back"},
                                       GroupType::DEVICE_BUTTONS));
  settings.push_back(SettingInfo::Enum("Short Power Button Click", &SystemSetting::shortPwrBtn,
                                       {"Ignore", "Sleep", "Page Refresh"}, GroupType::DEVICE_BUTTONS));
  settings.push_back(SettingInfo::Enum("Main Menu Buttons", &SystemSetting::mainMenuNav,
                                       {"Front (Left/Right)", "Side (Up/Down)"}, GroupType::DEVICE_BUTTONS));
  if (x3) {
    settings.push_back(SettingInfo::Enum("Flick page turn", &SystemSetting::shakePageTurn,
                                         {"Off", "Normal", "Inverted"}, GroupType::DEVICE_BUTTONS));
    settings.push_back(SettingInfo::Enum("Flick sensitivity", &SystemSetting::shakePageTurnSensitivity,
                                         {"Low", "Normal", "High"}, GroupType::DEVICE_BUTTONS));
  }

  settings.push_back(SettingInfo::Separator("Device ", GroupType::DEVICE_ADVANCED));
  settings.push_back(SettingInfo::Enum("Time to Sleep", &SystemSetting::sleepTimeout,
                                       {"1 min", "5 min", "10 min", "15 min", "30 min"}, GroupType::DEVICE_ADVANCED));
  settings.push_back(SettingInfo::Enum("Boot Mode", &SystemSetting::bootSetting, {"Recent Books", "Home Page"},
                                       GroupType::DEVICE_ADVANCED));

  settings.push_back(SettingInfo::Separator("Actions", GroupType::DEVICE_ACTIONS));
  settings.push_back(SettingInfo::Action("Delete Cache", GroupType::DEVICE_ACTIONS));
  settings.push_back(SettingInfo::Action("Index your library", GroupType::DEVICE_ACTIONS));
  settings.push_back(SettingInfo::Action("Generate thumbnails", GroupType::DEVICE_ACTIONS));
  return settings;
}

}  // namespace

Settings::Settings(GfxRenderer& renderer, MappedInputManager& mappedInput) : Page("Settings", renderer, mappedInput) {}

void Settings::onEnter() {
  Page::onEnter();
  currentPanel = SettingsPanel::System;
  nextPanel = currentPanel;
  pending = Pending::None;
  externalNavigation = nullptr;
  tabsFocused_ = true;
  openPanel();
}

void Settings::onExit() {
  closePanel();
  Page::onExit();
}

void Settings::loop() {
  if (panel) {
    // The three settings tabs are the first focus target. Confirm advances to
    // the next settings tab; item navigation enters the active tab's rows.
    if (!panelDetailOpen()) {
      const bool upPressed = mappedInput.wasPressed(itemPrevButton());
      const bool downPressed = mappedInput.wasPressed(itemNextButton());
      if (tabsFocused_) {
        if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
          requestPanelSwitch();
          processPending();
          return;
        }
        if (upPressed) {
          requestRender();
          return;
        }
        if (downPressed) {
          tabsFocused_ = false;
        }
      } else if (upPressed && firstItemSelected()) {
        clearItemSelection();
        tabsFocused_ = true;
        requestRender();
        return;
      }
    }

    panel->loop();
    if (runExternalNavigation()) return;
    if (pending != Pending::None) processPending();

    bool panelRequestedRender = false;
    if (categoryPanel) panelRequestedRender = categoryPanel->takeRenderRequest();
    if (readerPanel) panelRequestedRender = readerPanel->takeRenderRequest() || panelRequestedRender;
    if (presetsPanel) panelRequestedRender = presetsPanel->takeRenderRequest() || panelRequestedRender;
    if (panelRequestedRender) requestRender();
    renderIfNeeded();
    return;
  }
  Page::loop();
}

bool Settings::firstItemSelected() const {
  return (categoryPanel && categoryPanel->isFirstItemSelected()) ||
         (readerPanel && readerPanel->isFirstItemSelected()) ||
         (presetsPanel && presetsPanel->isFirstItemSelected());
}

void Settings::clearItemSelection() {
  if (categoryPanel) categoryPanel->clearItemSelection();
  if (readerPanel) readerPanel->clearItemSelection();
  if (presetsPanel) presetsPanel->clearItemSelection();
}

void Settings::openPanel() {
  closePanel();

  const auto navigateHome = [this] { deferExternalNavigation([] { onGoToRecent(); }); };
  const auto navigateLibrary = [this] { deferExternalNavigation([] { onGoToLibrary("/"); }); };
  const auto navigateSync = [this] { deferExternalNavigation([] { onGoToFileTransfer(); }); };
  const auto navigateSearch = [this] {
    deferExternalNavigation([] { openSearchFromCallback([] { onGoToRecent(); }); });
  };

  if (currentPanel == SettingsPanel::System) {
    auto* category = new CategorySettingsActivity(
        renderer, mappedInput, "System settings", buildSystemSettings(renderer.deviceIsX3()),
        [this] { requestPanelSwitch(); }, nullptr, nullptr, nullptr, navigateHome, navigateLibrary, navigateSync,
        navigateSearch, true);
    categoryPanel = category;
    panel.reset(category);
  } else {
    // The current inx reader activity owns the global reader controls. Presets mode asks that same
    // activity to expose only its preset rows, preserving the existing preset editor/storage.
    auto* reader = new ReaderPresetsActivity(renderer, mappedInput, [this] { requestPanelSwitch(); }, navigateHome,
                                             navigateLibrary, navigateSync, navigateSearch, true,
                                             currentPanel == SettingsPanel::Presets);
    if (currentPanel == SettingsPanel::Reader) {
      readerPanel = reader;
    } else {
      presetsPanel = reader;
    }
    panel.reset(reader);
  }
  panel->onEnter();
}

void Settings::closePanel() {
  if (!panel) return;
  panel->onExit();
  panel.reset();
  categoryPanel = nullptr;
  readerPanel = nullptr;
  presetsPanel = nullptr;
}

void Settings::requestPanelSwitch() {
  nextPanel = currentPanel == SettingsPanel::System
                  ? SettingsPanel::Reader
                  : currentPanel == SettingsPanel::Reader ? SettingsPanel::Presets : SettingsPanel::System;
  pending = Pending::SwitchPanel;
}

void Settings::processPending() {
  if (pending != Pending::SwitchPanel) return;
  pending = Pending::None;
  SETTINGS.saveToFile();
  currentPanel = nextPanel;
  openPanel();
  requestRender();
}

void Settings::deferExternalNavigation(const std::function<void()>& action) { externalNavigation = action; }

bool Settings::runExternalNavigation() {
  if (!externalNavigation) return false;
  auto action = std::move(externalNavigation);
  externalNavigation = nullptr;
  action();
  return true;
}

bool Settings::panelDetailOpen() const {
  return (categoryPanel && categoryPanel->isDetailOpen()) ||
         (readerPanel && readerPanel->isDetailOpen()) || (presetsPanel && presetsPanel->isDetailOpen());
}

bool Settings::panelSubPageOpen() const {
  return (categoryPanel && categoryPanel->isSubPageOpen()) ||
         (readerPanel && readerPanel->isSubPageOpen()) || (presetsPanel && presetsPanel->isSubPageOpen());
}

bool Settings::panelOverlayOpen() const {
  return (categoryPanel && categoryPanel->isOverlayOpen()) ||
         (readerPanel && readerPanel->isOverlayOpen()) || (presetsPanel && presetsPanel->isOverlayOpen());
}

void Settings::title() const {
  const int font = MONTSERRAT_16_FONT_ID;
  const int textY = navigation::Menu::topPadding +
                    (navigation::Menu::iconSize - renderer.text.getLineHeight(font)) / 2;
  renderer.text.render(font, navigation::Menu::leftMargin, textY, name(), true, EpdFontFamily::BOLD);
}

void Settings::panelTabs() {
  renderPanelTab(renderer, systemX(renderer), kPanelTabY, kPanelTabWidth, kPanelTabHeight, "System",
                 currentPanel == SettingsPanel::System, true, false);
  renderPanelTab(renderer, readerX(renderer), kPanelTabY, kPanelTabWidth, kPanelTabHeight, "Reader",
                 currentPanel == SettingsPanel::Reader, false, false);
  renderPanelTab(renderer, presetsX(renderer), kPanelTabY, kPanelTabWidth, kPanelTabHeight, "Presets",
                 currentPanel == SettingsPanel::Presets, false, true);

  if (tabsFocused_) {
    const int selectedX = currentPanel == SettingsPanel::System
                              ? systemX(renderer)
                              : currentPanel == SettingsPanel::Reader ? readerX(renderer) : presetsX(renderer);
    const int centerX = selectedX + kPanelTabWidth / 2;
    const int centerY = kPanelTabY + kPanelTabHeight + 6;
    constexpr int radius = 3;
    for (int dy = -radius; dy <= radius; ++dy) {
      for (int dx = -radius; dx <= radius; ++dx) {
        if (dx * dx + dy * dy <= radius * radius) renderer.drawPixel(centerX + dx, centerY + dy, true);
      }
    }
  }
}

void Settings::content() {
  // Keep the Settings tab strip visible while a child selector popup is open.
  // Sub-pages are full-screen and intentionally replace the strip, but popups
  // are overlays on the current tab and must not remove its navigation.
  if (!panelSubPageOpen()) panelTabs();
  if (categoryPanel) categoryPanel->renderEmbedded();
  if (readerPanel) readerPanel->renderEmbedded();
  if (presetsPanel) presetsPanel->renderEmbedded();
}

void Settings::menu() {
  // A detail subpage owns the full screen and draws its own header. Do not
  // redraw the parent Settings shell afterward, otherwise its title is
  // painted over the child header at the same coordinates.
  if (panelSubPageOpen()) return;

  // Settings owns the page shell. Draw it last for normal settings frames so a
  // child panel's single-buffer clear or popup paper fill cannot erase the
  // bottom navigation.
  Page::menu();
}

bool Settings::back() {
  onGoToRecent();
  return true;
}
