#include "FontPackageManager.h"

#include <SDCardManager.h>

#include "../network/HttpDownloader.h"
#include "FontManager.h"
#include <FsHelpers.h>
#include <ZipFile.h>

#include <utility>

#include "util/SdIoMutex.h"
#include "util/StringUtils.h"

namespace {
constexpr char kFontRepositoryBase[] =
    "https://raw.githubusercontent.com/obijuankenobiii/inx-font/main/";
constexpr char kDownloadPath[] = "/.system/font-package.zip";
constexpr size_t kMaxPackageBytes = 5 * 1024 * 1024;
constexpr size_t kMaxExtractedBytes = 16 * 1024 * 1024;

struct StaticPackage {
  const char* name;
  const char* variant;
  const char* path;
  FontPackageManager::Category category;
};

constexpr StaticPackage kStaticPackages[] = {
    {"Alegreya", "1-bit", "1bit/Alegreya.zip", FontPackageManager::Category::Serif},
    {"AtkinsonHL-Mono", "1-bit", "1bit/AtkinsonHL-Mono.zip", FontPackageManager::Category::SansSerif},
    {"AtkinsonHL-Next", "1-bit", "1bit/AtkinsonHL-Next.zip", FontPackageManager::Category::SansSerif},
    {"BitterPro", "1-bit", "1bit/BitterPro.zip", FontPackageManager::Category::Serif},
    {"Charis", "1-bit", "1bit/Charis.zip", FontPackageManager::Category::Serif},
    {"Inter", "1-bit", "1bit/Inter.zip", FontPackageManager::Category::SansSerif},
    {"Lexend", "1-bit", "1bit/Lexend.zip", FontPackageManager::Category::SansSerif},
    {"LexicaUltralegible", "1-bit", "1bit/LexicaUltralegible.zip", FontPackageManager::Category::SansSerif},
    {"Lora", "1-bit", "1bit/Lora.zip", FontPackageManager::Category::Serif},
    {"Merriweather", "1-bit", "1bit/Merriweather.zip", FontPackageManager::Category::Serif},
    {"NotoSans", "1-bit", "1bit/NotoSans.zip", FontPackageManager::Category::SansSerif},
    {"OpenDyslexic", "1-bit", "1bit/OpenDyslexic.zip", FontPackageManager::Category::SansSerif},
    {"PlexMono", "1-bit", "1bit/PlexMono.zip", FontPackageManager::Category::SansSerif},
    {"PlexSans", "1-bit", "1bit/PlexSans.zip", FontPackageManager::Category::SansSerif},
    {"SourceSans3", "1-bit", "1bit/SourceSans3.zip", FontPackageManager::Category::SansSerif},
    {"SourceSerif4", "1-bit", "1bit/SourceSerif4.zip", FontPackageManager::Category::Serif},
    {"Tinos", "1-bit", "1bit/Tinos.zip", FontPackageManager::Category::Serif},
    {"Alegreya", "2-bit", "2bit/Alegreya.zip", FontPackageManager::Category::Serif},
    {"AtkinsonHL-Mono", "2-bit", "2bit/AtkinsonHL-Mono.zip", FontPackageManager::Category::SansSerif},
    {"AtkinsonHL-Next", "2-bit", "2bit/AtkinsonHL-Next.zip", FontPackageManager::Category::SansSerif},
    {"BitterPro", "2-bit", "2bit/BitterPro.zip", FontPackageManager::Category::Serif},
    {"Charis", "2-bit", "2bit/Charis.zip", FontPackageManager::Category::Serif},
    {"Inter", "2-bit", "2bit/Inter.zip", FontPackageManager::Category::SansSerif},
    {"Lexend", "2-bit", "2bit/Lexend.zip", FontPackageManager::Category::SansSerif},
    {"LexicaUltralegible", "2-bit", "2bit/LexicaUltralegible.zip", FontPackageManager::Category::SansSerif},
    {"Lora", "2-bit", "2bit/Lora.zip", FontPackageManager::Category::Serif},
    {"Merriweather", "2-bit", "2bit/Merriweather.zip", FontPackageManager::Category::Serif},
    {"NotoSans", "2-bit", "2bit/NotoSans.zip", FontPackageManager::Category::SansSerif},
    {"OpenDyslexic", "2-bit", "2bit/OpenDyslexic.zip", FontPackageManager::Category::SansSerif},
    {"PlexMono", "2-bit", "2bit/PlexMono.zip", FontPackageManager::Category::SansSerif},
    {"PlexSans", "2-bit", "2bit/PlexSans.zip", FontPackageManager::Category::SansSerif},
    {"SourceSans3", "2-bit", "2bit/SourceSans3.zip", FontPackageManager::Category::SansSerif},
    {"SourceSerif4", "2-bit", "2bit/SourceSerif4.zip", FontPackageManager::Category::Serif},
    {"Tinos", "2-bit", "2bit/Tinos.zip", FontPackageManager::Category::Serif},
};

bool isSafeBinName(const std::string& name) {
  if (!StringUtils::checkFileExtension(name, ".bin")) return false;
  if (name.empty() || name == "." || name == "..") return false;
  for (const char c : name) {
    if (c == '/' || c == '\\' || c == ':' || c < 32) return false;
  }
  return true;
}

std::string packageFamily(const FontPackageManager::Package& package, const std::string& entryName) {
  if (!package.installFamily.empty()) {
    return StringUtils::sanitizeFilename(package.installFamily, 48);
  }

  const size_t slash = entryName.find('/');
  if (slash != std::string::npos && slash > 0 && entryName.compare(0, slash, "__MACOSX") != 0) {
    return StringUtils::sanitizeFilename(entryName.substr(0, slash), 48);
  }
  return StringUtils::sanitizeFilename(package.name, 48);
}

std::string entryBaseName(const std::string& entryName) {
  const size_t slash = entryName.rfind('/');
  return slash == std::string::npos ? entryName : entryName.substr(slash + 1);
}

bool removeDirectoryTree(const std::string& path) {
  FsFile directory = SdMan.open(path.c_str());
  if (!directory || !directory.isDirectory()) {
    if (directory) directory.close();
    return false;
  }

  bool removed = true;
  char name[128] = {};
  while (true) {
    FsFile entry = directory.openNextFile();
    if (!entry) break;
    entry.getName(name, sizeof(name));
    const bool isDirectory = entry.isDirectory();
    entry.close();

    const std::string childPath = path + "/" + name;
    if (isDirectory) {
      if (!removeDirectoryTree(childPath)) removed = false;
    } else if (!SdMan.remove(childPath.c_str())) {
      removed = false;
    }
  }
  directory.close();

  return removed && SdMan.removeDir(path.c_str());
}
}

