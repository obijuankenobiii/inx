#include "ReaderActivity.h"

#include "Epub.h"
#include "Epub/EpubActivity.h"
#include "Txt.h"
#include "TxtReaderActivity.h"
#include "Xtc.h"
#include "XtcReaderActivity.h"
#include "activity/util/FullScreenMessageActivity.h"
#include "util/StringUtils.h"

/**
 * @brief Extracts the parent directory path from a file path
 * @param filePath Full path to a file
 * @return Path to the containing directory
 */
std::string ReaderActivity::extractFolderPath(const std::string& filePath) {
  const auto lastSlash = filePath.find_last_of('/');
  if (lastSlash == std::string::npos || lastSlash == 0) {
    return "/";
  }
  return filePath.substr(0, lastSlash);
}

/**
 * @brief Checks if the file is an XTC format file
 * @param path Path to the file
 * @return true if file has .xtc or .xtch extension
 */
bool ReaderActivity::isXtcFile(const std::string& path) {
  return StringUtils::checkFileExtension(path, ".xtc") || StringUtils::checkFileExtension(path, ".xtch");
}

/**
 * @brief Checks if the file is a plain text file
 * @param path Path to the file
 * @return true if file has .txt or .md extension
 */
bool ReaderActivity::isTxtFile(const std::string& path) {
  return StringUtils::checkFileExtension(path, ".txt") ||
         StringUtils::checkFileExtension(path, ".md");
}

/**
 * @brief Loads an EPUB file from the given path
 * @param path Path to the EPUB file
 * @return Unique pointer to loaded Epub object, or nullptr if loading fails
 */
std::unique_ptr<Epub> ReaderActivity::loadEpub(const std::string& path) {
  if (!SdMan.exists(path.c_str())) {
    return nullptr;
  }

  auto epub = std::unique_ptr<Epub>(new Epub(path, "/.metadata"));
  if (epub->load()) {
    return epub;
  }

  return nullptr;
}

/**
 * @brief Loads an XTC file from the given path
 * @param path Path to the XTC file
 * @return Unique pointer to loaded Xtc object, or nullptr if loading fails
 */
std::unique_ptr<Xtc> ReaderActivity::loadXtc(const std::string& path) {
  if (!SdMan.exists(path.c_str())) {
    return nullptr;
  }

  auto xtc = std::unique_ptr<Xtc>(new Xtc(path, "/.system"));
  if (xtc->load()) {
    return xtc;
  }

  return nullptr;
}

/**
 * @brief Loads a TXT file from the given path
 * @param path Path to the TXT file
 * @return Unique pointer to loaded Txt object, or nullptr if loading fails
 */
std::unique_ptr<Txt> ReaderActivity::loadTxt(const std::string& path) {
  if (!SdMan.exists(path.c_str())) {
    return nullptr;
  }

  auto txt = std::unique_ptr<Txt>(new Txt(path, "/.system"));
  if (txt->load()) {
    return txt;
  }

  return nullptr;
}

/**
 * @brief Transitions to the EPUB reader activity with the loaded EPUB
 * @param epub Unique pointer to loaded Epub object
 */
void ReaderActivity::onGoToEpubReader(std::unique_ptr<Epub> epub) {
  // Capture the path in the lambda directly
  std::string bookPath = epub->getPath();
  
  exitActivity();
  enterNewActivity(
    new EpubActivity(
      renderer, 
      mappedInput, 
      std::move(epub), 
      [this, bookPath] {  // Capture bookPath by value
        if (onGoBack) {
          onGoBack(bookPath);  // Use captured path
        }
      },
      []{}));
}

/**
 * @brief Transitions to the XTC reader activity with the loaded XTC
 * @param xtc Unique pointer to loaded Xtc object
 */
void ReaderActivity::onGoToXtcReader(std::unique_ptr<Xtc> xtc) {
  const auto xtcPath = xtc->getPath();
  currentBookPath = xtcPath;
  exitActivity();
  enterNewActivity(new XtcReaderActivity(
      renderer, 
      mappedInput, 
      std::move(xtc), 
      [this, xtcPath] { onGoBack(xtcPath); }, 
      [this] { onGoBack(""); }));
}

/**
 * @brief Transitions to the TXT reader activity with the loaded TXT
 * @param txt Unique pointer to loaded Txt object
 */
void ReaderActivity::onGoToTxtReader(std::unique_ptr<Txt> txt) {
  const auto txtPath = txt->getPath();
  currentBookPath = txtPath;
  exitActivity();
  enterNewActivity(new TxtReaderActivity(
      renderer, 
      mappedInput, 
      std::move(txt), 
      [this] { 
        if (onGoBack) {
          onGoBack(currentBookPath);
        }
      },
      []{}));
}

/**
 * @brief Called when entering the reader activity
 */
void ReaderActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  if (initialBookPath.empty()) {
    if (onGoBack) {
      onGoBack("");
    }
    return;
  }

  currentBookPath = initialBookPath;

  if (isXtcFile(initialBookPath)) {
    auto xtc = loadXtc(initialBookPath);
    if (!xtc) {
      if (onGoBack) {
        onGoBack(currentBookPath);
      }
      return;
    }
    onGoToXtcReader(std::move(xtc));
  } else if (isTxtFile(initialBookPath)) {
    auto txt = loadTxt(initialBookPath);
    if (!txt) {
      if (onGoBack) {
        onGoBack(currentBookPath);
      }
      return;
    }
    onGoToTxtReader(std::move(txt));
  } else {
    auto epub = loadEpub(initialBookPath);
    if (!epub) {
      if (onGoBack) {
        onGoBack(currentBookPath);
      }
      return;
    }
    onGoToEpubReader(std::move(epub));
  }
}