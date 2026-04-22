#pragma once

#include <cstdint>
#include <iosfwd>

/**
 * @brief System settings management class
 */
class SystemSetting {
private:
    SystemSetting() = default;
    static SystemSetting instance;

public:
    SystemSetting(const SystemSetting&) = delete;
    SystemSetting& operator=(const SystemSetting&) = delete;

    /**
     * @brief Sleep screen display modes
     */
    enum SLEEP_SCREEN_MODE { 
        DARK = 0,           ///< Dark screen
        LIGHT = 1,          ///< Light screen
        CUSTOM = 2,         ///< Custom image
        COVER = 3,          ///< Book cover
        TRANSPARENT = 4,    ///< Transparent
        BLANK = 5,          ///< Blank screen
        SLEEP_SCREEN_MODE_COUNT 
    };
    
    /**
     * @brief Sleep screen cover scaling modes
     */
    enum SLEEP_SCREEN_COVER_MODE { 
        FIT = 0,            ///< Fill screen; preserve aspect (center crop / cover)
        CROP = 1,           ///< Show full image in screen (letterbox / contain, no upscale)
        SLEEP_SCREEN_COVER_MODE_COUNT 
    };
    
    /**
     * @brief Sleep screen cover filter options
     */
    enum SLEEP_SCREEN_COVER_FILTER {
        NO_FILTER = 0,                      ///< No filter
        BLACK_AND_WHITE = 1,                ///< Black and white
        INVERTED_BLACK_AND_WHITE = 2,       ///< Inverted black and white
        SLEEP_SCREEN_COVER_FILTER_COUNT
    };

    /**
     * @brief Navigation disable modes
     */
    enum DISABLE_NAVIGATION_MODE {
        NAV_NONE = 0,       ///< Navigation enabled
        LEFT_RIGHT = 1,     ///< Disable left/right
        UP_DOWN = 2,        ///< Disable up/down
        DISABLE_NAVIGATION_MODE_COUNT
    };

    /**
     * @brief Status bar item types for configurable sections
     */
    enum STATUS_BAR_ITEM {
        STATUS_ITEM_NONE = 0,                       ///< No item
        STATUS_ITEM_PAGE_NUMBERS = 1,               ///< Page numbers
        STATUS_ITEM_PERCENTAGE = 2,                 ///< Reading percentage
        STATUS_ITEM_CHAPTER_TITLE = 3,              ///< Chapter title
        STATUS_ITEM_BATTERY_ICON = 4,               ///< Battery icon only
        STATUS_ITEM_BATTERY_PERCENTAGE = 5,         ///< Battery percentage text
        STATUS_ITEM_BATTERY_ICON_WITH_PERCENT = 6,  ///< Battery icon with percentage
        STATUS_ITEM_PROGRESS_BAR = 7,               ///< Progress bar only
        STATUS_ITEM_PROGRESS_BAR_WITH_PERCENT = 8,  ///< Progress bar with percentage
        STATUS_ITEM_PAGE_BARS = 9,                  ///< Page bars
        STATUS_ITEM_BOOK_TITLE = 10,                ///< Book title
        STATUS_ITEM_AUTHOR_NAME = 11,               ///< Author name
        STATUS_BAR_ITEM_COUNT
    };

    /**
     * @brief Legacy status bar mode (kept for backward compatibility)
     */
    enum STATUS_BAR_MODE {
        NONE = 0,                       ///< No status bar
        NO_PROGRESS = 1,                ///< Status bar without progress
        FULL = 2,                       ///< Full status bar with percentage
        FULL_WITH_PROGRESS_BAR = 3,     ///< Full status bar with progress bar
        ONLY_PROGRESS_BAR = 4,          ///< Progress bar only
        BATTERY_PERCENTAGE = 5,         ///< Battery with percentage
        PERCENTAGE = 6,                 ///< Just percentage
        PAGE_BARS = 7,                  ///< Dynamic page bars
        STATUS_BAR_MODE_COUNT
    };

    /**
     * @brief Screen orientation modes
     */
    enum ORIENTATION {
        PORTRAIT = 0,       ///< Portrait orientation
        LANDSCAPE_CW = 1,   ///< Landscape clockwise
        INVERTED = 2,       ///< Inverted portrait
        LANDSCAPE_CCW = 3,  ///< Landscape counter-clockwise
        ORIENTATION_COUNT
    };

    /**
     * @brief Front button layout configurations
     */
    enum FRONT_BUTTON_LAYOUT {
        BACK_CONFIRM_LEFT_RIGHT = 0,    ///< Back/Confirm on left, Prev/Next on right
        LEFT_RIGHT_BACK_CONFIRM = 1,    ///< Prev/Next on left, Back/Confirm on right
        LEFT_BACK_CONFIRM_RIGHT = 2,    ///< Prev on left, Back/Confirm in middle, Next on right
        BACK_CONFIRM_RIGHT_LEFT = 3,    ///< Back/Confirm on right, Prev/Next on left
        FRONT_BUTTON_LAYOUT_COUNT
    };

