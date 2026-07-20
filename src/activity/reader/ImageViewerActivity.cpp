#include "ImageViewerActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <ImageRender.h>
#include <SDCardManager.h>

#include <algorithm>
#include <utility>

#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "util/StringUtils.h"

namespace {

std::string parentPathFor(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  if (slash == std::string::npos || slash == 0) {
    return "/";
  }
  return path.substr(0, slash);
}

bool supportedImage(const std::string& filename) {
  return StringUtils::checkFileExtension(filename, ".bmp") || StringUtils::checkFileExtension(filename, ".jpg") ||
         StringUtils::checkFileExtension(filename, ".jpeg") || StringUtils::checkFileExtension(filename, ".png");
}

}  // namespace

ImageViewerActivity::ImageViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path,
                                         const std::function<void()>& onGoBack)
    : Activity("ImageViewer", renderer, mappedInput), currentPath_(std::move(path)), onGoBack_(onGoBack) {}

void ImageViewerActivity::onEnter() {
  loadImages();
  render();
}

void ImageViewerActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onGoBack_();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Right) ||
      mappedInput.wasPressed(MappedInputManager::Button::Down) ||
      mappedInput.wasPressed(MappedInputManager::Button::PageForward)) {
    turn(1);
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Left) ||
      mappedInput.wasPressed(MappedInputManager::Button::Up) ||
      mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
    turn(-1);
    return;
  }

  if (updateRequired_) {
    updateRequired_ = false;
    render();
  }
}

void ImageViewerActivity::loadImages() {
  images_.clear();
  const std::string folder = parentPathFor(currentPath_);
  auto dir = SdMan.open(folder.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    images_.push_back(currentPath_);
    currentIndex_ = 0;
    return;
  }

  dir.rewindDirectory();
  char name[256];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    file.getName(name, sizeof(name));
    if (!file.isDirectory()) {
      const std::string filename = name;
      if (supportedImage(filename)) {
        std::string path = folder;
        if (path.back() != '/') path += "/";
        path += filename;
        images_.push_back(path);
      }
    }
    file.close();
  }
  dir.close();

  std::sort(images_.begin(), images_.end());
  auto it = std::find(images_.begin(), images_.end(), currentPath_);
  currentIndex_ = it == images_.end() ? 0 : static_cast<int>(std::distance(images_.begin(), it));
  if (images_.empty()) {
    images_.push_back(currentPath_);
    currentIndex_ = 0;
  }
}

void ImageViewerActivity::render() {
  renderer.clearScreen();
  if (images_.empty() || currentIndex_ < 0 || currentIndex_ >= static_cast<int>(images_.size())) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, renderer.getScreenHeight() / 2, "Image unavailable");
    renderer.displayBuffer();
    return;
  }

  ImageRender::Options options;
  options.mode = ImageRenderMode::TwoBit;
  options.cropToFill = false;
  options.useDisplayCache = true;
  options.quality = true;
  options.fastQuality = true;

  const std::string& imagePath = images_[static_cast<size_t>(currentIndex_)];
  const ImageRender image = ImageRender::create(renderer, imagePath);
  if (!image.hasCachedTwoBit(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(), options, /*quality=*/true)) {
    renderPlaceholder(imagePath);
  }

  const bool rendered =
      image.displayGrayscale(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight(), options, /*quality=*/true);
  if (!rendered) {
    renderer.clearScreen();
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, renderer.getScreenHeight() / 2, "Image unavailable");
    renderer.displayBuffer();
  }
}

void ImageViewerActivity::renderPlaceholder(const std::string& imagePath) {
  const int screenW = renderer.getScreenWidth();
  const int screenH = renderer.getScreenHeight();
  int imageW = 0;
  int imageH = 0;
  ImageRender::getDimensions(imagePath, &imageW, &imageH);

  const int maxW = screenW - 48;
  const int maxH = screenH - 72;
  int boxW = maxW;
  int boxH = maxH;
  if (imageW > 0 && imageH > 0) {
    const float scale = std::min(static_cast<float>(maxW) / static_cast<float>(imageW),
                                 static_cast<float>(maxH) / static_cast<float>(imageH));
    boxW = std::max(24, static_cast<int>(imageW * scale));
    boxH = std::max(24, static_cast<int>(imageH * scale));
  }

  const int boxX = (screenW - boxW) / 2;
  const int boxY = (screenH - boxH) / 2;
  renderer.clearScreen();
  renderer.rectangle.fill(boxX, boxY, boxW, boxH, false, /*rounded=*/false, /*subtle=*/true);
  renderer.rectangle.render(boxX, boxY, boxW, boxH, true, /*rounded=*/false, /*subtle=*/true);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void ImageViewerActivity::turn(int delta) {
  if (images_.empty()) {
    return;
  }
  const int count = static_cast<int>(images_.size());
  currentIndex_ = (currentIndex_ + delta + count) % count;
  updateRequired_ = true;
}
