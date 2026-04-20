#pragma once

#include <GfxRenderer.h>

#include "state/SystemSetting.h"

/** Reader "Contrast" → scaled bitmap gray style (Balance = former full-gray pipeline). */
inline GfxRenderer::BitmapGrayRenderStyle readerImageBitmapGrayStyle() {
  if (SETTINGS.readerImagePresentation == SystemSetting::IMAGE_PRESENTATION_DARK) {
    return GfxRenderer::BitmapGrayRenderStyle::Dark;
  }
  return GfxRenderer::BitmapGrayRenderStyle::FullGray;
}

/** System/library/sleep/stats "Contrast" → same mapping as reader. */
inline GfxRenderer::BitmapGrayRenderStyle displayImageBitmapGrayStyle() {
  if (SETTINGS.displayImagePresentation == SystemSetting::IMAGE_PRESENTATION_DARK) {
    return GfxRenderer::BitmapGrayRenderStyle::Dark;
  }
  return GfxRenderer::BitmapGrayRenderStyle::FullGray;
}
