/**
 * @file BackupRestoreActivity.cpp
 * @brief Device backup and restore window launched from Sync.
 */

#include "BackupRestoreActivity.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "KOReaderCredentialStore.h"
#include "state/BookState.h"
#include "state/NetworkCredential.h"
#include "state/OpdsServerStore.h"
#include "state/ReaderPreset.h"
#include "state/RecentBooks.h"
#include "state/SystemSetting.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"
#include "system/UiTheme.h"

namespace {
constexpr int kBodyFont = ATKINSON_HYPERLEGIBLE_10_FONT_ID;
constexpr int kTitleFont = ATKINSON_HYPERLEGIBLE_12_FONT_ID;
constexpr int kMetaFont = ATKINSON_HYPERLEGIBLE_8_FONT_ID;
constexpr int kRowH = UiTheme::DRAWER_LIST_ITEM_HEIGHT;
constexpr const char* kBackupRoot = "/.system/backup";

constexpr const char* kDirectFiles[] = {
    "/.system/settings.bin",        "/.system/ui_theme.bin",      "/.system/reader_presets.bin",
    "/.system/wifi.bin",            "/.system/opds_servers.bin",  "/.system/koreader.bin",
    "/.system/statistics.bin",      "/.metadata/recent.bin",      "/.metadata/books.bin",
    "/.metadata/library/book_tags.json",
};

constexpr const char* kPerBookFiles[] = {"progress.bin", "statistics.bin", "settings.bin"};

std::string parentDir(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) {
    return "/";
  }
  return path.substr(0, slash);
}

void ensureDirForFile(const std::string& path) { SdMan.mkdir(parentDir(path).c_str()); }

bool copyFile(const char* from, const char* to, int& copied, int& skipped, int& failed) {
  if (!SdMan.exists(from)) {
    ++skipped;
    return true;
  }

  ensureDirForFile(to);
  FsFile src;
  if (!SdMan.openFileForRead("BKR", from, src)) {
    ++failed;
    return false;
  }
  FsFile dst;
  if (!SdMan.openFileForWrite("BKR", to, dst)) {
    src.close();
    ++failed;
    return false;
  }

  uint8_t buffer[256];
  bool ok = true;
  while (src.available()) {
    const int n = src.read(buffer, sizeof(buffer));
    if (n <= 0) {
      ok = false;
      break;
    }
    if (dst.write(buffer, static_cast<size_t>(n)) != static_cast<size_t>(n)) {
      ok = false;
      break;
    }
    yield();
  }

  src.close();
  dst.close();
  if (ok) {
    ++copied;
  } else {
    ++failed;
  }
  return ok;
}

bool copyFileToBackup(const char* path, int& copied, int& skipped, int& failed) {
  const std::string to = std::string(kBackupRoot) + path;
  return copyFile(path, to.c_str(), copied, skipped, failed);
}

bool copyFileFromBackup(const char* path, int& copied, int& skipped, int& failed) {
  const std::string from = std::string(kBackupRoot) + path;
  return copyFile(from.c_str(), path, copied, skipped, failed);
}

void copyPerBookFiles(const char* root, const bool toBackup, int& copied, int& skipped, int& failed) {
  FsFile dir = SdMan.open(root);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    ++skipped;
    return;
  }

  char name[128];
  dir.rewindDirectory();
  for (FsFile entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (!entry.isDirectory()) {
      entry.close();
      continue;
    }
    entry.getName(name, sizeof(name));
    entry.close();

    for (const char* fileName : kPerBookFiles) {
      const std::string relative = std::string(root) + "/" + name + "/" + fileName;
      if (toBackup) {
        copyFileToBackup(relative.c_str(), copied, skipped, failed);
      } else {
        const std::string backupPrefix = std::string(kBackupRoot);
        if (relative.compare(0, backupPrefix.size(), backupPrefix) != 0) {
          ++failed;
          continue;
        }
        const std::string target = relative.substr(backupPrefix.size());
        copyFile(relative.c_str(), target.c_str(), copied, skipped, failed);
      }
    }
  }
  dir.close();
}

void writeBackupMarker() {
  FsFile marker;
  if (SdMan.openFileForWrite("BKR", std::string(kBackupRoot) + "/backup.txt", marker)) {
    marker.println("INX backup");
    marker.close();
  }
}
}  // namespace

void BackupRestoreActivity::onEnter() {
  ActivityWithSubactivity::onEnter();
  state_ = State::Menu;
  selectedIndex_ = 0;
  copiedCount_ = 0;
  skippedCount_ = 0;
  failedCount_ = 0;
  render();
}

void BackupRestoreActivity::loop() {
  if (state_ == State::Working) {
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    goBack_();
    return;
  }

  if (state_ == State::Done) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      state_ = State::Menu;
      render();
    }
    return;
  }

  if (mappedInput.wasPressed(MenuNav::itemPrev())) {
    selectedIndex_ = (selectedIndex_ + 1) % 2;
    render();
    return;
  }
  if (mappedInput.wasPressed(MenuNav::itemNext())) {
    selectedIndex_ = (selectedIndex_ + 1) % 2;
    render();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedIndex_ == 0) {
      startAction(Action::Backup);
    } else if (selectedIndex_ == 1) {
      startAction(Action::Restore);
    }
  }
}

