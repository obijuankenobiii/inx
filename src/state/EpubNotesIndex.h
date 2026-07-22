#pragma once

#include <SDCardManager.h>

namespace EpubNotesIndex {

constexpr const char* kPath = "/.metadata/epub/notes_index.json";
constexpr int kVersion = 4;

inline void invalidate() {
  if (SdMan.exists(kPath)) {
    SdMan.remove(kPath);
  }
}

}  // namespace EpubNotesIndex
