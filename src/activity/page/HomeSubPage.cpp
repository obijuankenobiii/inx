#include "HomeSubPage.h"

#include <Epub/BookMetadataCache.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <algorithm>
#include <cstdio>
#include <functional>

#include "components/global/PopUp.h"
#include "state/BookState.h"
#include "state/RecentBooks.h"
#include "state/SavedDictionaryWords.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/ScreenComponents.h"
#include "system/UiLayout.h"

extern void onGoToReader(const std::string& path);

namespace {
constexpr unsigned long kLongPressMs = 500;
constexpr int kTop = UiLayout::PAGE_HEADER_HEIGHT;
constexpr int kBottom = 24;
constexpr int kRowHeight = UiLayout::LIST_ITEM_HEIGHT;

std::string cacheForBook(const RecentBook& book) {
  return book.cachePath.empty() ? "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(book.path))
                                : book.cachePath;
}

std::string titleForCache(const std::string& cache) {
  BookMetadataCache metadata(cache);
  if (metadata.load() && !metadata.coreMetadata.title.empty()) return metadata.coreMetadata.title;
  return cache;
}

std::vector<std::string> cacheDirectories() {
  std::vector<std::string> result;
  FsFile root = SdMan.open("/.metadata/epub");
  if (!root || !root.isDirectory()) { if (root) root.close(); return result; }
  char name[96] = {};
  while (true) {
    FsFile entry = root.openNextFile();
    if (!entry) break;
    if (entry.isDirectory()) { entry.getName(name, sizeof(name)); result.emplace_back(std::string("/.metadata/epub/") + name); }
    entry.close();
  }
  root.close();
  return result;
}

struct LegacyBookmark {
  uint16_t spineIndex;
  uint16_t pageNumber;
  uint16_t pageCount;
  char chapterTitle[64];
  uint32_t timestamp;
  bool isValid() const { return spineIndex != 0xFFFF && pageNumber != 0xFFFF; }
};

template <typename T>
bool readValue(FsFile& file, T& value) {
  return file.read(&value, sizeof(value)) == sizeof(value);
}

template <typename Callback>
void readAnnotationFile(const std::string& path, const int spine, const int page, Callback callback) {
  FsFile file;
  if (!SdMan.openFileForRead("HMB", path.c_str(), file)) return;
  uint32_t magic = 0;
  uint16_t count = 0;
  if (!readValue(file, magic) || magic != 0x334E4E41 || !readValue(file, count)) { file.close(); return; }
  for (uint16_t i = 0; i < count && i < 250; ++i) {
    uint32_t timestamp = 0;
    uint16_t length = 0;
    if (!readValue(file, timestamp) || !readValue(file, length)) break;
    std::string text;
    text.resize(length);
    if (length > 0 && file.read(&text[0], length) != length) break;
    uint16_t ignored[8] = {};
    if (file.read(ignored, sizeof(ignored)) != sizeof(ignored)) break;
    callback(text, spine, page);
  }
  file.close();
}

bool removeAnnotationRecord(const std::string& path, const std::string& target) {
  FsFile file;
  if (!SdMan.openFileForRead("HMB", path.c_str(), file)) return false;
  uint32_t magic = 0;
  uint16_t count = 0;
  if (!readValue(file, magic) || magic != 0x334E4E41 || !readValue(file, count)) { file.close(); return false; }
  struct Record { uint32_t timestamp; std::string text; uint16_t fields[8]; };
  std::vector<Record> records;
  records.reserve(count);
  for (uint16_t i = 0; i < count && i < 250; ++i) {
    Record record{};
    uint16_t length = 0;
    if (!readValue(file, record.timestamp) || !readValue(file, length)) break;
    record.text.resize(length);
    if (length > 0 && file.read(&record.text[0], length) != length) break;
    if (file.read(record.fields, sizeof(record.fields)) != sizeof(record.fields)) break;
    records.push_back(std::move(record));
  }
  file.close();
  const auto found = std::find_if(records.begin(), records.end(), [&](const Record& record) {
    return record.text == target;
  });
  if (found == records.end()) return false;
  records.erase(found);
  if (records.empty()) return SdMan.remove(path.c_str());
  if (!SdMan.openFileForWrite("HMB", path.c_str(), file)) return false;
  file.write(&magic, sizeof(magic));
  const uint16_t newCount = static_cast<uint16_t>(records.size());
  file.write(&newCount, sizeof(newCount));
  for (const Record& record : records) {
    file.write(&record.timestamp, sizeof(record.timestamp));
    const uint16_t length = static_cast<uint16_t>(record.text.size());
    file.write(&length, sizeof(length));
    if (length > 0) file.write(record.text.data(), length);
    file.write(record.fields, sizeof(record.fields));
  }
  file.close();
  return true;
}

std::string bookPathForCache(const std::string& cache) {
  for (const RecentBook& book : RECENT_BOOKS.getBooks()) if (cacheForBook(book) == cache) return book.path;
  for (const BookState::Book& book : BOOK_STATE.getAllBooks()) {
    if (cache == "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(book.path))) return book.path;
  }
  return {};
}
}

HomeSubPage::HomeSubPage(GfxRenderer& renderer, MappedInputManager& mappedInput, const Section section,
                         std::function<void()> onBack)
    : Page("Home shortcut", renderer, mappedInput), section_(section), onBack_(std::move(onBack)) {}

const char* HomeSubPage::pageTitle() const {
  switch (section_) {
    case Section::Bookmarks: return "Bookmarks";
    case Section::Highlights: return "Highlights";
    case Section::Favorites: return "Favorites";
    case Section::Dictionary: return "Dictionary";
  }
  return "";
}

const char* HomeSubPage::emptyLabel() const {
  switch (section_) {
    case Section::Bookmarks: return "No bookmarks yet";
    case Section::Highlights: return "No highlights yet";
    case Section::Favorites: return "No favorites yet";
    case Section::Dictionary: return "No saved words yet";
  }
  return "";
}

void HomeSubPage::onEnter() {
  Page::onEnter();
  selected_ = 0;
  popupAction_ = 0;
  popupOpen_ = false;
  longPressHandled_ = false;
  load();
}

void HomeSubPage::load() {
  rows_.clear();
  if (section_ == Section::Favorites) {
    for (const BookState::Book& book : BOOK_STATE.getFavoriteBooks())
      rows_.push_back({book.title.empty() ? book.path : book.title, book.path, {}, -1, -1});
  } else if (section_ == Section::Dictionary) {
    for (int i = 0; i < SAVED_WORDS.count(); ++i) rows_.push_back({SAVED_WORDS.wordAt(i), {}, {}, -1, -1, true, i});
  } else {
    for (const std::string& cache : cacheDirectories()) {
      const std::string bookTitle = titleForCache(cache);
      const std::string path = bookPathForCache(cache);
      if (section_ == Section::Bookmarks) {
        FsFile file;
        if (!SdMan.openFileForRead("HMB", (cache + "/bookmarks.bin").c_str(), file)) continue;
        const int count = static_cast<int>(file.fileSize() / sizeof(LegacyBookmark));
        for (int i = 0; i < count && i < 64; ++i) {
          LegacyBookmark bookmark{};
          if (file.read(&bookmark, sizeof(bookmark)) != sizeof(bookmark) || !bookmark.isValid()) continue;
          char page[24] = {};
          std::snprintf(page, sizeof(page), " p%d", static_cast<int>(bookmark.pageNumber) + 1);
          rows_.push_back({bookTitle + " - " + std::string(bookmark.chapterTitle) + page, path, cache,
                           bookmark.spineIndex, bookmark.pageNumber});
        }
        file.close();
      } else {
        const std::string annDir = cache + "/ann";
        if (!SdMan.exists(annDir.c_str())) continue;
        for (const String& fileName : SdMan.listFiles(annDir.c_str())) {
          int spine = 0, page = 0;
          if (std::sscanf(fileName.c_str(), "s_%d_p_%d.bin", &spine, &page) != 2) continue;
          readAnnotationFile(cache + "/ann/" + std::string(fileName.c_str()), spine, page,
                             [&](const std::string& text, const int, const int) {
                               rows_.push_back(Row{text.empty() ? bookTitle : text, path, cache, spine, page});
                               rows_.back().annotationText = text;
                             });
        }
      }
    }
  }
  if (selected_ >= static_cast<int>(rows_.size())) selected_ = std::max(0, static_cast<int>(rows_.size()) - 1);
}

bool HomeSubPage::isDeletable() const { return selected_ >= 0 && selected_ < static_cast<int>(rows_.size()); }

void HomeSubPage::loop() {
  if (popupOpen_) {
    popupInput();
    renderIfNeeded();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (onBack_) onBack_();
    return;
  }
  if (rows_.empty()) { renderIfNeeded(); return; }
  if (mappedInput.wasPressed(itemPrevButton())) { selected_ = (selected_ + rows_.size() - 1) % rows_.size(); requestRender(); return; }
  if (mappedInput.wasPressed(itemNextButton())) { selected_ = (selected_ + 1) % rows_.size(); requestRender(); return; }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) longPressHandled_ = false;
  if (mappedInput.isPressed(MappedInputManager::Button::Confirm) && !longPressHandled_ &&
      mappedInput.getHeldTime() >= kLongPressMs) {
    popupOpen_ = isDeletable();
    popupAction_ = 0;
    longPressHandled_ = true;
    requestRender();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const bool held = longPressHandled_ || mappedInput.getHeldTime() >= kLongPressMs;
    longPressHandled_ = false;
    if (held) return;
    openSelected();
    return;
  }
  renderIfNeeded();
}

