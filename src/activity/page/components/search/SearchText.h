#pragma once

#include <string>

class GfxRenderer;

/** Search field shared by the physical-button search page. */
class SearchText {
 public:
  static constexpr int height = 56;

  static int top();
  static void render(const GfxRenderer& renderer, const std::string& value,
                     const char* placeholder = "Search books");
};
