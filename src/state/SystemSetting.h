#pragma once

#include <cstdint>
#include <iosfwd>

class SystemSetting {
private:
    SystemSetting() = default;
    static SystemSetting instance;

public:
    SystemSetting(const SystemSetting&) = delete;
    SystemSetting& operator=(const SystemSetting&) = delete;

    enum SLEEP_SCREEN_MODE { 
        DARK = 0, 
        LIGHT = 1, 
        CUSTOM = 2, 
        COVER = 3, 
        TRANSPARENT = 4, 
        BLANK = 5, 
        SLEEP_SCREEN_MODE_COUNT 
    };
    
    enum SLEEP_SCREEN_COVER_MODE { 
        FIT = 0, 
        CROP = 1, 
        SLEEP_SCREEN_COVER_MODE_COUNT 
    };
    
    enum SLEEP_SCREEN_COVER_FILTER {
        NO_FILTER = 0,
        BLACK_AND_WHITE = 1,
        INVERTED_BLACK_AND_WHITE = 2,
        SLEEP_SCREEN_COVER_FILTER_COUNT
    };

    enum DISABLE_NAVIGATION_MODE {
        NAV_NONE = 0,
        LEFT_RIGHT = 1,
        UP_DOWN = 2,
        DISABLE_NAVIGATION_MODE_COUNT
    };

    // Status bar item types for new configurable sections
    enum STATUS_BAR_ITEM {
        STATUS_ITEM_NONE = 0,
        STATUS_ITEM_PAGE_NUMBERS = 1,
        STATUS_ITEM_PERCENTAGE = 2,
        STATUS_ITEM_CHAPTER_TITLE = 3,
        STATUS_ITEM_BATTERY_ICON = 4,
        STATUS_ITEM_BATTERY_PERCENTAGE = 5,
        STATUS_ITEM_BATTERY_ICON_WITH_PERCENT = 6,
        STATUS_ITEM_PROGRESS_BAR = 7,
        STATUS_ITEM_PROGRESS_BAR_WITH_PERCENT = 8,
        STATUS_ITEM_PAGE_BARS = 9,
        STATUS_ITEM_BOOK_TITLE = 10,
        STATUS_ITEM_AUTHOR_NAME = 11,
        STATUS_BAR_ITEM_COUNT
    };

    // Legacy status bar mode (kept for backward compatibility)
    enum STATUS_BAR_MODE {
        NONE = 0,
        NO_PROGRESS = 1,
        FULL = 2,                    // Full w/%
        FULL_WITH_PROGRESS_BAR = 3,  // Full w/Bar
        ONLY_PROGRESS_BAR = 4,       // Bar Only
        BATTERY_PERCENTAGE = 5,      // Battery with percentage
        PERCENTAGE = 6,              // Just percentage
        PAGE_BARS = 7,               // Dynamic page bars
        STATUS_BAR_MODE_COUNT
    };

    enum ORIENTATION {
        PORTRAIT = 0,
        LANDSCAPE_CW = 1,
        INVERTED = 2,
        LANDSCAPE_CCW = 3,
        ORIENTATION_COUNT
    };

    enum FRONT_BUTTON_LAYOUT {
        BACK_CONFIRM_LEFT_RIGHT = 0,
        LEFT_RIGHT_BACK_CONFIRM = 1,
        LEFT_BACK_CONFIRM_RIGHT = 2,
        BACK_CONFIRM_RIGHT_LEFT = 3,
        FRONT_BUTTON_LAYOUT_COUNT
    };

    enum SIDE_BUTTON_LAYOUT { 
        PREV_NEXT = 0, 
        NEXT_PREV = 1, 
        SIDE_BUTTON_LAYOUT_COUNT 
    };

    enum READER_DIRECTION_MAPPING {
        MAP_LEFT_RIGHT = 0,
        MAP_RIGHT_LEFT = 1,
        MAP_UP_DOWN = 2,
        MAP_DOWN_UP = 3,
        MAP_NONE = 4,
        READER_DIRECTION_MAPPING_COUNT
    };

    enum READER_MENU_BUTTON {
        MENU_UP = 0,
        MENU_DOWN = 1,
        MENU_LEFT = 2,
        MENU_RIGHT = 3,
        READER_MENU_BUTTON_COUNT
    };
    
    enum FONT_FAMILY { 
        BOOKERLY = 0, 
        ATKINSON_HYPERLEGIBLE = 1, 
        LITERATA = 2, 
        FONT_FAMILY_COUNT 
    };
    
    enum FONT_SIZE { 
        EXTRA_SMALL = 0,
        SMALL = 1, 
        MEDIUM = 2, 
        LARGE = 3, 
        EXTRA_LARGE = 4, 
        FONT_SIZE_COUNT 
    };
    
    enum LINE_COMPRESSION { 
        TIGHT = 0, 
        NORMAL = 1, 
        WIDE = 2, 
        LINE_COMPRESSION_COUNT 
    };
    
