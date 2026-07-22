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
  enum class BrowserState { CHECK_WIFI, WIFI_SELECTION, LOADING, BROWSING, DOWNLOADING, ERROR };

  /** Constructs an OpdsBookBrowserActivity that uses the globally configured OPDS server settings. */
  explicit OpdsBookBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const std::function<void()>& onGoToRecent, const std::string& serverUrl,
                                   const std::string& serverUsername, const std::string& serverPassword)
      : ActivityWithSubactivity("OpdsBookBrowser", renderer, mappedInput),
        onGoToRecent(onGoToRecent),
        serverUrl(serverUrl),
        serverUsername(serverUsername),
        serverPassword(serverPassword) {}

  /** Starts the display task and checks WiFi connectivity before loading the feed. */
  void onEnter() override;
  /** Stops the display task and clears loaded catalog data. */
  void onExit() override;
  /** Handles input for browsing the catalog and downloading books. */
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
  std::string serverUrl;
  std::string serverUsername;
  std::string serverPassword;

  /** Static trampoline that dispatches to the instance's displayTaskLoop. */
  static void taskTrampoline(void* param);
  /** Background task loop that renders the screen when an update is required. */
  [[noreturn]] void displayTaskLoop();
  /** Renders the current browser state (loading, error, catalog list, etc). */
  void render() const;

  /** Checks WiFi status and either fetches the feed or launches WiFi selection. */
  void checkAndConnectWifi();
  /** Enters the WiFi selection subactivity. */
  void launchWifiSelection();
  /** Handles completion of the WiFi selection subactivity. */
  void onWifiSelectionComplete(bool connected);
  /** Fetches and parses the OPDS feed at the given path. */
  void fetchFeed(const std::string& path);
  /** Navigates into a catalog or book entry. */
  void navigateToEntry(const OpdsEntry& entry);
  /** Navigates back to the previous catalog entry, or exits if at the root. */
  void navigateBack();
  /** Downloads the given book entry to the SD card. */
  void downloadBook(const OpdsEntry& book);
};