    /**
     * @brief Side button layout configurations
     */
    enum SIDE_BUTTON_LAYOUT { 
        PREV_NEXT = 0,      ///< Previous on top, Next on bottom
        NEXT_PREV = 1,      ///< Next on top, Previous on bottom
        SIDE_BUTTON_LAYOUT_COUNT 
    };

    /**
     * @brief Reader direction mapping for navigation
     */
    enum READER_DIRECTION_MAPPING {
        MAP_LEFT_RIGHT = 0,     ///< Map left/right to prev/next
        MAP_RIGHT_LEFT = 1,     ///< Map right/left to prev/next
        MAP_UP_DOWN = 2,        ///< Map up/down to prev/next
        MAP_DOWN_UP = 3,        ///< Map down/up to prev/next
        MAP_NONE = 4,           ///< No mapping
        READER_DIRECTION_MAPPING_COUNT
    };

    /**
     * @brief Reader menu button assignment
     */
    enum READER_MENU_BUTTON {
        MENU_UP = 0,        ///< Menu on up button
        MENU_DOWN = 1,      ///< Menu on down button
        MENU_LEFT = 2,      ///< Menu on left button
        MENU_RIGHT = 3,     ///< Menu on right button
        READER_MENU_BUTTON_COUNT
    };
    
    /**
     * @brief Font family options
     */
    enum FONT_FAMILY { 
        BOOKERLY = 0,               ///< Bookerly font
        ATKINSON_HYPERLEGIBLE = 1,  ///< Atkinson Hyperlegible font
        LITERATA = 2,               ///< Literata font
        FONT_FAMILY_COUNT 
    };
    
    /**
     * @brief Font size options
     */
    enum FONT_SIZE { 
        EXTRA_SMALL = 0,    ///< Extra small font
        SMALL = 1,          ///< Small font
        MEDIUM = 2,         ///< Medium font
        LARGE = 3,          ///< Large font
        EXTRA_LARGE = 4,    ///< Extra large font
        FONT_SIZE_COUNT 
    };
    
    /**
     * @brief Line spacing compression options
     */
    enum LINE_COMPRESSION { 
        TIGHT = 0,      ///< Tight line spacing
        NORMAL = 1,     ///< Normal line spacing
        WIDE = 2,       ///< Wide line spacing
        LINE_COMPRESSION_COUNT 
    };
    
    /**
     * @brief Paragraph alignment options
     */
    enum PARAGRAPH_ALIGNMENT {
        JUSTIFIED = 0,      ///< Justified alignment
        LEFT_ALIGN = 1,     ///< Left alignment
        CENTER_ALIGN = 2,   ///< Center alignment
        RIGHT_ALIGN = 3,    ///< Right alignment
        /** Same role as CrossPoint “Book style”: use EPUB `text-align` / cascade for each block (value must match
         *  EPUB_PARAGRAPH_ALIGNMENT_FOLLOW_CSS in lib/Epub/Epub/parsers/ChapterHtmlSlimParser.h). */
        FOLLOW_CSS = 4,
        PARAGRAPH_ALIGNMENT_COUNT
    };

    /**
     * @brief Sleep timeout duration options
     */
    enum SLEEP_TIMEOUT {
        SLEEP_1_MIN = 0,    ///< 1 minute timeout
        SLEEP_5_MIN = 1,    ///< 5 minute timeout
        SLEEP_10_MIN = 2,   ///< 10 minute timeout
        SLEEP_15_MIN = 3,   ///< 15 minute timeout
        SLEEP_30_MIN = 4,   ///< 30 minute timeout
        SLEEP_TIMEOUT_COUNT
    };

    /**
     * @brief Screen refresh frequency options
     */
    enum REFRESH_FREQUENCY {
        REFRESH_1 = 0,      ///< Refresh every page
        REFRESH_5 = 1,      ///< Refresh every 5 pages
        REFRESH_10 = 2,     ///< Refresh every 10 pages
        REFRESH_15 = 3,     ///< Refresh every 15 pages
        REFRESH_30 = 4,     ///< Refresh every 30 pages
        REFRESH_FREQUENCY_COUNT
    };

    /**
     * @brief Global short power button behavior (for library, home, etc.)
     */
    enum SHORT_PWRBTN { 
        IGNORE = 0,         ///< Ignore short press
        SLEEP = 1,          ///< Put to sleep
        PAGE_REFRESH = 2,   ///< Refresh page
        SHORT_PWRBTN_COUNT 
    };
    
