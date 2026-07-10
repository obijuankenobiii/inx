#include "OpdsServerListActivity.h"

#include <GfxRenderer.h>

#include "browser/OpdsBookBrowserActivity.h"
#include "state/OpdsServerStore.h"
#include "system/Fonts.h"
#include "system/MappedInputManager.h"
#include "system/MenuNav.h"

void OpdsServerListActivity::taskTrampoline(void* param) {
  auto* self = static_cast<OpdsServerListActivity*>(param);
  self->displayTaskLoop();
}

void OpdsServerListActivity::onEnter() {
  ActivityWithSubactivity::onEnter();

  renderingMutex = xSemaphoreCreateMutex();

  updateRequired = true;

  xTaskCreate(&OpdsServerListActivity::taskTrampoline, "OpdsServerListTask", 4096, this, 1, &displayTaskHandle);
}

void OpdsServerListActivity::onExit() {
  ActivityWithSubactivity::onExit();

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;
}

void OpdsServerListActivity::loop() {
  if (subActivity) {
    subActivity->loop();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onBack();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  {
    const int count = OPDS_STORE.getAllServers().size();
    if (mappedInput.wasPressed(MenuNav::itemPrev()) || mappedInput.wasPressed(MenuNav::tabPrev())) {
      if (count > 0) {
        selectedIndex = (selectedIndex - 1 + count) % count;
        updateRequired = true;
      }
    } else if (mappedInput.wasPressed(MenuNav::itemNext()) || mappedInput.wasPressed(MenuNav::tabNext())) {
      if (count > 0) {
        selectedIndex = (selectedIndex + 1) % count;
        updateRequired = true;
      }
    }
  }
}

void OpdsServerListActivity::handleSelection() {
  const auto& servers = OPDS_STORE.getAllServers();
  if (servers.empty()) return;
  if (selectedIndex >= (int)servers.size()) return;

  const auto& srv = servers[selectedIndex];

  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  exitActivity();
  enterNewActivity(new OpdsBookBrowserActivity(
      renderer, mappedInput,
      [this] {
        exitActivity();
        updateRequired = true;
      },
      srv.url, srv.username, srv.password));
  xSemaphoreGive(renderingMutex);
}

void OpdsServerListActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired && !subActivity) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void OpdsServerListActivity::render() {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();

  renderer.text.centered(ATKINSON_HYPERLEGIBLE_12_FONT_ID, 15, "OPDS Server", true, EpdFontFamily::BOLD);

  const auto& servers = OPDS_STORE.getAllServers();

  if (servers.empty()) {
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 80, "No servers configured", false);
    renderer.text.centered(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 100, "Add servers via the web interface", false);
  } else {
    if (selectedIndex >= 0 && selectedIndex < (int)servers.size()) {
      renderer.rectangle.fill(0, 70 + selectedIndex * 30 - 2, pageWidth - 1, 30,
                              static_cast<int>(GfxRenderer::FillTone::Ink));
    }

    for (int i = 0; i < (int)servers.size(); i++) {
      const int y = 70 + i * 30;
      const bool isSelected = (i == selectedIndex);
      renderer.text.render(ATKINSON_HYPERLEGIBLE_10_FONT_ID, 20, y, servers[i].name.c_str(), !isSelected);
    }
  }

  const auto labels = mappedInput.mapLabels("« Back", !servers.empty() ? "Browse" : "", "", "");
  renderer.ui.buttonHints(ATKINSON_HYPERLEGIBLE_10_FONT_ID, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
