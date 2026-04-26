#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

/** Raw HID report prefix captured during learning (page-turn remotes). */
struct BleRemoteBindings {
  uint8_t nextLen = 0;
  uint8_t prevLen = 0;
  uint8_t nextPat[12]{};
  uint8_t prevPat[12]{};
};

bool bleRemoteHasSaved();
bool bleRemoteLoad(uint8_t outAddr[6], BleRemoteBindings& outBindings);
void bleRemoteSave(const uint8_t addr[6], const BleRemoteBindings& bindings);
void bleRemoteClear();

/** MAC as lowercase hex with colons (NimBLE-style), e.g. aa:bb:cc:dd:ee:ff */
bool bleRemoteAddrBytesToAscii(const uint8_t addr[6], std::string& out);
/** Parse MAC from scan/connect string into six bytes. */
bool bleRemoteParseAsciiAddr(const std::string& ascii, uint8_t outAddr[6]);