    /**
     * @brief Reader-specific short power button behavior
     */
    enum READER_SHORT_PWRBTN {
        READER_PAGE_TURN = 0,       ///< Turn page
        READER_PAGE_REFRESH = 1,    ///< Refresh screen
        READER_SHORT_PWRBTN_COUNT
    };
    
    /**
     * @brief Battery percentage display options
     */
    enum HIDE_BATTERY_PERCENTAGE { 
        HIDE_NEVER = 0,     ///< Always show
        HIDE_READER = 1,    ///< Hide in reader
        HIDE_ALWAYS = 2,    ///< Always hide
        HIDE_BATTERY_PERCENTAGE_COUNT 
    };

    /**
     * @brief Recent library display modes
     */
    enum RECENT_LIBRARY_MODE {
        RECENT_GRID = 0,    ///< Grid view
        RECENT_LIST = 1,    ///< List view
        RECENT_FLOW = 2,    ///< List view
        RECENT_LIBRARY_MODE_COUNT
    };

    /**
     * @brief Boot destination settings
     */
    enum BOOT_SETTING {
        RECENT_PAGE = 0,    ///< Boot to recent page
        HOME_PAGE = 1,      ///< Boot to home page
        BOOT_SETTING_COUNT
    };

    /**
     * @brief Bitmap contrast / ink weight for reader and system images (2bpp → BW when scaled).
     */
    enum READER_IMAGE_PRESENTATION {
        IMAGE_PRESENTATION_LOW = 0,     ///< Lightest: legacy Balanced snap (less mid-gray ink)
        IMAGE_PRESENTATION_MEDIUM = 1, ///< Default: full-gray halftone (former “Balance”)
        IMAGE_PRESENTATION_HIGH = 2,   ///< Strongest: tighter snap + more ink (former “Dark”)
        READER_IMAGE_PRESENTATION_COUNT
    };

    /**
     * @brief Error diffusion when decoding high-color BMP to 2bpp (matches `BitmapDitherMode`).
     */
    enum READER_IMAGE_DITHER {
        IMAGE_DITHER_NONE = 0,
        IMAGE_DITHER_FLOYD_STEINBERG = 1,
        IMAGE_DITHER_ATKINSON = 2,
        READER_IMAGE_DITHER_COUNT
    };

    // Sleep screen settings
    uint8_t sleepScreen = LIGHT;                                ///< Sleep screen display mode
    uint8_t sleepScreenCoverMode = FIT;                         ///< Sleep screen cover scaling mode
    uint8_t sleepScreenCoverFilter = NO_FILTER;                 ///< Sleep screen cover filter
    /** When set (and filter is None), 2bpp cover images get the e-ink grayscale pass on sleep. */
    uint8_t sleepScreenCoverGrayscale = 0;
    /**
     * Fixed custom/transparent sleep BMP when multiple images exist.
     * Empty = pick a random file from /sleep/ (and /sleep.bmp) each time.
     * Basename only = use /sleep/<basename> (e.g. night.bmp).
     * Exactly "/sleep.bmp" = use the BMP at the SD card root only.
     */
    char sleepCustomBmp[64] = "";

    // Legacy status bar mode (kept for backward compatibility)
    uint8_t statusBar = FULL;                                   ///< Legacy status bar mode
    
    // New configurable status bar sections
    uint8_t statusBarLeft = STATUS_ITEM_BATTERY_ICON_WITH_PERCENT;   ///< Left status bar section
    uint8_t statusBarMiddle = STATUS_ITEM_CHAPTER_TITLE;             ///< Middle status bar section
    uint8_t statusBarRight = STATUS_ITEM_PAGE_NUMBERS;               ///< Right status bar section
    
    // Text rendering settings
    uint8_t extraParagraphSpacing = 1;                          ///< Extra paragraph spacing enabled
    uint8_t textAntiAliasing = 1;                               ///< Text anti-aliasing enabled
    
    // Global short power button click behaviour (outside reader)
    uint8_t shortPwrBtn = IGNORE;                               ///< Short power button behavior
    
    // Reader-specific short power button click behaviour
    uint8_t readerShortPwrBtn = READER_PAGE_TURN;               ///< Reader short power button behavior
    
    // EPUB reading orientation settings
    uint8_t orientation = PORTRAIT;                             ///< Screen orientation
    
    // Button layouts
    uint8_t frontButtonLayout = BACK_CONFIRM_LEFT_RIGHT;        ///< Front button layout
    uint8_t sideButtonLayout = PREV_NEXT;                       ///< Side button layout
    
    // Reader navigation settings
    uint8_t readerDirectionMapping = MAP_NONE;                  ///< Reader direction mapping
    uint8_t readerMenuButton = MENU_UP;                         ///< Reader menu button assignment
    
