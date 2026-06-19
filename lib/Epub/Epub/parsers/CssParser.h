#pragma once

/**
 * @file CssParser.h
 * @brief Public interface and types for CssParser.
 */

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class CssParser {
 public:
  struct CssRule {
    /** Lowercase selector; filled at parse time so hot paths do not reallocate/transform on every lookup. */
    std::string selectorLower;
    /** Index into sourcePaths_ (interned) — avoids storing the full CSS file path on every rule. */
    uint16_t sourcePathIndex = 0;
    /** Precomputed selector classifications so per-element scans avoid re-running string searches. */
    bool isPseudoElement = false;
    bool isFirstLetterPseudo = false;
    std::map<std::string, std::string> properties;
  };

  CssParser();
  ~CssParser();

  void parse(const std::string& cssContent, const std::string& sourcePath = "");
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

  /** True if `text-align` is specified inline or by matching stylesheet rules for this element. */
  bool hasTextAlignSpecified(const std::string& elementTagLower, const std::string& className, const std::string& id,
                             const std::string& styleAttr) const;
  bool isDisplayBlock(const std::string& elementTagLower, const std::string& className, const std::string& id,
                      const std::string& styleAttr) const;

  /** Resolved inherited CSS font emphasis for the current element. */
  bool resolveFontBold(const std::string& elementTagLower, const std::string& className, const std::string& id,
                       const std::string& styleAttr, bool inheritedBold) const;
  bool resolveFontItalic(const std::string& elementTagLower, const std::string& className, const std::string& id,
                         const std::string& styleAttr, bool inheritedItalic) const;
  bool resolveSmallCaps(const std::string& elementTagLower, const std::string& className, const std::string& id,
                        const std::string& styleAttr, bool inheritedSmallCaps) const;
  bool hasFirstLetterDropCapHint(const std::string& elementTagLower, const std::string& className,
                                 const std::string& id, const std::string& styleAttr) const;
  uint8_t getFirstLetterDropCapLineCount(const std::string& elementTagLower, const std::string& className,
                                         const std::string& id, const std::string& styleAttr) const;

  /** Resolved first-line text-indent in pixels (>= 0) from inline then stylesheet, including type selectors. */
  int getTextIndentPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                      const std::string& styleAttr, int viewportWidth, int viewportHeight) const;

  /** True if `text-indent` appears in inline style or matching stylesheet rules (including value 0). */
  bool hasTextIndentSpecified(const std::string& elementTagLower, const std::string& className, const std::string& id,
                              const std::string& styleAttr) const;

  /** Resolved paragraph spacing from CSS block margins/padding in pixels. */
  int getParagraphSpacingTopPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                               const std::string& styleAttr, int viewportWidth, int viewportHeight) const;
  int getParagraphSpacingBottomPx(const std::string& elementTagLower, const std::string& className,
                                  const std::string& id, const std::string& styleAttr, int viewportWidth,
                                  int viewportHeight) const;
  int getMarginTopPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                     const std::string& styleAttr, int viewportWidth, int viewportHeight) const;
  int getMarginBottomPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                        const std::string& styleAttr, int viewportWidth, int viewportHeight) const;
  int getPaddingTopPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                      const std::string& styleAttr, int viewportWidth, int viewportHeight) const;
  int getPaddingBottomPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                         const std::string& styleAttr, int viewportWidth, int viewportHeight) const;
  int getBorderTopPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                     const std::string& styleAttr, int viewportWidth, int viewportHeight) const;
  int getBorderBottomPx(const std::string& elementTagLower, const std::string& className, const std::string& id,
                        const std::string& styleAttr, int viewportWidth, int viewportHeight) const;
  bool hasParagraphSpacingSpecified(const std::string& elementTagLower, const std::string& className,
                                    const std::string& id, const std::string& styleAttr) const;
  bool hasBorderSpecified(const std::string& elementTagLower, const std::string& className, const std::string& id,
                          const std::string& styleAttr) const;
  std::string getBackgroundImagePath(const std::string& elementTagLower, const std::string& className,
                                     const std::string& id, const std::string& styleAttr,
                                     const std::string& currentFilePath) const;

  size_t getRuleCount() const { return rules.size(); }

 private:
  std::vector<CssRule> rules;
  /** Interned CSS file paths; CssRule::sourcePathIndex points here (used to resolve background-image URLs). */
  std::vector<std::string> sourcePaths_;
  uint16_t internSourcePath(const std::string& path);
  std::string bodyTextAlignRaw;

  /**
   * Per-element matched-rule cache. A single element triggers ~12 getCascadedPropertyValue() calls with the
   * same (tag, class, id); without this each would re-tokenize and re-scan every rule (N+1). The first call
   * builds the match set, the rest reuse it. Invalidated whenever the rule set changes.
   */
  struct MatchedRule {
    const CssRule* rule;
    uint8_t tier;  // 2 = id selector, 1 = class selector, 0 = type selector
  };
  mutable std::string mcTag_;
  mutable std::string mcClass_;
  mutable std::string mcId_;
  mutable std::vector<MatchedRule> mcMatched_;
  mutable bool mcValid_ = false;
  const std::vector<MatchedRule>& matchedRulesFor(const std::string& elementTagLower, const std::string& className,
                                                  const std::string& id) const;

  void noteBodyHtmlTextAlign(const std::string& selectorRaw, const std::map<std::string, std::string>& properties);
  std::string getCascadedPropertyValue(const std::string& propName, const std::string& className,
                                       const std::string& id, const std::string& styleAttr,
                                       const std::string& elementTagLower = "") const;
  bool getCascadedPropertyValueAndSource(const std::string& propName, const std::string& className,
                                         const std::string& id, const std::string& styleAttr,
                                         const std::string& elementTagLower, std::string* outValue,
                                         std::string* outSourcePath) const;
  int mapTextAlignToStyleIndex(const std::string& rawValue) const;

  void parsePropertiesForDimensions(const std::string& propertiesStr, std::map<std::string, std::string>& properties) const;
  enum class PercentRefersTo { Width, Height };
  int parseDimensionValue(const std::string& value, int viewportWidth, int viewportHeight,
                          PercentRefersTo percentAxis) const;
  void parseInlineStyle(const std::string& styleAttr, std::map<std::string, std::string>& out) const;
  int getInlineOrSheetLength(const std::string& propName, const std::string& className, const std::string& id,
                             const std::string& styleAttr, int viewportWidth, int viewportHeight) const;
  int getSpacingEdgePx(const std::string& propName, const std::string& shorthandName, const std::string& className,
                       const std::string& id, const std::string& styleAttr, int viewportWidth,
                       int viewportHeight) const;
  int getBorderEdgePx(const std::string& edgePropName, const std::string& className, const std::string& id,
                      const std::string& styleAttr, int viewportWidth, int viewportHeight) const;
  bool hasPropertySpecified(const std::string& propName, const std::string& className, const std::string& id,
                            const std::string& styleAttr, const std::string& elementTagLower = "") const;

  std::string trim(const std::string& str) const;
  std::string toLower(const std::string& str) const;
};
