#include "UiRender.h"

#include <algorithm>

#include "GfxRenderer.h"

void UiRender::buttonHints(const int fontId, const char* btn1, const char* btn2, const char* btn3,
                               const char* btn4) const {
  const GfxRenderer::Orientation origOrientation = gfx.getOrientation();
  gfx.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = gfx.getScreenHeight();
  constexpr int buttonWidth = 106;
  constexpr int buttonHeight = 40;
  constexpr int buttonY = 40;
  constexpr int textYOffset = 7;
  constexpr int buttonPositions[] = {25, 130, 245, 350};
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      const int x = buttonPositions[i];
      gfx.rectangle.fill(x, pageHeight - buttonY, buttonWidth, buttonHeight, false, true);
      gfx.rectangle.render(x, pageHeight - buttonY, buttonWidth, buttonHeight, true, true);
      const int textWidth = gfx.text.getWidth(fontId, labels[i]);
      const int textX = x + (buttonWidth - 1 - textWidth) / 2;
      gfx.text.render(fontId, textX, pageHeight - buttonY + textYOffset, labels[i]);
    }
  }

  gfx.setOrientation(origOrientation);
}

void UiRender::sideButtonHints(const int fontId, const char* powerBtn, const char* topBtn,
                                   const char* bottomBtn) const {
  const int screenWidth = gfx.getScreenWidth();
  constexpr int sideW = 32;
  constexpr int upDownH = 80;
  constexpr int powerH = 80;
  constexpr int buttonX = 5;
  constexpr int topButtonY = 345;

  const int x = screenWidth - buttonX - sideW;
  const bool hasPower = powerBtn != nullptr && powerBtn[0] != '\0';
  const char* labels[] = {topBtn, bottomBtn};

  if (hasPower) {
    const int powerY = 120;
    gfx.line.render(x, powerY, x, powerY + powerH - 1);
    gfx.line.render(x + sideW - 1, powerY, x + sideW - 1, powerY + powerH - 1);
    gfx.line.render(x, powerY, x + sideW - 1, powerY);
    gfx.line.render(x, powerY + powerH - 1, x + sideW - 1, powerY + powerH - 1);
    const int ptw = gfx.text.getWidth(fontId, powerBtn);
    const int pth = gfx.text.getFontAscenderSize(fontId);
    const int ptextX = x + (sideW - pth) / 2;
    const int ptextY = powerY + (powerH + ptw) / 2;
    gfx.text.rotated90CW(fontId, ptextX, ptextY, powerBtn);
  }

  if (topBtn != nullptr && topBtn[0] != '\0') {
    gfx.line.render(x, topButtonY, x + sideW - 1, topButtonY);
    gfx.line.render(x, topButtonY, x, topButtonY + upDownH - 1);
    gfx.line.render(x + sideW - 1, topButtonY, x + sideW - 1, topButtonY + upDownH - 1);
  }

  if ((topBtn != nullptr && topBtn[0] != '\0') || (bottomBtn != nullptr && bottomBtn[0] != '\0')) {
    gfx.line.render(x, topButtonY + upDownH, x + sideW - 1, topButtonY + upDownH);
  }

  if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
    gfx.line.render(x, topButtonY + upDownH, x, topButtonY + 2 * upDownH - 1);
    gfx.line.render(x + sideW - 1, topButtonY + upDownH, x + sideW - 1, topButtonY + 2 * upDownH - 1);
    gfx.line.render(x, topButtonY + 2 * upDownH - 1, x + sideW - 1, topButtonY + 2 * upDownH - 1);
  }

  for (int i = 0; i < 2; i++) {
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      const int y = topButtonY + i * upDownH;
      const int textWidth = gfx.text.getWidth(fontId, labels[i]);
      const int textHeight = gfx.text.getFontAscenderSize(fontId);
      const int textX = x + (sideW - textHeight) / 2;
      const int textY = y + (upDownH + textWidth) / 2;
      gfx.text.rotated90CW(fontId, textX, textY, labels[i]);
    }
  }
}

void UiRender::dottedRect(const int x, const int y, const int width, const int height, const bool state) const {
  gfx.rectangle.dotted(x, y, width, height, state);
}

void UiRender::fillSparseInkLatticeInRect(const int x, const int y, const int width, const int height,
                                          const int latticeStep) const {
  if (width <= 0 || height <= 0) {
    return;
  }
  int step = latticeStep;
  if (step < 2) {
    step = 2;
  }
  const bool pow2 = (step & (step - 1)) == 0;
  const int sw = gfx.getScreenWidth();
  const int sh = gfx.getScreenHeight();
  const int x1 = std::max(0, x);
  const int y1 = std::max(0, y);
  const int x2 = std::min(sw, x + width);
  const int y2 = std::min(sh, y + height);
  if (pow2) {
    const int mask = step - 1;
    for (int py = (y1 + step - 1) & ~mask; py < y2; py += step) {
      for (int px = (x1 + step - 1) & ~mask; px < x2; px += step) {
        gfx.drawPixel(px, py, true);
      }
    }
    return;
  }
  for (int py = y1; py < y2; py += step) {
    for (int px = x1; px < x2; px += step) {
      gfx.drawPixel(px, py, true);
    }
  }
}
