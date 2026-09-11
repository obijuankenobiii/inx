#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

/** Lists and installs compiled SD-font packages hosted in the inx-font repository. */
class FontPackageManager {
 public:
  enum class Category : uint8_t {
    SansSerif,
    Serif,
  };

  struct Package {
    std::string name;
    std::string variant;
    std::string url;
    std::string installFamily;
    size_t size = 0;
    Category category = Category::SansSerif;
  };

  static bool fetchAvailable(std::vector<Package>& packages, std::string& error);
  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;
  static bool install(const Package& package, std::string& error, ProgressCallback progress = nullptr);
  static bool remove(const Package& package, std::string& error);
  static bool isInstalled(const Package& package);

 private:
  FontPackageManager() = delete;
};