void BackupRestoreActivity::render() {
  renderer.clearScreen();
  const int bodyTop = INX_THEME.drawPageHeader(renderer, "Backup and restore");
  if (state_ == State::Working) {
    renderWorking();
  } else if (state_ == State::Done) {
    renderDone();
  } else {
    renderMenu(bodyTop);
  }
  renderer.displayBuffer();
}

void BackupRestoreActivity::renderMenu(const int bodyTop) {
  const int pageW = renderer.getScreenWidth();
  constexpr const char* labels[] = {"Create backup", "Restore backup"};

  for (int i = 0; i < 2; ++i) {
    const int y = bodyTop + i * kRowH;
    const bool selected = i == selectedIndex_;
    if (selected) {
      renderer.rectangle.fill(0, y, pageW, kRowH, static_cast<int>(GfxRenderer::FillTone::Ink));
    }
    const int titleY = y + (kRowH - renderer.text.getLineHeight(kBodyFont)) / 2;
    renderer.text.render(kBodyFont, 20, titleY, labels[i], !selected, EpdFontFamily::REGULAR);
    const int caretW = renderer.text.getWidth(kBodyFont, "›");
    renderer.text.render(kBodyFont, pageW - caretW - 30, titleY, "›", !selected);
    renderer.line.render(0, y + kRowH - 1, pageW, y + kRowH - 1, true, LineRender::Style::Dotted);
  }

  const auto hints = mappedInput.mapLabels("\xC2\xAB Back", "Select", "Up", "Down");
  renderer.ui.buttonHints(kBodyFont, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
}

void BackupRestoreActivity::renderWorking() {
  const int centerY = renderer.getScreenHeight() / 2;
  renderer.text.centered(kTitleFont, centerY - 18,
                         action_ == Action::Backup ? "Creating backup" : "Restoring backup", true,
                         EpdFontFamily::BOLD);
  renderer.text.centered(kMetaFont, centerY + 18, "Please wait");
}

void BackupRestoreActivity::renderDone() {
  const int centerY = renderer.getScreenHeight() / 2;
  const bool ok = failedCount_ == 0;
  renderer.text.centered(kTitleFont, centerY - 42,
                         action_ == Action::Backup ? (ok ? "Backup complete" : "Backup incomplete")
                                                   : (ok ? "Restore complete" : "Restore incomplete"),
                         true, EpdFontFamily::BOLD);

  char line[80];
  snprintf(line, sizeof(line), "%d copied, %d skipped, %d failed", copiedCount_, skippedCount_, failedCount_);
  renderer.text.centered(kBodyFont, centerY, line, true);
  renderer.text.centered(kMetaFont, centerY + 34,
                         ok ? (action_ == Action::Backup ? "Saved in /.system/backup" : "State restored from backup")
                            : "Some files could not be copied");

  const auto hints = mappedInput.mapLabels("\xC2\xAB Back", "Menu", "", "");
  renderer.ui.buttonHints(kBodyFont, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
}

void BackupRestoreActivity::startAction(const Action action) {
  action_ = action;
  copiedCount_ = 0;
  skippedCount_ = 0;
  failedCount_ = 0;
  state_ = State::Working;
  render();
  if (action == Action::Backup) {
    createBackup();
  } else {
    restoreBackup();
  }
  state_ = State::Done;
  render();
}

bool BackupRestoreActivity::createBackup() {
  if (SdMan.exists(kBackupRoot)) {
    SdMan.removeDir(kBackupRoot);
  }
  SdMan.mkdir(kBackupRoot);
  SdMan.mkdir((std::string(kBackupRoot) + "/.system").c_str());
  SdMan.mkdir((std::string(kBackupRoot) + "/.metadata").c_str());

  for (const char* path : kDirectFiles) {
    copyFileToBackup(path, copiedCount_, skippedCount_, failedCount_);
  }
  copyPerBookFiles("/.metadata/epub", true, copiedCount_, skippedCount_, failedCount_);
  copyPerBookFiles("/.metadata/xtc", true, copiedCount_, skippedCount_, failedCount_);
  writeBackupMarker();
  return failedCount_ == 0;
}

bool BackupRestoreActivity::restoreBackup() {
  if (!SdMan.exists(kBackupRoot)) {
    failedCount_++;
    return false;
  }
  for (const char* path : kDirectFiles) {
    copyFileFromBackup(path, copiedCount_, skippedCount_, failedCount_);
  }
  copyPerBookFiles((std::string(kBackupRoot) + "/.metadata/epub").c_str(), false, copiedCount_, skippedCount_,
                   failedCount_);
  copyPerBookFiles((std::string(kBackupRoot) + "/.metadata/xtc").c_str(), false, copiedCount_, skippedCount_,
                   failedCount_);
  reloadStoresAfterRestore();
  return failedCount_ == 0;
}

void BackupRestoreActivity::reloadStoresAfterRestore() {
  SETTINGS.loadFromFile();
  RECENT_BOOKS.loadFromFile();
  BOOK_STATE.loadFromFile();
  WIFI_STORE.loadFromFile();
  OPDS_STORE.loadFromFile();
  KOREADER_STORE.loadFromFile();
  READER_PRESETS.reload();
}
