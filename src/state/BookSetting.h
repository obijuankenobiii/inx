#pragma once

#include <cstdint>
#include <string>
#include <SDCardManager.h>
#include "state/SystemSetting.h"

// Status bar related enums
enum class StatusBarItem {
    NONE,
    PAGE_NUMBERS,
    PERCENTAGE,
    CHAPTER_TITLE,
    BATTERY_ICON,
    BATTERY_PERCENTAGE,
    BATTERY_ICON_WITH_PERCENT,
    PROGRESS_BAR,
    PROGRESS_BAR_WITH_PERCENT,
    PAGE_BARS,
    BOOK_TITLE,
    AUTHOR_NAME,
    STATUS_BAR_ITEM_COUNT
};

struct StatusBarSectionConfig {
    StatusBarItem item = StatusBarItem::NONE;
    
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

struct BookSettings {
    // Font settings - store as string to support any font
    std::string fontFamily = "Atkinson Hyperlegible";
    int fontSize = 12;  // Actual point size (8, 10, 12, 14, 16, 18)
    
    uint8_t lineSpacing = SystemSetting::NORMAL;
    uint8_t paragraphAlignment = SystemSetting::JUSTIFIED;
    uint8_t extraParagraphSpacing = 1;
    uint8_t textAntiAliasing = 1;
    uint8_t hyphenationEnabled = 1;
    uint8_t screenMargin = 20;
    uint8_t orientation = SystemSetting::PORTRAIT;
    uint8_t longPressChapterSkip = 1;
    uint8_t refreshFrequency = 15;
    
    StatusBarSectionConfig statusBarLeft;
    StatusBarSectionConfig statusBarMiddle;
    StatusBarSectionConfig statusBarRight;
    
    bool useCustomSettings = false;
    
    bool loadFromFile(const std::string& bookCachePath) {
        std::string settingsPath = bookCachePath + "/settings.bin";
        FsFile f;
        if (SdMan.openFileForRead("BST", settingsPath.c_str(), f)) {
            uint8_t data[256];
            size_t bytesRead = f.read(data, sizeof(data));
            
            if (bytesRead > 0) {
                size_t offset = 0;
                
                // Read font family string
                uint8_t familyLen = data[offset++];
                if (familyLen > 0 && offset + familyLen <= bytesRead) {
                    fontFamily.assign((char*)&data[offset], familyLen);
                    offset += familyLen;
                }
                
                // Read font size
                if (offset < bytesRead) {
                    fontSize = data[offset++];
                }
                
                // Read other settings
                if (offset < bytesRead) lineSpacing = data[offset++];
                if (offset < bytesRead) extraParagraphSpacing = data[offset++];
                if (offset < bytesRead) paragraphAlignment = data[offset++];
                if (offset < bytesRead) hyphenationEnabled = data[offset++];
                if (offset < bytesRead) screenMargin = data[offset++];
                if (offset < bytesRead) refreshFrequency = data[offset++];
                if (offset < bytesRead) longPressChapterSkip = data[offset++];
                if (offset < bytesRead) textAntiAliasing = data[offset++];
                if (offset < bytesRead) orientation = data[offset++];
                
                // Read status bar config
                if (bytesRead >= offset + 3) {
                    statusBarLeft.fromBytes(data, offset);
                    statusBarMiddle.fromBytes(data, offset);
                    statusBarRight.fromBytes(data, offset);
                }
                
                useCustomSettings = true;
                f.close();
                return true;
            }
            f.close();
        }
        
        loadFromGlobalSettings();
        useCustomSettings = false;
        return false;
    }
    
    bool saveToFile(const std::string& bookCachePath) {
        std::string settingsPath = bookCachePath + "/settings.bin";
        FsFile f;
        if (SdMan.openFileForWrite("BST", settingsPath.c_str(), f)) {
            uint8_t data[256];
            size_t offset = 0;
            
            // Write font family
            uint8_t familyLen = std::min((uint8_t)fontFamily.length(), (uint8_t)63);
            data[offset++] = familyLen;
            memcpy(&data[offset], fontFamily.c_str(), familyLen);
            offset += familyLen;
            
            // Write font size
            data[offset++] = fontSize;
            
            // Write other settings
            data[offset++] = lineSpacing;
            data[offset++] = extraParagraphSpacing;
            data[offset++] = paragraphAlignment;
            data[offset++] = hyphenationEnabled;
            data[offset++] = screenMargin;
            data[offset++] = refreshFrequency;
            data[offset++] = longPressChapterSkip;
            data[offset++] = textAntiAliasing;
            data[offset++] = orientation;
            
            // Write status bar config
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

        fontFamily = global.fontFamily;
        fontSize = global.fontSize;
        lineSpacing = global.lineSpacing;
        extraParagraphSpacing = global.extraParagraphSpacing;
        paragraphAlignment = global.paragraphAlignment;
        hyphenationEnabled = global.hyphenationEnabled;
        screenMargin = global.screenMargin;
        
        switch (global.refreshFrequency) {
            case SystemSetting::REFRESH_1:  refreshFrequency = 1;  break;
            case SystemSetting::REFRESH_5:  refreshFrequency = 5;  break;
            case SystemSetting::REFRESH_10: refreshFrequency = 10; break;
            case SystemSetting::REFRESH_15: refreshFrequency = 15; break;
            case SystemSetting::REFRESH_30: refreshFrequency = 30; break;
            default:                        refreshFrequency = 15; break;
        }

        longPressChapterSkip = global.longPressChapterSkip;
        textAntiAliasing = global.textAntiAliasing;
        orientation = global.orientation;
        
        statusBarLeft.item = static_cast<StatusBarItem>(global.statusBarLeft);
        statusBarMiddle.item = static_cast<StatusBarItem>(global.statusBarMiddle);
        statusBarRight.item = static_cast<StatusBarItem>(global.statusBarRight);
    }
    
    // Helper to get font ID from FontManager
    int getReaderFontId() const;
    
    float getReaderLineCompression() const {
        SystemSetting& global = SystemSetting::getInstance();
        std::string oldFam = global.fontFamily;
        uint8_t oldSpacing = global.lineSpacing;
        const_cast<SystemSetting&>(global).fontFamily = "Atkinson Hyperlegible";
        const_cast<SystemSetting&>(global).lineSpacing = this->lineSpacing;
        float comp = global.getReaderLineCompression();
        const_cast<SystemSetting&>(global).fontFamily = oldFam;
        const_cast<SystemSetting&>(global).lineSpacing = oldSpacing;
        return comp;
    }
    
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