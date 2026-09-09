#pragma once

/**
 * @file Fonts.h
 * @brief Built-in bitmap font headers and numeric font IDs.
 */

#include "font/montserrat_10_bold.h"
#include "font/montserrat_10_bolditalic.h"
#include "font/montserrat_10_italic.h"
#include "font/montserrat_10_regular.h"
#include "font/montserrat_12_bold.h"
#include "font/montserrat_12_bolditalic.h"
#include "font/montserrat_12_italic.h"
#include "font/montserrat_12_regular.h"
#include "font/montserrat_14_bold.h"
#include "font/montserrat_14_bolditalic.h"
#include "font/montserrat_14_italic.h"
#include "font/montserrat_14_regular.h"
#include "font/montserrat_16_bold.h"
#include "font/montserrat_16_bolditalic.h"
#include "font/montserrat_16_italic.h"
#include "font/montserrat_16_regular.h"
#include "font/montserrat_18_bold.h"
#include "font/montserrat_18_bolditalic.h"
#include "font/montserrat_18_italic.h"
#include "font/montserrat_18_regular.h"
#include "font/montserrat_8_regular.h"
#include "font/montserrat_clock_70_bold.h"
#include "font/montserrat_clock_70_regular.h"

#define MONTSERRAT_8_FONT_ID (2501)
#define MONTSERRAT_10_FONT_ID (2502)
#define MONTSERRAT_12_FONT_ID (2503)
#define MONTSERRAT_14_FONT_ID (2504)
#define MONTSERRAT_16_FONT_ID (2505)
#define MONTSERRAT_18_FONT_ID (2506)

#define MONTSERRAT_CLOCK_70_FONT_ID (4001)

/** @brief Font used by the native settings activities. */
inline int systemFontId() { return MONTSERRAT_10_FONT_ID; }
