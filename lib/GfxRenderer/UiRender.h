#pragma once

class GfxRenderer;

class UiRender {
 public:
  explicit UiRender(GfxRenderer& g) : gfx(g) {}

  /**
   * Optional app-level policy: when the callback returns true, {@link buttonHints} and
   * {@link sideButtonHints} become no-ops. Used by firmware to honor "Hide button hints".
   * Pass nullptr to always draw hints (default).
   */
  using HintsHiddenFn = bool (*)();
  static void setHintsHiddenFn(HintsHiddenFn fn);

  /** @param topY Row's top edge; defaults to the standard bottom-anchored position (pageHeight - 40). */
  void buttonHints(int fontId, const char* btn1, const char* btn2, const char* btn3, const char* btn4,
                   int topY = -1) const;
  void sideButtonHints(int fontId, const char* powerBtn, const char* topBtn, const char* bottomBtn) const;
  void dottedRect(int x, int y, int width, int height, bool state = true) const;
  void fillSparseInkLatticeInRect(int x, int y, int width, int height, int latticeStep = 2) const;

 private:
  static bool hintsHidden();

  GfxRenderer& gfx;
  static HintsHiddenFn hintsHiddenFn_;
};