    enum PARAGRAPH_ALIGNMENT {
        JUSTIFIED = 0,
        LEFT_ALIGN = 1,
        CENTER_ALIGN = 2,
        RIGHT_ALIGN = 3,
        PARAGRAPH_ALIGNMENT_COUNT
    };

    enum SLEEP_TIMEOUT {
        SLEEP_1_MIN = 0,
        SLEEP_5_MIN = 1,
        SLEEP_10_MIN = 2,
        SLEEP_15_MIN = 3,
        SLEEP_30_MIN = 4,
        SLEEP_TIMEOUT_COUNT
    };

    enum REFRESH_FREQUENCY {
        REFRESH_1 = 0,
        REFRESH_5 = 1,
        REFRESH_10 = 2,
        REFRESH_15 = 3,
        REFRESH_30 = 4,
        REFRESH_FREQUENCY_COUNT
    };

    // Global short power button behavior (for library, home, etc.)
    enum SHORT_PWRBTN { 
        IGNORE = 0, 
        SLEEP = 1, 
        PAGE_REFRESH = 2,
        SHORT_PWRBTN_COUNT 
    };
    
    // Reader-specific short power button behavior
    enum READER_SHORT_PWRBTN {
        READER_PAGE_TURN = 0,
        READER_PAGE_REFRESH = 1,
        READER_SHORT_PWRBTN_COUNT
    };
    
    enum HIDE_BATTERY_PERCENTAGE { 
        HIDE_NEVER = 0, 
        HIDE_READER = 1, 
        HIDE_ALWAYS = 2, 
        HIDE_BATTERY_PERCENTAGE_COUNT 
    };

    enum RECENT_LIBRARY_MODE {
        RECENT_GRID = 0,
        RECENT_LIST = 1,
        RECENT_LIBRARY_MODE_COUNT
    };

    enum BOOT_SETTING {
        RECENT_PAGE = 0,
        HOME_PAGE = 1,
        BOOT_SETTING_COUNT
    };

    // Sleep screen settings
    uint8_t sleepScreen = LIGHT;
    uint8_t sleepScreenCoverMode = FIT;
    uint8_t sleepScreenCoverFilter = NO_FILTER;
    
    // Legacy status bar mode (kept for backward compatibility)
    uint8_t statusBar = FULL;
    
    // New configurable status bar sections
    uint8_t statusBarLeft = STATUS_ITEM_BATTERY_ICON_WITH_PERCENT;
    uint8_t statusBarMiddle = STATUS_ITEM_CHAPTER_TITLE;
    uint8_t statusBarRight = STATUS_ITEM_PAGE_NUMBERS;
    
    // Text rendering settings
    uint8_t extraParagraphSpacing = 1;
    uint8_t textAntiAliasing = 1;
    
    // Global short power button click behaviour (outside reader)
    uint8_t shortPwrBtn = IGNORE;
    
    // Reader-specific short power button click behaviour
    uint8_t readerShortPwrBtn = READER_PAGE_TURN;
    
    // EPUB reading orientation settings
    uint8_t orientation = PORTRAIT;
    
    // Button layouts
    uint8_t frontButtonLayout = BACK_CONFIRM_LEFT_RIGHT;
    uint8_t sideButtonLayout = PREV_NEXT;
    
    // --- NEW MEMBER VARIABLES ---
    uint8_t readerDirectionMapping = MAP_NONE;
    uint8_t readerMenuButton = MENU_UP;
    
    // Reader font settings
    uint8_t fontFamily = LITERATA;
    uint8_t fontSize = SMALL;
    uint8_t lineSpacing = TIGHT;
    uint8_t paragraphAlignment = JUSTIFIED;
    
    // Auto-sleep timeout setting (default 10 minutes)
    uint8_t sleepTimeout = SLEEP_10_MIN;
    
    // E-ink refresh frequency (default 15 pages)
    uint8_t refreshFrequency = REFRESH_15;
    uint8_t hyphenationEnabled = 1;

    // Reader screen margin settings
    uint8_t screenMargin = 20;
    
    // OPDS browser settings
    char opdsServerUrl[128] = "";
    char opdsUsername[64] = "";
    char opdsPassword[64] = "";
    
    uint8_t hideBatteryPercentage = HIDE_NEVER;
    uint8_t longPressChapterSkip = 1;
    uint8_t useLibraryIndex = 0;
    uint8_t disableNavigation = NAV_NONE;
    
    // Library display settings
    uint8_t recentLibraryMode = RECENT_LIST;
    
    // Boot settings
    uint8_t bootSetting = RECENT_PAGE; 

    ~SystemSetting() = default;

    static SystemSetting& getInstance() { 
        return instance; 
    }

    uint16_t getPowerButtonDuration() const {
        return (shortPwrBtn == SystemSetting::SHORT_PWRBTN::SLEEP) ? 10 : 400;
    }
    
    int getReaderFontId() const;
    bool saveToFile() const;
    bool loadFromFile();
    float getReaderLineCompression() const;
    unsigned long getSleepTimeoutMs() const;
    int getRefreshFrequency() const;
};

#define SETTINGS SystemSetting::getInstance()