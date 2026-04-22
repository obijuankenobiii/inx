/**
 * @file LiangHyphenation.cpp
 * @brief Definitions for LiangHyphenation.
 */

#include "LiangHyphenation.h"

#include <algorithm>
#include <vector>



namespace {

struct AugmentedWord {
  std::vector<uint8_t> bytes;
  std::vector<size_t> charByteOffsets;
  std::vector<int32_t> byteToCharIndex;

  bool empty() const { return bytes.empty(); }
  size_t charCount() const { return charByteOffsets.size(); }
};


size_t encodeUtf8(uint32_t cp, std::vector<uint8_t>& out) {
  if (cp <= 0x7Fu) {
    out.push_back(static_cast<uint8_t>(cp));
    return 1;
  }
  if (cp <= 0x7FFu) {
    out.push_back(static_cast<uint8_t>(0xC0u | ((cp >> 6) & 0x1Fu)));
    out.push_back(static_cast<uint8_t>(0x80u | (cp & 0x3Fu)));
    return 2;
  }
  if (cp <= 0xFFFFu) {
    out.push_back(static_cast<uint8_t>(0xE0u | ((cp >> 12) & 0x0Fu)));
    out.push_back(static_cast<uint8_t>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<uint8_t>(0x80u | (cp & 0x3Fu)));
    return 3;
  }
  out.push_back(static_cast<uint8_t>(0xF0u | ((cp >> 18) & 0x07u)));
  out.push_back(static_cast<uint8_t>(0x80u | ((cp >> 12) & 0x3Fu)));
  out.push_back(static_cast<uint8_t>(0x80u | ((cp >> 6) & 0x3Fu)));
  out.push_back(static_cast<uint8_t>(0x80u | (cp & 0x3Fu)));
  return 4;
}


AugmentedWord buildAugmentedWord(const std::vector<CodepointInfo>& cps, const LiangWordConfig& config) {
  AugmentedWord word;
  if (cps.empty()) {
    return word;
  }

  word.bytes.reserve(cps.size() * 2 + 2);
  word.charByteOffsets.reserve(cps.size() + 2);

  word.charByteOffsets.push_back(0);
  word.bytes.push_back('.');

  for (const auto& info : cps) {
    if (!config.isLetter(info.value)) {
      word.bytes.clear();
      word.charByteOffsets.clear();
      word.byteToCharIndex.clear();
      return word;
    }
    word.charByteOffsets.push_back(word.bytes.size());
    encodeUtf8(config.toLower(info.value), word.bytes);
  }

  word.charByteOffsets.push_back(word.bytes.size());
  word.bytes.push_back('.');

  word.byteToCharIndex.assign(word.bytes.size(), -1);
  for (size_t i = 0; i < word.charByteOffsets.size(); ++i) {
    const size_t offset = word.charByteOffsets[i];
    if (offset < word.byteToCharIndex.size()) {
      word.byteToCharIndex[offset] = static_cast<int32_t>(i);
    }
  }
  return word;
}





struct AutomatonState {
  const uint8_t* data = nullptr;
  size_t size = 0;
  size_t addr = 0;
  uint8_t stride = 1;
  size_t childCount = 0;
  const uint8_t* transitions = nullptr;
  const uint8_t* targets = nullptr;
  const uint8_t* levels = nullptr;
  size_t levelsLen = 0;

  bool valid() const { return data != nullptr; }
};





struct EmbeddedAutomaton {
  const uint8_t* data = nullptr;
  size_t size = 0;
  uint32_t rootOffset = 0;

