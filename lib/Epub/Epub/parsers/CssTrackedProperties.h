#pragma once

/**
 * @file CssTrackedProperties.h
 * @brief Allowlist of CSS properties the reader actually consumes.
 *
 * The EPUB layout/render pipeline only reads the properties listed here (alignment, indent,
 * font weight/style/variant, display, image sizing, table borders, block spacing, backgrounds).
 * Every other declaration is dropped at parse time so large stylesheets don't exhaust the heap.
 *
 * Keep this in sync with the getters in CssParser (computeParagraphAlignment, resolveSmallCaps,
 * isDisplayBlock, getWidth/getHeight/min/max, getBlockSpacing, background-image resolution, …).
 * The table MUST stay sorted (strcmp order) — lookup is a binary search.
 */

#include <algorithm>
#include <cstring>
#include <string>

inline bool isTrackedCssProperty(const std::string& name) {
  static const char* const kTracked[] = {
      "background",      "background-image", "block-size",
      "border",          "border-bottom",    "border-left",
      "border-right",    "border-style",     "border-top",
      "border-width",    "display",          "font-size",
      "font-style",      "font-variant",     "font-variant-caps",
      "font-weight",     "height",           "initial-letter",
      "inline-size",     "line-height",      "margin",
      "margin-bottom",   "margin-left",      "margin-right",
      "margin-top",      "max-block-size",   "max-height",
      "max-inline-size", "max-width",        "min-block-size",
      "min-height",      "min-inline-size",  "min-width",
      "padding",         "padding-bottom",   "padding-left",
      "padding-right",   "padding-top",      "text-align",
      "text-indent",     "vertical-align",   "width",
  };
  return std::binary_search(std::begin(kTracked), std::end(kTracked), name.c_str(),
                            [](const char* a, const char* b) { return std::strcmp(a, b) < 0; });
}
