#pragma once

#include <functional>
#include <string>
#include <vector>

#include "activity/Activity.h"

class ImageViewerActivity final : public Activity {
 public:
  explicit ImageViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path,
                               const std::function<void()>& onGoBack);

  void onEnter() override;
  void loop() override;

 private:
  std::string currentPath_;
  std::vector<std::string> images_;
  int currentIndex_ = 0;
  bool updateRequired_ = false;
  const std::function<void()> onGoBack_;

  void loadImages();
  void render();
  void renderPlaceholder(const std::string& imagePath);
  void turn(int delta);
};
