#pragma once

/**
 * @file SerializedHyphenationTrie.h
 * @brief Public interface and types for SerializedHyphenationTrie.
 */

#include <cstddef>
#include <cstdint>


struct SerializedHyphenationPatterns {
  const std::uint8_t* data;
  size_t size;
};
