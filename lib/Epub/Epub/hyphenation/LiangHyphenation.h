#pragma once

/**
 * @file LiangHyphenation.h
 * @brief Public interface and types for LiangHyphenation.
 */

#include <cstddef>
#include <cstdint>
#include <vector>

#include "HyphenationCommon.h"
#include "SerializedHyphenationTrie.h"





struct LiangWordConfig {
  static constexpr size_t kDefaultMinPrefix = 2;
  static constexpr size_t kDefaultMinSuffix = 2;
  
  
  bool (*isLetter)(uint32_t);
  
  
  
  uint32_t (*toLower)(uint32_t);
  
  
  size_t minPrefix;
  size_t minSuffix;

  
  
  LiangWordConfig(bool (*letterFn)(uint32_t), uint32_t (*lowerFn)(uint32_t), size_t prefix = kDefaultMinPrefix,
                  size_t suffix = kDefaultMinSuffix)
      : isLetter(letterFn), toLower(lowerFn), minPrefix(prefix), minSuffix(suffix) {}
};


std::vector<size_t> liangBreakIndexes(const std::vector<CodepointInfo>& cps,
                                      const SerializedHyphenationPatterns& patterns, const LiangWordConfig& config);
