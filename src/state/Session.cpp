#include "state/Session.h"

#include <HardwareSerial.h>
#include <SDCardManager.h>
#include <Serialization.h>

namespace {
constexpr uint8_t STATE_FILE_VERSION = 2;
constexpr char STATE_FILE[] = "/.system/state.bin";
}  // namespace

Session Session::instance;

bool Session::saveToFile() const {
  // Ensure directory exists
  std::string dirPath = STATE_FILE;
  dirPath = dirPath.substr(0, dirPath.find_last_of('/'));
  SdMan.mkdir(dirPath.c_str());
  
  FsFile outputFile;
  if (!SdMan.openFileForWrite("CPS", STATE_FILE, outputFile)) {
    return false;
  }

  serialization::writePod(outputFile, STATE_FILE_VERSION);
  serialization::writeString(outputFile, lastRead);
  serialization::writePod(outputFile, lastSleepImage);
  
  outputFile.close();
  return true;
}

bool Session::loadFromFile() {
  // Check if file exists
  if (!SdMan.exists(STATE_FILE)) {
    lastRead = "";
    lastSleepImage = 0;
    return saveToFile();
  }

  FsFile inputFile;
  if (!SdMan.openFileForRead("CPS", STATE_FILE, inputFile)) {
    lastRead = "";
    lastSleepImage = 0;
    return saveToFile();
  }

  uint8_t version;
  serialization::readPod(inputFile, version);
  
  if (version > STATE_FILE_VERSION) {
    inputFile.close();
    lastRead = "";
    lastSleepImage = 0;
    return saveToFile();
  }

  serialization::readString(inputFile, lastRead);

  if (version >= 2) {
    serialization::readPod(inputFile, lastSleepImage);
  } else {
    lastSleepImage = 0;
  }

  inputFile.close();
  
  // Basic sanity check
  if (lastRead.length() > 512) {
    lastRead = "";
    lastSleepImage = 0;
    return saveToFile();
  }
  
  return true;
}