void HomeSubPage::openSelected() {
  if (!isDeletable()) return;
  const Row& row = rows_[static_cast<size_t>(selected_)];
  if (!row.path.empty()) onGoToReader(row.path);
  else { requestRender(); }
}

void HomeSubPage::deleteSelected() {
  if (!isDeletable()) return;
  const Row row = rows_[static_cast<size_t>(selected_)];
  if (row.dictionary) SAVED_WORDS.remove(row.label);
  else if (section_ == Section::Favorites) BOOK_STATE.toggleFavorite(row.path);
  else if (section_ == Section::Bookmarks) {
    FsFile file;
    std::vector<LegacyBookmark> bookmarks;
    if (SdMan.openFileForRead("HMB", (row.cachePath + "/bookmarks.bin").c_str(), file)) {
      const int count = static_cast<int>(file.fileSize() / sizeof(LegacyBookmark));
      bookmarks.resize(std::max(0, count));
      if (!bookmarks.empty()) file.read(bookmarks.data(), bookmarks.size() * sizeof(LegacyBookmark));
      file.close();
      bookmarks.erase(std::remove_if(bookmarks.begin(), bookmarks.end(), [&](const LegacyBookmark& b) {
        return b.spineIndex == row.spine && b.pageNumber == row.page;
      }), bookmarks.end());
      if (bookmarks.empty()) SdMan.remove((row.cachePath + "/bookmarks.bin").c_str());
      else if (SdMan.openFileForWrite("HMB", (row.cachePath + "/bookmarks.bin").c_str(), file)) {
        file.write(bookmarks.data(), bookmarks.size() * sizeof(LegacyBookmark)); file.close();
      }
    }
  } else if (section_ == Section::Highlights) {
    char fileName[48] = {};
    std::snprintf(fileName, sizeof(fileName), "/ann/s_%05d_p_%05d.bin", row.spine, row.page);
    removeAnnotationRecord(row.cachePath + fileName, row.annotationText);
  }
  load();
  popupOpen_ = false;
  requestRender();
}

