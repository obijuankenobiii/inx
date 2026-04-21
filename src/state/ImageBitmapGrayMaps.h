#pragma once

#include <GfxRenderer.h>

#include "state/SystemSetting.h"

/** Reader image contrast (Low / Medium / High) → scaled bitmap gray style. */
inline GfxRenderer::BitmapGrayRenderStyle readerImageBitmapGrayStyle() {
  switch (SETTINGS.readerImagePresentation) {
    case SystemSetting::IMAGE_PRESENTATION_HIGH:
      return GfxRenderer::BitmapGrayRenderStyle::Dark;
    case SystemSetting::IMAGE_PRESENTATION_MEDIUM:
      return GfxRenderer::BitmapGrayRenderStyle::FullGray;
    default:
      return GfxRenderer::BitmapGrayRenderStyle::Balanced;
  }
}

/** System/library/sleep/stats image contrast → same mapping as reader. */
inline GfxRenderer::BitmapGrayRenderStyle displayImageBitmapGrayStyle() {
  switch (SETTINGS.displayImagePresentation) {
    case SystemSetting::IMAGE_PRESENTATION_HIGH:
      return GfxRenderer::BitmapGrayRenderStyle::Dark;
    case SystemSetting::IMAGE_PRESENTATION_MEDIUM:
      return GfxRenderer::BitmapGrayRenderStyle::FullGray;
    default:
      return GfxRenderer::BitmapGrayRenderStyle::Balanced;
  }
}
