#pragma once

/**
 * @file BleRemoteSetupActivity.h
 * @brief Pair one BLE HID page-turn remote and learn next/prev report prefixes.
 */

#include <freertos/portmacro.h>

#include <functional>
#include <string>
#include <vector>

#include "../Activity.h"
#include "state/BleRemoteStore.h"
#include "system/BleHidRemote.h"

/**
 * Full-screen flow: scan, pick device, learn next then previous HID report, save.
 * NimBLE stack is held only while this activity is active (plus EPUB reader when paired).
 */
class BleRemoteSetupActivity final : public Activity {
 public:
  BleRemoteSetupActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::function<void()> onDone);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }

 private:
  enum class Phase { MainMenu, DeviceList, LearnNext, LearnPrev };

  const std::function<void()> onDone_;

  Phase phase_ = Phase::MainMenu;
  int menuIndex_ = 0;
  int listIndex_ = 0;
  std::vector<BleScannedRow> scanRows_;
  std::string pickedAddrHex_;
  uint8_t pickedAddrBytes_[6]{};
  BleRemoteBindings learnBindings_{};

  void* hidClient_ = nullptr;
  unsigned long phaseDeadlineMs_ = 0;

  portMUX_TYPE learnMux_ = portMUX_INITIALIZER_UNLOCKED;
  uint8_t learnBuf_[16]{};
  size_t learnLen_ = 0;
  volatile bool learnFresh_ = false;

  char statusLine_[48]{};

  void pushLearn(const uint8_t* data, size_t len);
  bool takeLearn(uint8_t out[16], size_t& outLen);
  void disconnectHid();
  void drainLearn();
  void render() const;
  void setStatus(const char* msg);
  bool startLearnConnection();
  void copyPrefixFromReport(const uint8_t* data, size_t len, uint8_t outPat[12], uint8_t& outLen) const;
  bool patternsConflict(const uint8_t* a, uint8_t alen, const uint8_t* b, uint8_t blen) const;
};