bool HomeSubPage::popupInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) { popupOpen_ = false; requestRender(); return true; }
  if (mappedInput.wasPressed(itemPrevButton()) || mappedInput.wasPressed(itemNextButton())) {
    popupAction_ = (popupAction_ + 1) % 2; requestRender(); return true;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (popupAction_ == 0) deleteSelected(); else { popupOpen_ = false; requestRender(); }
    return true;
  }
  return true;
}

void HomeSubPage::menu() { ScreenComponents::drawSubPageHeader(renderer, pageTitle()); }

void HomeSubPage::content() {
  const int font = systemFontId();
  if (rows_.empty()) {
    renderer.text.centered(font, (kTop + renderer.getScreenHeight() - kBottom) / 2, emptyLabel());
  } else {
    for (size_t i = 0; i < rows_.size() && kTop + static_cast<int>(i) * kRowHeight < renderer.getScreenHeight() - kBottom; ++i) {
      const int y = kTop + static_cast<int>(i) * kRowHeight;
      const bool active = static_cast<int>(i) == selected_ && !popupOpen_;
      if (active) renderer.rectangle.fill(0, y, renderer.getScreenWidth(), kRowHeight, true, true, true);
      const std::string text = renderer.text.truncate(font, rows_[i].label.c_str(), renderer.getScreenWidth() - 48);
      renderer.text.render(font, 28, y + (kRowHeight - renderer.text.getLineHeight(font)) / 2, text.c_str(), !active);
    }
  }
  if (popupOpen_) renderPopup();
}

void HomeSubPage::renderPopup() const {
  const std::vector<std::string> actions = {"Yes", "No"};
  const PopUpBounds box = PopUp::bounds(renderer, static_cast<int>(actions.size()), kTop);
  PopUp::background(renderer, box);
  PopUp::title(renderer, box, "Delete item?");
  PopUp::list(renderer, box, actions, popupAction_, 0);
  PopUp::border(renderer, box);
}