    // Reader font settings
    uint8_t fontFamily = LITERATA;                              ///< Font family
    uint8_t fontSize = SMALL;                                   ///< Font size
    uint8_t lineSpacing = TIGHT;                                ///< Line spacing
    uint8_t paragraphAlignment = JUSTIFIED;                     ///< Paragraph alignment
    /** When set, EPUB/CSS `text-indent` (inline or stylesheet) is honored even if paragraph alignment is not Use EPUB CSS. */
    uint8_t paragraphCssIndentEnabled = 0;

    // Auto-sleep timeout setting (default 10 minutes)
    uint8_t sleepTimeout = SLEEP_10_MIN;                        ///< Sleep timeout
    
    // E-ink refresh frequency (default 15 pages)
    uint8_t refreshFrequency = REFRESH_15;                      ///< Refresh frequency
    uint8_t hyphenationEnabled = 1;                             ///< Hyphenation enabled

    // Reader screen margin settings
    uint8_t screenMargin = 20;                                  ///< Screen margin in pixels
    
    // OPDS browser settings
    char opdsServerUrl[128] = "";                               ///< OPDS server URL
    char opdsUsername[64] = "";                                 ///< OPDS username
    char opdsPassword[64] = "";                                 ///< OPDS password
    
    uint8_t hideBatteryPercentage = HIDE_NEVER;                 ///< Hide battery percentage setting
    uint8_t longPressChapterSkip = 1;                           ///< Long press chapter skip enabled
    uint8_t useLibraryIndex = 0;                                ///< Use library index enabled
    uint8_t disableNavigation = NAV_NONE;                       ///< Navigation disable mode
    
    // Library display settings
    uint8_t recentLibraryMode = RECENT_FLOW;                  ///< Recent library display mode
    
    // Boot settings
    uint8_t bootSetting = RECENT_PAGE;                          ///< Boot destination setting

    /**
     * @brief Page auto-turn interval in seconds
     * @details Values: 0 = off, increments of 10 (10, 20, 30, 40, 50, 60)
     */
    uint8_t pageAutoTurnSeconds = 0;

    /** When set, EPUB pages with bitmap images run the extra grayscale pass after BW render. */
    uint8_t readerImageGrayscale = 0;
    /** When set, image-heavy EPUB pages use a gentler (half) refresh before/after transitions. */
    uint8_t readerSmartRefreshOnImages = 1;
    /** Bitmap gray mapping for book images in the reader (see READER_IMAGE_PRESENTATION). */
    uint8_t readerImagePresentation = IMAGE_PRESENTATION_MEDIUM;
    /** High-color BMP → 2bpp dither for EPUB reader pages and reader cover (see READER_IMAGE_DITHER). */
    uint8_t readerImageDither = IMAGE_DITHER_ATKINSON;
    /** Same enum as reader: sleep screen BMPs, recent/library covers, stats thumbnails. */
    uint8_t displayImageDither = IMAGE_DITHER_ATKINSON;
    /** Bitmap gray mapping for sleep/library/stats images (see READER_IMAGE_PRESENTATION). */
    uint8_t displayImagePresentation = IMAGE_PRESENTATION_MEDIUM;

    ~SystemSetting() = default;

    /**
     * @brief Gets the singleton instance
     * @return Reference to the SystemSetting instance
     */
    static SystemSetting& getInstance() { 
        return instance; 
    }

    /**
     * @brief Gets power button duration in milliseconds
     * @return Duration in ms (10ms for sleep, 400ms for ignore)
     */
    uint16_t getPowerButtonDuration() const {
        return (shortPwrBtn == SystemSetting::SHORT_PWRBTN::SLEEP) ? 10 : 400;
    }
    
    /**
     * @brief Gets reader font ID based on font family and size
     * @return Font identifier for rendering
     */
    int getReaderFontId() const;
    
    /**
     * @brief Validates and stores the fixed custom sleep BMP choice (basename under /sleep/ or "/sleep.bmp").
     * @param s nullptr or empty string clears (random selection each sleep).
     */
    void setSleepCustomBmpFromInput(const char* s);

    /**
     * @brief Saves all settings to file
     * @return true if save successful, false otherwise
     */
    bool saveToFile() const;
    
    /**
     * @brief Loads all settings from file
     * @return true if load successful, false otherwise
     */
    bool loadFromFile();
    
    /**
     * @brief Gets reader line compression factor based on font and spacing
     * @return Line compression multiplier
     */
    float getReaderLineCompression() const;
    
    /**
     * @brief Gets sleep timeout in milliseconds
     * @return Sleep timeout in milliseconds
     */
    unsigned long getSleepTimeoutMs() const;
    
    /**
     * @brief Gets screen refresh frequency in pages
     * @return Number of pages between refreshes
     */
    int getRefreshFrequency() const;
};

#define SETTINGS SystemSetting::getInstance()