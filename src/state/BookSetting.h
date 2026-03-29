#pragma once

#include <cstdint>
#include <string>
#include <SDCardManager.h>
#include "state/SystemSetting.h"

// Status bar related enums
enum class StatusBarItem {
    NONE,                       // Nothing displayed
    PAGE_NUMBERS,               // Current page / total pages (e.g., "5/120")
    PERCENTAGE,                 // Reading percentage (e.g., "42%")
    CHAPTER_TITLE,              // Current chapter title
    BATTERY_ICON,               // Battery icon only
    BATTERY_PERCENTAGE,         // Battery percentage text only
    BATTERY_ICON_WITH_PERCENT,  // Battery icon with percentage
    PROGRESS_BAR,               // Horizontal progress bar
    PROGRESS_BAR_WITH_PERCENT,  // Progress bar with percentage
    PAGE_BARS,                  // Vertical bars representing pages
    BOOK_TITLE,                 // Book title
    AUTHOR_NAME,                // Author name
    STATUS_BAR_ITEM_COUNT
};

// Simple config with just the item type
struct StatusBarSectionConfig {
    StatusBarItem item = StatusBarItem::NONE;
    
    // Simplified serialization - just the item
    void toBytes(uint8_t* data, size_t& offset) const {
        data[offset++] = static_cast<uint8_t>(item);
    }
    
    void fromBytes(const uint8_t* data, size_t& offset) {
        item = static_cast<StatusBarItem>(data[offset++]);
    }
    
    bool operator==(const StatusBarSectionConfig& other) const {
        return item == other.item;
    }
    
    bool operator!=(const StatusBarSectionConfig& other) const {
        return !(*this == other);
    }
};

struct StatusBarLayout {
    StatusBarSectionConfig left;
    StatusBarSectionConfig middle;
    StatusBarSectionConfig right;
};

struct BookSettings {
    uint8_t fontFamily = SystemSetting::LITERATA;
    uint8_t fontSize = SystemSetting::SMALL;
    uint8_t lineSpacing = SystemSetting::NORMAL;
    uint8_t paragraphAlignment = SystemSetting::JUSTIFIED;
    
    // Text rendering settings
    uint8_t extraParagraphSpacing = 1;
    uint8_t textAntiAliasing = 1;
    uint8_t hyphenationEnabled = 1;

    // Reader screen margin settings
    uint8_t screenMargin = 20;
    
    // Reading orientation settings
    uint8_t orientation = SystemSetting::PORTRAIT;
    
    // Navigation settings
    uint8_t longPressChapterSkip = 1;
    
    // Display settings 
    // This now stores the LITERAL value (1, 5, 10, 15, or 30)
    uint8_t refreshFrequency = 15; 
    
    // Configurable status bar sections
    StatusBarSectionConfig statusBarLeft;
    StatusBarSectionConfig statusBarMiddle;
    StatusBarSectionConfig statusBarRight;
    
    bool useCustomSettings = false;
    
    // Helper to get complete layout
    struct Layout {
        StatusBarSectionConfig left;
        StatusBarSectionConfig middle;
        StatusBarSectionConfig right;
    };
    
    Layout getStatusBarLayout() const {
        return {statusBarLeft, statusBarMiddle, statusBarRight};
    }
    
    void setStatusBarLayout(const Layout& layout) {
        statusBarLeft = layout.left;
        statusBarMiddle = layout.middle;
        statusBarRight = layout.right;
    }
    
    bool loadFromFile(const std::string& bookCachePath) {
        std::string settingsPath = bookCachePath + "/settings.bin";
        FsFile f;
        if (SdMan.openFileForRead("BST", settingsPath.c_str(), f)) {
            size_t fileSize = f.size();
            
            if (fileSize >= 11) {  // 11 legacy fields (statusBarMode removed)
                uint8_t data[64];
                size_t bytesRead = f.read(data, std::min(fileSize, sizeof(data)));
                
                if (bytesRead >= 11) {
                    size_t offset = 0;
                    
                    // Read legacy fields (11 bytes - statusBarMode removed)
                    fontFamily              = data[offset++];
                    fontSize                = data[offset++];
                    lineSpacing             = data[offset++];
                    extraParagraphSpacing   = data[offset++];
                    paragraphAlignment      = data[offset++];
                    hyphenationEnabled      = data[offset++];
                    screenMargin            = data[offset++];
                    refreshFrequency        = data[offset++];
                    longPressChapterSkip    = data[offset++];
                    textAntiAliasing        = data[offset++];
                    orientation             = data[offset++];
                    
                    // Check if file contains status bar config (3 bytes for 3 sections)
                    if (bytesRead >= offset + 3) {
                        statusBarLeft.fromBytes(data, offset);
                        statusBarMiddle.fromBytes(data, offset);
                        statusBarRight.fromBytes(data, offset);
                    } else {
                        // No config found, set defaults from global
                        loadFromGlobalSettings();
                    }
                    
                    useCustomSettings = true;
                    f.close();
                    return true;
                }
            }
            f.close();
        }
        
        // If file doesn't exist or is invalid, load from global settings
        loadFromGlobalSettings();
        useCustomSettings = false;
        return false;
    }
    
