#pragma once

/**
 * @file CssParser.h
 * @brief Public interface and types for CssParser.
 */

#include <map>
#include <string>
#include <vector>

class CssParser {
 public:
  struct CssRule {
    std::string selector;
    /** Lowercase selector; filled at parse time so hot paths do not reallocate/transform on every lookup. */
    std::string selectorLower;
    std::map<std::string, std::string> properties;
  };

  CssParser();
  ~CssParser();

  void parse(const std::string& cssContent);
  void parseFile(const std::string& filepath);
  void clear();

  int getWidth(const std::string& className, const std::string& id, const std::string& styleAttr, int viewportWidth,
               int viewportHeight) const;
  int getHeight(const std::string& className, const std::string& id, const std::string& styleAttr, int viewportWidth,
                int viewportHeight) const;
  int getMaxWidth(const std::string& className, const std::string& id, const std::string& styleAttr, int viewportWidth,
                  int viewportHeight) const;
  int getMinWidth(const std::string& className, const std::string& id, const std::string& styleAttr, int viewportWidth,
                  int viewportHeight) const;
  int getMaxHeight(const std::string& className, const std::string& id, const std::string& styleAttr, int viewportWidth,
                   int viewportHeight) const;
  int getMinHeight(const std::string& className, const std::string& id, const std::string& styleAttr, int viewportWidth,
                   int viewportHeight) const;

  /**
   * Parse a single CSS length (e.g. HTML width="50%" or style value).
   * @param percentOfWidth When true, % uses viewportWidth; when false, % uses viewportHeight.
   */
  int parseCssLength(const std::string& value, int viewportWidth, int viewportHeight,
                     bool percentOfWidth = true) const;

  /**
   * Resolves paragraph alignment from inline style, class/id/type rules, then body/html defaults in CSS.
   * Return values match TextBlock::Style / SystemSetting (0 justify, 1 left, 2 center, 3 right).
   * @param elementTagLower HTML tag in lower case (e.g. "p", "li") for type selectors like "p { ... }".
   */
  uint8_t computeParagraphAlignment(const std::string& className, const std::string& id, const std::string& styleAttr,
                                    const std::string& elementTagLower = "") const;

  /** Resolved first-line text-indent in pixels (>= 0) from inline then stylesheet, including type selectors. */
  int getTextIndentPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                      const std::string& styleAttr, int viewportWidth, int viewportHeight) const;

  /** True if `text-indent` appears in inline style or matching stylesheet rules (including value 0). */
  bool hasTextIndentSpecified(const std::string& elementTagLower, const std::string& className, const std::string& id,
                              const std::string& styleAttr) const;

  size_t getRuleCount() const { return rules.size(); }

 private:
  std::vector<CssRule> rules;
  std::string bodyTextAlignRaw;

  void noteBodyHtmlTextAlign(const std::string& selectorRaw, const std::map<std::string, std::string>& properties);
  std::string getCascadedPropertyValue(const std::string& propName, const std::string& className,
                                       const std::string& id, const std::string& styleAttr,
                                       const std::string& elementTagLower = "") const;
  int mapTextAlignToStyleIndex(const std::string& rawValue) const;

  void parsePropertiesForDimensions(const std::string& propertiesStr, std::map<std::string, std::string>& properties) const;
  enum class PercentRefersTo { Width, Height };
  int parseDimensionValue(const std::string& value, int viewportWidth, int viewportHeight,
                          PercentRefersTo percentAxis) const;
  void parseInlineStyle(const std::string& styleAttr, std::map<std::string, std::string>& out) const;
  int getInlineOrSheetLength(const std::string& propName, const std::string& className, const std::string& id,
                             const std::string& styleAttr, int viewportWidth, int viewportHeight) const;

  static bool selectorHasIdToken(const std::string& selectorLower, const std::string& idLower);
  static bool selectorHasClassToken(const std::string& selectorLower, const std::string& classTokenLower);
  bool ruleMatchesElement(const CssRule& rule, const std::string& classAttr, const std::string& idAttr) const;

  std::string trim(const std::string& str) const;
  std::string toLower(const std::string& str) const;

  std::map<std::string, std::string> getCombinedPropertiesForElement(const std::string& elementName,
                                                                     const std::string& className,
                                                                     const std::string& id) const;
};
