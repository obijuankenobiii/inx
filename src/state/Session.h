#pragma once
#include <iosfwd>
#include <string>

class Session {
  // Static instance
  static Session instance;

 public:
  std::string lastRead;
  uint8_t lastSleepImage;
  ~Session() = default;

  // Get singleton instance
  static Session& getInstance() { return instance; }

  bool saveToFile() const;

  bool loadFromFile();
};

// Helper macro to access settings
#define APP_STATE Session::getInstance()
