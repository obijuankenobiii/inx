#pragma once

#ifdef SIMULATOR

class GfxRenderer {
 public:
  enum Orientation { Portrait, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };

  Orientation getOrientation() const { return orientation; }
  void setOrientation(Orientation nextOrientation) { orientation = nextOrientation; }

 private:
  Orientation orientation = LandscapeClockwise;
};

#else
#error "src/simulator/GfxRenderer.h is only for simulator builds"
#endif
