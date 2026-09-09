#pragma once

/**
 * @file UiLayout.h
 * @brief Single source of truth for shared UI geometry.
 *
 * Keep layout values here so page chrome can be tuned without hunting through
 * individual renderers. This header intentionally contains only constants and
 * has no renderer dependencies.
 */

namespace UiLayout {

// Shared page/list geometry.
constexpr int LIST_ITEM_HEIGHT = 66;
constexpr int HEADER_HEIGHT = 75;
constexpr int PAGE_HEADER_HEIGHT = 79;
constexpr int TAB_BAR_HEIGHT = 60;
constexpr int CONTENT_TOP = 36;
constexpr int CONTENT_BOTTOM_PADDING = 5;
constexpr int LIST_BOTTOM_PADDING = 12;

// Redesigned page shell geometry.
constexpr int MENU_ITEM_COUNT = 5;
constexpr int MENU_TOP_PADDING = 20;
constexpr int MENU_ICON_SIZE = 40;
constexpr int MENU_LEFT_MARGIN = 20;
constexpr int MENU_BOTTOM_PADDING = 10;
constexpr int MENU_BOTTOM_SIZE = 70;
constexpr int MENU_BOTTOM_HEIGHT = MENU_BOTTOM_SIZE + MENU_BOTTOM_PADDING;
constexpr int MENU_HEIGHT = MENU_TOP_PADDING + MENU_ICON_SIZE + MENU_BOTTOM_PADDING;
constexpr int SHELL_BATTERY_RIGHT_MARGIN = 10;

// Recent Flow carousel geometry.
constexpr int FLOW_CAROUSEL_HEIGHT = 340;
constexpr int FLOW_CAROUSEL_CENTER_WIDTH = 210;
constexpr int FLOW_CAROUSEL_CENTER_HEIGHT = 318;
constexpr int FLOW_CAROUSEL_SIDE_SCALE_PERCENT = 90;
constexpr int FLOW_CAROUSEL_CARD_GAP = 20;

// Sidebar geometry.
constexpr int SIDEBAR_WIDTH_LIMIT = 320;
constexpr int SIDEBAR_LIST_TOP = MENU_HEIGHT + 34;
constexpr int SIDEBAR_INNER_PADDING = 16;
constexpr int SIDEBAR_TOP_PADDING = 24;
constexpr int SIDEBAR_ROW_GAP = 8;
constexpr int SIDEBAR_ROW_HEIGHT = LIST_ITEM_HEIGHT;
constexpr int SIDEBAR_ICON_SIZE = MENU_ICON_SIZE;

// Existing main-tab chrome geometry, centralized for future tuning.
constexpr int MAIN_TAB_COUNT = 5;
constexpr int MAIN_TAB_ICON_SIZE = 38;
constexpr int MAIN_TAB_SELECTED_BORDER_WIDTH = 38;
constexpr int MAIN_TAB_SELECTED_BORDER_HEIGHT = 5;
constexpr int BOTTOM_TAB_ICON_NUDGE_Y = -3;
constexpr int PAGE_HEADER_TOP_PADDING = 5;
constexpr int PAGE_HEADER_BOTTOM_PADDING = 5;
constexpr int PAGE_HEADER_DIVIDER_THICKNESS = 2;
constexpr int MENU_BATTERY_RIGHT_MARGIN = 80;
constexpr int BOTTOM_MENU_CLOCK_LEFT_MARGIN = 20;

}  // namespace UiLayout
