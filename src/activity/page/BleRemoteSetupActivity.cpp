/**
 * @file BleRemoteSetupActivity.cpp
 * @brief BLE HID remote pairing and key learning UI.
 */

#include "activity/page/BleRemoteSetupActivity.h"

#include <cstring>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <GfxRenderer.h>

#include "system/Fonts.h"
#include "system/MappedInputManager.h"

namespace {

constexpr int kMenuCount = 3;
constexpr int kRowH = 52;
constexpr unsigned long kLearnTimeoutMs = 90000;

}  // namespace

BleRemoteSetupActivity::BleRemoteSetupActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               std::function<void()> onDone)
    : Activity("BleRemoteSetup", renderer, mappedInput), onDone_(std::move(onDone)) {}

void BleRemoteSetupActivity::setStatus(const char* msg) {
  if (!msg) {
    statusLine_[0] = '\0';
    return;
  }
  strncpy(statusLine_, msg, sizeof(statusLine_) - 1);
  statusLine_[sizeof(statusLine_) - 1] = '\0';
}

void BleRemoteSetupActivity::pushLearn(const uint8_t* data, const size_t len) {
  if (!data || len == 0) {
    return;
  }
  portENTER_CRITICAL(&learnMux_);
  learnLen_ = len > sizeof(learnBuf_) ? sizeof(learnBuf_) : len;
  memcpy(learnBuf_, data, learnLen_);
  learnFresh_ = true;
  portEXIT_CRITICAL(&learnMux_);
}

bool BleRemoteSetupActivity::takeLearn(uint8_t out[16], size_t& outLen) {
  bool got = false;
  portENTER_CRITICAL(&learnMux_);
  if (learnFresh_) {
    outLen = learnLen_;
    memcpy(out, learnBuf_, learnLen_);
    learnFresh_ = false;
    got = true;
  }
  portEXIT_CRITICAL(&learnMux_);
  return got;
}

void BleRemoteSetupActivity::disconnectHid() {
  if (hidClient_) {
    bleRemoteDisconnectClient(hidClient_);
    hidClient_ = nullptr;
  }
}

void BleRemoteSetupActivity::drainLearn() {
  uint8_t tmp[16]{};
  size_t n = 0;
  while (takeLearn(tmp, n)) {
  }
}

void BleRemoteSetupActivity::copyPrefixFromReport(const uint8_t* data, const size_t len, uint8_t outPat[12],
                                                  uint8_t& outLen) const {
  outLen = static_cast<uint8_t>(len > 12 ? 12 : len);
  memcpy(outPat, data, outLen);
}

bool BleRemoteSetupActivity::patternsConflict(const uint8_t* a, const uint8_t alen, const uint8_t* b,
                                              const uint8_t blen) const {
  if (!a || !b || alen == 0 || blen == 0 || alen != blen) {
    return false;
  }
  return memcmp(a, b, alen) == 0;
}

bool BleRemoteSetupActivity::startLearnConnection() {
  disconnectHid();
  learnFresh_ = false;
  std::string err;
  hidClient_ = bleRemoteConnectHid(
      pickedAddrHex_,
      [this](const uint8_t* p, size_t l) { pushLearn(p, l); }, err);
  if (!hidClient_) {
    setStatus(err.c_str());
    return false;
  }
  return true;
}

void BleRemoteSetupActivity::onEnter() {
  Activity::onEnter();
  phase_ = Phase::MainMenu;
  menuIndex_ = 0;
  listIndex_ = 0;
  scanRows_.clear();
  pickedAddrHex_.clear();
  memset(pickedAddrBytes_, 0, sizeof(pickedAddrBytes_));
  learnBindings_ = {};
  statusLine_[0] = '\0';

  if (!bleRemoteStackAcquire()) {
    setStatus("Bluetooth init failed");
  }
  render();
}

void BleRemoteSetupActivity::onExit() {
  disconnectHid();
  bleRemoteStackRelease();
  Activity::onExit();
}

