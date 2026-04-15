#pragma once

#include <map>
#include <string>
#include <vector>

class CssParser {
public:
    struct CssRule {
        std::string selector;
        std::map<std::string, std::string> properties;
    };
    
    CssParser();
    ~CssParser();
    
    void parse(const std::string& cssContent);
    void parseFile(const std::string& filepath);
    void clear();
    
    int getWidth(const std::string& className, const std::string& id, 
                      const std::string& styleAttr, int viewportWidth) const;
    int getHeight(const std::string& className, const std::string& id,
                       const std::string& styleAttr, int viewportHeight) const;
    
    size_t getRuleCount() const { return rules.size(); }
    
private:
    std::vector<CssRule> rules;
    
    void parsePropertiesForDimensions(const std::string& propertiesStr, 
                                      std::map<std::string, std::string>& properties) const;
    int parseDimensionValue(const std::string& value, int viewportWidth, int viewportHeight) const;
    int extractWidthFromInlineStyle(const std::string& styleAttr) const;
    int extractHeightFromInlineStyle(const std::string& styleAttr) const;
    std::string trim(const std::string& str) const;
    std::string toLower(const std::string& str) const;
    
    // Not implemented but kept for compatibility
    std::map<std::string, std::string> getCombinedPropertiesForElement(
        const std::string& elementName,
        const std::string& className,
        const std::string& id) const;
};