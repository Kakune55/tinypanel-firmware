#pragma once

#include <Arduino.h>
#include <BoardConfig.h>
#include <driver/sdmmc_host.h>
#include <esp_err.h>
#include <sdmmc_cmd.h>

class SdCardStorage {
 public:
  bool begin(const char* mountPoint = "/sdcard",
             int clk = BoardConfig::SdmmcClk,
             int cmd = BoardConfig::SdmmcCmd,
             int d0 = BoardConfig::SdmmcD0,
             int width = BoardConfig::SdmmcBusWidth);
  void end();

  bool isMounted() const;
  const char* mountPoint() const;
  const char* lastErrorText() const;
  uint64_t cardSizeBytes() const;
  uint64_t usedBytes() const;

  bool exists(const char* path) const;
  bool makeDir(const char* path);
  bool ensureDir(const char* path);
  bool writeText(const char* path, const String& text, bool append = false);
  bool writeTextAtomic(const char* path, const String& text);
  bool writeBinaryAtomic(const char* path, const uint8_t* data, size_t len);
  bool readText(const char* path, String& out, size_t maxBytes = 4096) const;
  bool readBinaryBuffer(const char* path, uint8_t*& out, size_t& outLen, size_t maxBytes, bool preferPsram = false) const;
  void freeBuffer(uint8_t* buffer) const;
  bool remove(const char* path);
  bool appendLine(const char* path, const String& line);
  void printInfo(Stream& out) const;

 private:
  String absolutePath(const char* path) const;
  bool isReady() const;
  void setError(esp_err_t err);
  void setErrorText(const char* text);

  const char* mountPoint_ = "/sdcard";
  sdmmc_card_t* card_ = nullptr;
  bool mounted_ = false;
  uint64_t cardSizeBytes_ = 0;
  char lastError_[48] = "not initialized";
};