bool FontPackageManager::fetchAvailable(std::vector<Package>& packages, std::string& error) {
  packages.clear();
  error.clear();

  packages.reserve(sizeof(kStaticPackages) / sizeof(kStaticPackages[0]));
  for (const StaticPackage& item : kStaticPackages) {
    const std::string installFamily = std::string(item.name) + " " + item.variant;
    FontPackageManager::Package package;
    package.name = item.name;
    package.variant = item.variant;
    package.url = std::string(kFontRepositoryBase) + item.path;
    package.installFamily = installFamily;
    package.category = item.category;
    packages.push_back(std::move(package));
  }
  return true;
}

bool FontPackageManager::isInstalled(const Package& package) {
  if (!SdMan.ready()) return false;

  SdIoMutex::Lock ioLock;
  const std::string family = packageFamily(package, "");
  const std::string familyPath = std::string("/fonts/") + family;
  FsFile directory = SdMan.open(familyPath.c_str());
  if (!directory || !directory.isDirectory()) {
    if (directory) directory.close();
    return false;
  }

  bool installed = false;
  for (FsFile file = directory.openNextFile(); file; file = directory.openNextFile()) {
    char name[128] = {0};
    file.getName(name, sizeof(name));
    if (!file.isDirectory() && isSafeBinName(name)) {
      installed = true;
      file.close();
      break;
    }
    file.close();
  }
  directory.close();
  return installed;
}

