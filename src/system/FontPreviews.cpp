#include "FontPreviews.h"

#include <GfxRenderer.h>

#include "font/font_preview_alegreya_14_regular.h"
#include "font/font_preview_atkinsonhl_mono_14_regular.h"
#include "font/font_preview_atkinsonhl_next_14_regular.h"
#include "font/font_preview_bitterpro_14_regular.h"
#include "font/font_preview_chareink7_14_regular.h"
#include "font/font_preview_charis_14_regular.h"
#include "font/font_preview_inter_14_regular.h"
#include "font/font_preview_lexend_14_regular.h"
#include "font/font_preview_lexicaultralegible_14_regular.h"
#include "font/font_preview_literata_14_regular.h"
#include "font/font_preview_lora_14_regular.h"
#include "font/font_preview_merriweather_14_regular.h"
#include "font/font_preview_notosans_14_regular.h"
#include "font/font_preview_opendyslexic_14_regular.h"
#include "font/font_preview_plexmono_14_regular.h"
#include "font/font_preview_plexsans_14_regular.h"
#include "font/font_preview_sourcesans3_14_regular.h"
#include "font/font_preview_sourceserif4_14_regular.h"
#include "font/font_preview_tinos_14_regular.h"

namespace FontPreviews {
namespace {

constexpr int kFirstPreviewFontId = 4400;

struct PreviewSlot {
  const char* family;
  int id;
  const EpdFontData* data;
};

EpdFont g_previewFonts[] = {
    EpdFont(&font_preview_alegreya_14_regular),
    EpdFont(&font_preview_atkinsonhl_mono_14_regular),
    EpdFont(&font_preview_atkinsonhl_next_14_regular),
    EpdFont(&font_preview_bitterpro_14_regular),
    EpdFont(&font_preview_chareink7_14_regular),
    EpdFont(&font_preview_charis_14_regular),
    EpdFont(&font_preview_inter_14_regular),
    EpdFont(&font_preview_lexend_14_regular),
    EpdFont(&font_preview_lexicaultralegible_14_regular),
    EpdFont(&font_preview_literata_14_regular),
    EpdFont(&font_preview_lora_14_regular),
    EpdFont(&font_preview_merriweather_14_regular),
    EpdFont(&font_preview_notosans_14_regular),
    EpdFont(&font_preview_opendyslexic_14_regular),
    EpdFont(&font_preview_plexmono_14_regular),
    EpdFont(&font_preview_plexsans_14_regular),
    EpdFont(&font_preview_sourcesans3_14_regular),
    EpdFont(&font_preview_sourceserif4_14_regular),
    EpdFont(&font_preview_tinos_14_regular),
};

EpdFontFamily g_previewFamilies[] = {
    EpdFontFamily(&g_previewFonts[0]),
    EpdFontFamily(&g_previewFonts[1]),
    EpdFontFamily(&g_previewFonts[2]),
    EpdFontFamily(&g_previewFonts[3]),
    EpdFontFamily(&g_previewFonts[4]),
    EpdFontFamily(&g_previewFonts[5]),
    EpdFontFamily(&g_previewFonts[6]),
    EpdFontFamily(&g_previewFonts[7]),
    EpdFontFamily(&g_previewFonts[8]),
    EpdFontFamily(&g_previewFonts[9]),
    EpdFontFamily(&g_previewFonts[10]),
    EpdFontFamily(&g_previewFonts[11]),
    EpdFontFamily(&g_previewFonts[12]),
    EpdFontFamily(&g_previewFonts[13]),
    EpdFontFamily(&g_previewFonts[14]),
    EpdFontFamily(&g_previewFonts[15]),
    EpdFontFamily(&g_previewFonts[16]),
    EpdFontFamily(&g_previewFonts[17]),
    EpdFontFamily(&g_previewFonts[18]),
};

constexpr PreviewSlot kPreviewSlots[] = {
    {"Alegreya", kFirstPreviewFontId + 0, &font_preview_alegreya_14_regular},
    {"AtkinsonHL-Mono", kFirstPreviewFontId + 1, &font_preview_atkinsonhl_mono_14_regular},
    {"AtkinsonHL-Next", kFirstPreviewFontId + 2, &font_preview_atkinsonhl_next_14_regular},
    {"BitterPro", kFirstPreviewFontId + 3, &font_preview_bitterpro_14_regular},
    {"ChareInk7", kFirstPreviewFontId + 4, &font_preview_chareink7_14_regular},
    {"Charis", kFirstPreviewFontId + 5, &font_preview_charis_14_regular},
    {"Inter", kFirstPreviewFontId + 6, &font_preview_inter_14_regular},
    {"Lexend", kFirstPreviewFontId + 7, &font_preview_lexend_14_regular},
    {"LexicaUltralegible", kFirstPreviewFontId + 8, &font_preview_lexicaultralegible_14_regular},
    {"Literata", kFirstPreviewFontId + 9, &font_preview_literata_14_regular},
    {"Lora", kFirstPreviewFontId + 10, &font_preview_lora_14_regular},
    {"Merriweather", kFirstPreviewFontId + 11, &font_preview_merriweather_14_regular},
    {"NotoSans", kFirstPreviewFontId + 12, &font_preview_notosans_14_regular},
    {"OpenDyslexic", kFirstPreviewFontId + 13, &font_preview_opendyslexic_14_regular},
    {"PlexMono", kFirstPreviewFontId + 14, &font_preview_plexmono_14_regular},
    {"PlexSans", kFirstPreviewFontId + 15, &font_preview_plexsans_14_regular},
    {"SourceSans3", kFirstPreviewFontId + 16, &font_preview_sourcesans3_14_regular},
    {"SourceSerif4", kFirstPreviewFontId + 17, &font_preview_sourceserif4_14_regular},
    {"Tinos", kFirstPreviewFontId + 18, &font_preview_tinos_14_regular},
};

}  // namespace

void initialize(GfxRenderer& renderer) {
  for (size_t i = 0; i < sizeof(kPreviewSlots) / sizeof(kPreviewSlots[0]); ++i) {
    renderer.insertFont(kPreviewSlots[i].id, g_previewFamilies[i]);
  }
}

int fontIdForFamily(const std::string& family) {
  for (const PreviewSlot& slot : kPreviewSlots) {
    if (family == slot.family) return slot.id;
    // The X3 package manager stores the variant in installFamily, e.g.
    // "Alegreya 1-bit". Preview assets are shared by both variants, so
    // accept the package family followed by its variant as well.
    const std::string prefix = std::string(slot.family) + " ";
    if (family.compare(0, prefix.size(), prefix) == 0) return slot.id;
  }
  return -1;
}

}  // namespace FontPreviews
