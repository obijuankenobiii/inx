#pragma once

/**
 * @file Battery.h
 * @brief Public interface and types for Battery.
 */

#include <BatteryMonitor.h>

#define BAT_GPIO0 0

static BatteryMonitor battery(BAT_GPIO0);