bool FontPackageManager::install(const Package& package, std::string& error, ProgressCallback progress) {
  error.clear();
  if (package.url.empty()) {
    error = "Invalid font package";
    return false;
  }
  if (package.size > kMaxPackageBytes) {
    error = "Font package is larger than 5 MB";
    return false;
  }
  if (!SdMan.ready()) {
    error = "SD card is not ready";
    return false;
  }

  SdIoMutex::Lock ioLock;
  SdMan.mkdir("/.system");
  if (HttpDownloader::downloadToFile(package.url, kDownloadPath, "", "", progress) != HttpDownloader::OK) {
    error = "Font download failed";
    return false;
  }

  FsFile archiveFile = SdMan.open(kDownloadPath);
  const size_t downloadedSize = archiveFile ? archiveFile.size() : 0;
  if (archiveFile) archiveFile.close();
  if (downloadedSize == 0 || downloadedSize > kMaxPackageBytes) {
    SdMan.remove(kDownloadPath);
    error = "Invalid font package size";
    return false;
  }

  ZipFile zip{std::string(kDownloadPath)};
  if (!zip.open() || !zip.loadAllFileStatSlims()) {
    zip.close();
    SdMan.remove(kDownloadPath);
    error = "Font package is not a valid ZIP";
    return false;
  }

  SdMan.mkdir("/fonts");
  size_t extractedBytes = 0;
  int extractedFiles = 0;
  for (const std::string& rawName : zip.fileNames()) {
    const std::string entryName = FsHelpers::normalisePath(rawName);
    const std::string baseName = entryBaseName(entryName);
    if (entryName.empty() || entryName.find("__MACOSX/") == 0 || !isSafeBinName(baseName)) continue;

    size_t inflatedSize = 0;
    if (!zip.getInflatedFileSize(entryName.c_str(), &inflatedSize) || inflatedSize == 0 ||
        extractedBytes + inflatedSize > kMaxExtractedBytes) {
      continue;
    }

    const std::string family = packageFamily(package, entryName);
    const std::string familyPath = std::string("/fonts/") + family;
    const std::string outputPath = familyPath + "/" + StringUtils::sanitizeFilename(baseName, 96);
    SdMan.mkdir(familyPath.c_str());

    FsFile output;
    if (!SdMan.openFileForWrite("FONT", outputPath, output)) {
      zip.close();
      SdMan.remove(kDownloadPath);
      error = "Could not create font file on SD card";
      return false;
    }
    const bool copied = zip.readFileToStream(entryName.c_str(), output, 1024);
    output.close();
    if (!copied) {
      SdMan.remove(outputPath.c_str());
      zip.close();
      SdMan.remove(kDownloadPath);
      error = "Could not extract font package";
      return false;
    }
    extractedBytes += inflatedSize;
    ++extractedFiles;
  }

  zip.close();
  SdMan.remove(kDownloadPath);
  if (extractedFiles == 0) {
    error = "No compiled font files found in package";
    return false;
  }

  FontManager::scanSDFonts("/fonts", true);
  return true;
}

bool FontPackageManager::remove(const Package& package, std::string& error) {
  error.clear();
  if (!SdMan.ready()) {
    error = "SD card is not ready";
    return false;
  }

  const std::string family = packageFamily(package, "");
  if (family.empty() || family == "." || family == "..") {
    error = "Invalid font package";
    return false;
  }

  const std::string familyPath = std::string("/fonts/") + family;
  SdIoMutex::Lock ioLock;
  if (!SdMan.exists(familyPath.c_str())) {
    error = "Font is not installed";
    return false;
  }

  FontManager::unloadAllSDFonts();
  if (!removeDirectoryTree(familyPath)) {
    error = "Could not remove font files";
    return false;
  }

  FontManager::scanSDFonts("/fonts", true);
  return true;
}
