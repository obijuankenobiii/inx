#pragma once

/**
 * @file OpdsBookBrowserActivity.h
 * @brief Public interface and types for OpdsBookBrowserActivity.
 */

#include <OpdsParser.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <functional>
#include <string>
#include <vector>

#include "../ActivityWithSubactivity.h"

/**
 * Activity for browsing and downloading books from an OPDS server.
 * Supports navigation through catalog hierarchy and downloading EPUBs.
 * When WiFi connection fails, launches WiFi selection to let user connect.
 */
class OpdsBookBrowserActivity final : public ActivityWithSubactivity {
 public:
  enum class BrowserState {
    CHECK_WIFI,      
    WIFI_SELECTION,  
    LOADING,         
    BROWSING,        
    DOWNLOADING,     
    ERROR            
  };

  explicit OpdsBookBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const std::function<void()>& onGoToRecent)
      : ActivityWithSubactivity("OpdsBookBrowser", renderer, mappedInput), onGoToRecent(onGoToRecent) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;

 private:
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  bool updateRequired = false;

  BrowserState state = BrowserState::LOADING;
  std::vector<OpdsEntry> entries;
  std::vector<std::string> navigationHistory;  
  std::string currentPath;                     
  int selectorIndex = 0;
  std::string errorMessage;
  std::string statusMessage;
  size_t downloadProgress = 0;
  size_t downloadTotal = 0;

  const std::function<void()> onGoToRecent;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  void render() const;

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchFeed(const std::string& path);
  void navigateToEntry(const OpdsEntry& entry);
  void navigateBack();
  void downloadBook(const OpdsEntry& book);
};
