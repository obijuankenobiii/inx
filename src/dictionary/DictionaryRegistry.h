#pragma once

/**
 * @file DictionaryRegistry.h
 * @brief Discovers StarDict folders under /dictionaries and maps them to language tags.
 *
 * Language comes from, in order: a persisted override, the .ifo `lang=` field, then the folder name.
 * Only one dictionary is kept open at a time by StarDictLookup; this registry is metadata only.
 */

#include <cstddef>
#include <string>
#include <vector>

class DictionaryRegistry {
 public:
  struct Entry {
    std::string folder;
    std::string lang;  // source language of headwords (en for both English/ and en-nl/)
    std::string bookname;
    bool bilingual = false;  // folder name is xx-yy with different source/target (en-nl, fr-nl, la-nl)
  };

  static constexpr const char* kDictionariesRoot = "/dictionaries";

  static std::string primaryLanguageTag(const std::string& bcp47);
  static std::string inferLangFromName(const std::string& name);
  static std::string langLabel(const std::string& lang);
  /** Source-language label on the lookup card, including bilingual folders. */
  static std::string displayLabel(const std::string& folder, const std::string& lang);
  /** Gloss language of a bilingual folder (en-nl → "nl"). Empty for monolingual dictionaries. */
  static std::string targetLangFromFolder(const std::string& folder);
  static bool folderLooksLikeDictionary(const std::string& folderPath);
  static std::string readIfoLang(const std::string& folderPath);
  static std::string readIfoBookname(const std::string& folderPath);

  static std::vector<Entry> scan();
  static std::string folderForLanguage(const std::string& epubLanguage, const char* fallbackFolder);
  /**
   * Picks a dictionary folder for a lookup. Order: confident passage language, then the session's
   * last-used folder, then the EPUB's language, then the settings fallback. Book language is a weak
   * prior on purpose — mixed books (Ulysses, etc.) need the surrounding words, not dc:language.
   */
  static std::string folderForLookup(const std::string& detectedLang, const std::string& sessionFolder,
                                     const std::string& epubLanguage, const char* fallbackFolder);
  static std::vector<std::string> foldersInFallbackOrder(const std::string& preferredFolder);
  /**
   * Auto-lookup order: the preferred folder, then other dictionaries of the same source language
   * (translation dicts before monolingual). Does not open unrelated languages — cycling Left/Right
   * still uses foldersInFallbackOrder for that.
   */
  static std::vector<std::string> foldersForAutoLookup(const std::string& preferredFolder);
  /** True when @p folder looks like a bilingual pair (en-nl, fr_nl). */
  static bool folderIsBilingual(const std::string& folder);

  /**
   * Scores a window of nearby words. Returns a source-language tag, or empty when the signal is too
   * weak to override the session/book fallback. @p focusIndex is the looked-up word inside @p words.
   */
  static std::string detectLanguage(const std::vector<std::string>& words, size_t focusIndex);

  static std::string overrideFor(const std::string& folder);
  static void setOverride(const std::string& folder, const std::string& lang);

  static const char* const kLangCycle[];
  static constexpr int kLangCycleCount = 5;
  static int langCycleIndex(const std::string& lang);
};

#define DICT_REGISTRY DictionaryRegistry