void BleRemoteSetupActivity::render() const {
  renderer.clearScreen();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  int y = 12;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 16, y, "Bluetooth remote", true, EpdFontFamily::BOLD);
  y += 28;
  renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, 16, y,
                    "One device. Scan replaces the saved remote.", true);
  y += 22;

  if (statusLine_[0] != '\0') {
    renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, 16, y, statusLine_, true);
    y += 20;
  }

  uint8_t savedAddr[6]{};
  BleRemoteBindings savedBind{};
  char pairLine[40] = "Saved: (none)";
  if (bleRemoteLoad(savedAddr, savedBind)) {
    std::string mac;
    if (bleRemoteAddrBytesToAscii(savedAddr, mac)) {
      snprintf(pairLine, sizeof(pairLine), "Saved: %s", mac.c_str());
    }
  }
  renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, 16, y, pairLine, true);
  y += 26;
  renderer.drawLine(0, y, sw, y, true);
  y += 8;

  if (phase_ == Phase::MainMenu) {
    const char* labels[kMenuCount] = {"Scan & map remote", "Forget saved remote", "Back to sync"};
    for (int i = 0; i < kMenuCount; ++i) {
      const int rowY = y + i * kRowH;
      const bool sel = (i == menuIndex_);
      if (sel) {
        renderer.fillRect(0, rowY, sw, kRowH - 2, GfxRenderer::FillTone::Ink);
      }
      const int ty = rowY + (kRowH - 2 - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, ty, labels[i], !sel);
    }
  } else if (phase_ == Phase::DeviceList) {
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 16, y, "Pick a device", true, EpdFontFamily::BOLD);
    y += 28;
    if (scanRows_.empty()) {
      renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 16, y, "(none found)", true);
    } else {
      const int maxVis = (sh - y - 60) / kRowH;
      const int start = (listIndex_ >= maxVis) ? (listIndex_ - maxVis + 1) : 0;
      for (int vi = 0; vi < maxVis && start + vi < static_cast<int>(scanRows_.size()); ++vi) {
        const int idx = start + vi;
        const int rowY = y + vi * kRowH;
        const bool sel = (idx == listIndex_);
        if (sel) {
          renderer.fillRect(0, rowY, sw, kRowH - 2, GfxRenderer::FillTone::Ink);
        }
        char line[96];
        const auto& row = scanRows_[static_cast<size_t>(idx)];
        snprintf(line, sizeof(line), "%s", row.name.c_str());
        const int ty = rowY + (kRowH - 2 - renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_10_FONT_ID)) / 2;
        renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, ty, line, !sel);
        const int ty2 = ty + renderer.getLineHeight(ATKINSON_HYPERLEGIBLE_8_FONT_ID);
        renderer.drawText(ATKINSON_HYPERLEGIBLE_8_FONT_ID, 20, ty2, row.addrHex.c_str(), !sel);
      }
    }
  } else if (phase_ == Phase::LearnNext) {
    renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 16, y, "Map: next page", true, EpdFontFamily::BOLD);
    y += 36;
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 16, y, "Press the button you use for", true);
    y += 22;
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 16, y, "NEXT page on the remote.", true);
  } else if (phase_ == Phase::LearnPrev) {
    renderer.drawText(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 16, y, "Map: previous page", true, EpdFontFamily::BOLD);
    y += 36;
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 16, y, "Press the button you use for", true);
    y += 22;
    renderer.drawText(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 16, y, "PREVIOUS page (different key).", true);
  }

  renderer.drawButtonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, "", "Select", "", "Back");
  renderer.displayBuffer();
}

void BleRemoteSetupActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (phase_ == Phase::MainMenu) {
      if (onDone_) {
        onDone_();
      }
      return;
    }
    disconnectHid();
    phase_ = Phase::MainMenu;
    setStatus("");
    render();
    return;
  }

  const bool up = mappedInput.wasPressed(MappedInputManager::Button::Up);
  const bool down = mappedInput.wasPressed(MappedInputManager::Button::Down);
  const bool ok = mappedInput.wasPressed(MappedInputManager::Button::Confirm);

  if (phase_ == Phase::MainMenu) {
    if (up) {
      menuIndex_ = (menuIndex_ + kMenuCount - 1) % kMenuCount;
      render();
    }
    if (down) {
      menuIndex_ = (menuIndex_ + 1) % kMenuCount;
      render();
    }
    if (ok) {
      if (menuIndex_ == 0) {
        setStatus("Scanning...");
        render();
        scanRows_.clear();
        bleRemoteScanForDevices(3000, scanRows_);
        if (scanRows_.empty()) {
          setStatus("No devices found");
          phase_ = Phase::MainMenu;
        } else {
          setStatus("");
          phase_ = Phase::DeviceList;
          listIndex_ = 0;
        }
        render();
        return;
      }
      if (menuIndex_ == 1) {
        if (!bleRemoteHasSaved()) {
          setStatus("No saved remote");
        } else {
          bleRemoteClear();
          setStatus("Forgot remote");
        }
        render();
        return;
      }
      if (menuIndex_ == 2) {
        if (onDone_) {
          onDone_();
        }
        return;
      }
    }
    return;
  }

  if (phase_ == Phase::DeviceList) {
    if (up && listIndex_ > 0) {
      listIndex_--;
      render();
    }
    if (down && listIndex_ + 1 < static_cast<int>(scanRows_.size())) {
      listIndex_++;
      render();
    }
    if (ok && !scanRows_.empty()) {
      const auto& row = scanRows_[static_cast<size_t>(listIndex_)];
      pickedAddrHex_ = row.addrHex;
      if (!bleRemoteParseAsciiAddr(pickedAddrHex_, pickedAddrBytes_)) {
        setStatus("Bad address");
        phase_ = Phase::MainMenu;
        render();
        return;
      }
      if (!startLearnConnection()) {
        phase_ = Phase::MainMenu;
        render();
        return;
      }
      vTaskDelay(pdMS_TO_TICKS(300));
      drainLearn();
      phase_ = Phase::LearnNext;
      phaseDeadlineMs_ = millis() + kLearnTimeoutMs;
      setStatus("");
      render();
    }
    return;
  }

  if (phase_ == Phase::LearnNext) {
    if (millis() > phaseDeadlineMs_) {
      disconnectHid();
      setStatus("Timed out (next)");
      phase_ = Phase::MainMenu;
      render();
      return;
    }
    uint8_t buf[16]{};
    size_t n = 0;
    if (takeLearn(buf, n) && n > 0) {
      copyPrefixFromReport(buf, n, learnBindings_.nextPat, learnBindings_.nextLen);
      portENTER_CRITICAL(&learnMux_);
      learnFresh_ = false;
      portEXIT_CRITICAL(&learnMux_);
      disconnectHid();
      vTaskDelay(pdMS_TO_TICKS(250));
      drainLearn();
      if (!startLearnConnection()) {
        setStatus("Reconnect failed");
        phase_ = Phase::MainMenu;
        render();
        return;
      }
      vTaskDelay(pdMS_TO_TICKS(200));
      drainLearn();
      phase_ = Phase::LearnPrev;
      phaseDeadlineMs_ = millis() + kLearnTimeoutMs;
      render();
    }
    return;
  }

  if (phase_ == Phase::LearnPrev) {
    if (millis() > phaseDeadlineMs_) {
      disconnectHid();
      setStatus("Timed out (prev)");
      phase_ = Phase::MainMenu;
      render();
      return;
    }
    uint8_t buf[16]{};
    size_t n = 0;
    if (takeLearn(buf, n) && n > 0) {
      uint8_t plen = 0;
      uint8_t pat[12]{};
      copyPrefixFromReport(buf, n, pat, plen);
      if (patternsConflict(pat, plen, learnBindings_.nextPat, learnBindings_.nextLen)) {
        setStatus("Use a different key than next");
        render();
        return;
      }
      memcpy(learnBindings_.prevPat, pat, plen);
      learnBindings_.prevLen = plen;
      bleRemoteSave(pickedAddrBytes_, learnBindings_);
      disconnectHid();
      setStatus("Saved. Open a book to use it.");
      phase_ = Phase::MainMenu;
      render();
    }
  }

  delay(5);
}
