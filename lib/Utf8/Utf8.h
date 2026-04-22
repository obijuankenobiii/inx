#pragma once

/**
 * @file Utf8.h
 * @brief Public interface and types for Utf8.
 */

#include <cstdint>
#include <string>
#define REPLACEMENT_GLYPH 0xFFFD

uint32_t utf8NextCodepoint(const unsigned char** string);

size_t utf8RemoveLastChar(std::string& str);

void utf8TruncateChars(std::string& str, size_t numChars);