  bool valid() const { return data != nullptr && size >= 4 && rootOffset < size; }
};


EmbeddedAutomaton parseAutomaton(const SerializedHyphenationPatterns& patterns) {
  EmbeddedAutomaton automaton;
  if (!patterns.data || patterns.size < 4) {
    return automaton;
  }

  automaton.data = patterns.data;
  automaton.size = patterns.size;
  automaton.rootOffset = (static_cast<uint32_t>(patterns.data[0]) << 24) |
                         (static_cast<uint32_t>(patterns.data[1]) << 16) |
                         (static_cast<uint32_t>(patterns.data[2]) << 8) | static_cast<uint32_t>(patterns.data[3]);
  if (automaton.rootOffset >= automaton.size) {
    automaton.data = nullptr;
    automaton.size = 0;
  }
  return automaton;
}


const EmbeddedAutomaton& getAutomaton(const SerializedHyphenationPatterns& patterns) {
  struct CacheEntry {
    const SerializedHyphenationPatterns* key;
    EmbeddedAutomaton automaton;
  };
  static std::vector<CacheEntry> cache;

  for (const auto& entry : cache) {
    if (entry.key == &patterns) {
      return entry.automaton;
    }
  }

  cache.push_back({&patterns, parseAutomaton(patterns)});
  return cache.back().automaton;
}


AutomatonState decodeState(const EmbeddedAutomaton& automaton, size_t addr) {
  AutomatonState state;
  if (!automaton.valid() || addr >= automaton.size) {
    return state;
  }

  const uint8_t* base = automaton.data + addr;
  size_t remaining = automaton.size - addr;
  size_t pos = 0;

  const uint8_t header = base[pos++];
  
  
  
  
  const bool hasLevels = (header >> 7) != 0;
  uint8_t stride = static_cast<uint8_t>((header >> 5) & 0x03u);
  if (stride == 0) {
    stride = 1;
  }
  size_t childCount = static_cast<size_t>(header & 0x1Fu);
  if (childCount == 31u) {
    if (pos >= remaining) {
      return AutomatonState{};
    }
    childCount = base[pos++];
  }

  const uint8_t* levelsPtr = nullptr;
  size_t levelsLen = 0;
  if (hasLevels) {
    if (pos + 1 >= remaining) {
      return AutomatonState{};
    }
    const uint8_t offsetHi = base[pos++];
    const uint8_t offsetLoLen = base[pos++];
    
    
    const size_t offset = (static_cast<size_t>(offsetHi) << 4) | (offsetLoLen >> 4);
    levelsLen = offsetLoLen & 0x0Fu;
    if (offset + levelsLen > automaton.size) {
      return AutomatonState{};
    }
    levelsPtr = automaton.data + offset;
  }

  if (pos + childCount > remaining) {
    return AutomatonState{};
  }
  const uint8_t* transitions = base + pos;
  pos += childCount;

  const size_t targetsBytes = childCount * stride;
  if (pos + targetsBytes > remaining) {
    return AutomatonState{};
  }
  const uint8_t* targets = base + pos;

  state.data = automaton.data;
  state.size = automaton.size;
  state.addr = addr;
  state.stride = stride;
  state.childCount = childCount;
  state.transitions = transitions;
  state.targets = targets;
  state.levels = levelsPtr;
  state.levelsLen = levelsLen;
  return state;
}


int32_t decodeDelta(const uint8_t* buf, uint8_t stride) {
  if (stride == 1) {
    return static_cast<int8_t>(buf[0]);
  }
  if (stride == 2) {
    return static_cast<int16_t>((static_cast<uint16_t>(buf[0]) << 8) | static_cast<uint16_t>(buf[1]));
  }
  const int32_t unsignedVal =
      (static_cast<int32_t>(buf[0]) << 16) | (static_cast<int32_t>(buf[1]) << 8) | static_cast<int32_t>(buf[2]);
  return unsignedVal - (1 << 23);
}


bool transition(const EmbeddedAutomaton& automaton, const AutomatonState& state, uint8_t letter, AutomatonState& out) {
  if (!state.valid()) {
    return false;
  }

  
  
  for (size_t idx = 0; idx < state.childCount; ++idx) {
    if (state.transitions[idx] != letter) {
      continue;
    }
    const uint8_t* deltaPtr = state.targets + idx * state.stride;
    const int32_t delta = decodeDelta(deltaPtr, state.stride);
    
    
    const int64_t nextAddr = static_cast<int64_t>(state.addr) + delta;
    if (nextAddr < 0 || static_cast<size_t>(nextAddr) >= automaton.size) {
      return false;
    }
    out = decodeState(automaton, static_cast<size_t>(nextAddr));
    return out.valid();
  }
  return false;
}




std::vector<size_t> collectBreakIndexes(const std::vector<CodepointInfo>& cps, const std::vector<uint8_t>& scores,
                                        const size_t minPrefix, const size_t minSuffix) {
  std::vector<size_t> indexes;
  const size_t cpCount = cps.size();
  if (cpCount < 2) {
    return indexes;
  }

  for (size_t breakIndex = 1; breakIndex < cpCount; ++breakIndex) {
    if (breakIndex < minPrefix) {
      continue;
    }

    const size_t suffixCount = cpCount - breakIndex;
    if (suffixCount < minSuffix) {
      continue;
    }

    const size_t scoreIdx = breakIndex + 1;
    if (scoreIdx >= scores.size()) {
      break;
    }
    if ((scores[scoreIdx] & 1u) == 0) {
      continue;
    }
    indexes.push_back(breakIndex);
  }

  return indexes;
}

}  


std::vector<size_t> liangBreakIndexes(const std::vector<CodepointInfo>& cps,
                                      const SerializedHyphenationPatterns& patterns, const LiangWordConfig& config) {
  const auto augmented = buildAugmentedWord(cps, config);
  if (augmented.empty()) {
    return {};
  }

  const EmbeddedAutomaton& automaton = getAutomaton(patterns);
  if (!automaton.valid()) {
    return {};
  }

  const AutomatonState root = decodeState(automaton, automaton.rootOffset);
  if (!root.valid()) {
    return {};
  }

  
  std::vector<uint8_t> scores(augmented.charCount(), 0);

  
  for (size_t charStart = 0; charStart < augmented.charByteOffsets.size(); ++charStart) {
    const size_t byteStart = augmented.charByteOffsets[charStart];
    AutomatonState state = root;

    for (size_t cursor = byteStart; cursor < augmented.bytes.size(); ++cursor) {
      AutomatonState next;
      if (!transition(automaton, state, augmented.bytes[cursor], next)) {
        break;  
      }
      state = next;

      if (state.levels && state.levelsLen > 0) {
        size_t offset = 0;
        
        for (size_t i = 0; i < state.levelsLen; ++i) {
          const uint8_t packed = state.levels[i];
          const size_t dist = static_cast<size_t>(packed / 10);
          const uint8_t level = static_cast<uint8_t>(packed % 10);

          offset += dist;
          const size_t splitByte = byteStart + offset;
          if (splitByte >= augmented.byteToCharIndex.size()) {
            continue;
          }

          const int32_t boundary = augmented.byteToCharIndex[splitByte];
          if (boundary < 0) {
            continue;  
          }
          if (boundary < 2 || boundary + 2 > static_cast<int32_t>(augmented.charCount())) {
            continue;  
          }

          const size_t idx = static_cast<size_t>(boundary);
          if (idx >= scores.size()) {
            continue;
          }
          scores[idx] = std::max(scores[idx], level);
        }
      }
    }
  }

  return collectBreakIndexes(cps, scores, config.minPrefix, config.minSuffix);
}