    bool saveToFile(const std::string& bookCachePath) {
        std::string settingsPath = bookCachePath + "/settings.bin";
        FsFile f;
        if (SdMan.openFileForWrite("BST", settingsPath.c_str(), f)) {
            uint8_t data[32];
            size_t offset = 0;
            
            // Write fields (11 bytes - statusBarMode removed)
            data[offset++] = fontFamily;
            data[offset++] = fontSize;
            data[offset++] = lineSpacing;
            data[offset++] = extraParagraphSpacing;
            data[offset++] = paragraphAlignment;
            data[offset++] = hyphenationEnabled;
            data[offset++] = screenMargin;
            data[offset++] = refreshFrequency;
            data[offset++] = longPressChapterSkip;
            data[offset++] = textAntiAliasing;
            data[offset++] = orientation;
            
            // Write status bar config (3 bytes - one per section)
            statusBarLeft.toBytes(data, offset);
            statusBarMiddle.toBytes(data, offset);
            statusBarRight.toBytes(data, offset);
            
            bool success = (f.write(data, offset) == offset);
            f.close();
            
            if (success) {
                useCustomSettings = true;
            }
            return success;
        }
        return false;
    }

    void loadFromGlobalSettings() {
        SystemSetting& global = SystemSetting::getInstance();
        fontFamily             = global.fontFamily;
        fontSize               = global.fontSize;
        lineSpacing            = global.lineSpacing;
        extraParagraphSpacing  = global.extraParagraphSpacing;
        paragraphAlignment     = global.paragraphAlignment;
        hyphenationEnabled     = global.hyphenationEnabled;
        screenMargin           = global.screenMargin;
        
        // Convert Global Enum Index to Actual Value for local storage
        switch (global.refreshFrequency) {
            case SystemSetting::REFRESH_1:  refreshFrequency = 1;  break;
            case SystemSetting::REFRESH_5:  refreshFrequency = 5;  break;
            case SystemSetting::REFRESH_10: refreshFrequency = 10; break;
            case SystemSetting::REFRESH_15: refreshFrequency = 15; break;
            case SystemSetting::REFRESH_30: refreshFrequency = 30; break;
            default:                        refreshFrequency = 15; break;
        }

        longPressChapterSkip   = global.longPressChapterSkip;
        textAntiAliasing       = global.textAntiAliasing;
        orientation            = global.orientation;
        
        // Load status bar sections from global settings
        statusBarLeft.item = static_cast<StatusBarItem>(global.statusBarLeft);
        statusBarMiddle.item = static_cast<StatusBarItem>(global.statusBarMiddle);
        statusBarRight.item = static_cast<StatusBarItem>(global.statusBarRight);
    }
    
    int getReaderFontId() const {
        SystemSetting& global = SystemSetting::getInstance();
        uint8_t oldFam = global.fontFamily;
        uint8_t oldSize = global.fontSize;
        global.fontFamily = this->fontFamily;
        global.fontSize = this->fontSize;
        int id = global.getReaderFontId();
        global.fontFamily = oldFam;
        global.fontSize = oldSize;
        return id;
    }

    float getReaderLineCompression() const {
        SystemSetting& global = SystemSetting::getInstance();
        uint8_t oldFam = global.fontFamily;
        uint8_t oldSpacing = global.lineSpacing;
        global.fontFamily = this->fontFamily;
        global.lineSpacing = this->lineSpacing;
        float comp = global.getReaderLineCompression();
        global.fontFamily = oldFam;
        global.lineSpacing = oldSpacing;
        return comp;
    }
    
    // Equality operator for comparison
    bool operator==(const BookSettings& other) const {
        return fontFamily == other.fontFamily &&
               fontSize == other.fontSize &&
               lineSpacing == other.lineSpacing &&
               paragraphAlignment == other.paragraphAlignment &&
               extraParagraphSpacing == other.extraParagraphSpacing &&
               textAntiAliasing == other.textAntiAliasing &&
               hyphenationEnabled == other.hyphenationEnabled &&
               screenMargin == other.screenMargin &&
               orientation == other.orientation &&
               longPressChapterSkip == other.longPressChapterSkip &&
               refreshFrequency == other.refreshFrequency &&
               statusBarLeft == other.statusBarLeft &&
               statusBarMiddle == other.statusBarMiddle &&
               statusBarRight == other.statusBarRight;
    }
    
    bool operator!=(const BookSettings& other) const {
        return !(*this == other);
    }
};