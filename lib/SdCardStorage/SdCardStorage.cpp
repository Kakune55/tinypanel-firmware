#include "SdCardStorage.h"

#include <BoardConfig.h>
#include <dirent.h>
#include <esp_vfs_fat.h>
#include <sys/stat.h>
#include <unistd.h>

bool SdCardStorage::begin(const char* mountPoint, int clk, int cmd, int d0, int width) {
  if (mounted_) {
    return true;
  }

  mountPoint_ = mountPoint;

  esp_vfs_fat_sdmmc_mount_config_t mountConfig = {};
  mountConfig.format_if_mount_failed = false;
  mountConfig.max_files = 5;
  mountConfig.allocation_unit_size = 16 * 1024;

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  sdmmc_slot_config_t slotConfig = SDMMC_SLOT_CONFIG_DEFAULT();
  slotConfig.width = width;
  slotConfig.clk = static_cast<gpio_num_t>(clk);
  slotConfig.cmd = static_cast<gpio_num_t>(cmd);
  slotConfig.d0 = static_cast<gpio_num_t>(d0);

  esp_err_t err = esp_vfs_fat_sdmmc_mount(mountPoint_, &host, &slotConfig, &mountConfig, &card_);
  if (err != ESP_OK) {
    card_ = nullptr;
    mounted_ = false;
    cardSizeBytes_ = 0;
    setError(err);
    return false;
  }

  mounted_ = true;
  cardSizeBytes_ = static_cast<uint64_t>(card_->csd.capacity) * card_->csd.sector_size;
  setErrorText("ok");
  return true;
}

void SdCardStorage::end() {
  if (!mounted_) {
    return;
  }

  esp_vfs_fat_sdcard_unmount(mountPoint_, card_);
  card_ = nullptr;
  mounted_ = false;
  cardSizeBytes_ = 0;
  setErrorText("not initialized");
}

bool SdCardStorage::isMounted() const {
  return mounted_;
}

const char* SdCardStorage::mountPoint() const {
  return mountPoint_;
}

const char* SdCardStorage::lastErrorText() const {
  return lastError_;
}

uint64_t SdCardStorage::cardSizeBytes() const {
  return cardSizeBytes_;
}

bool SdCardStorage::exists(const char* path) const {
  if (!isReady()) {
    return false;
  }

  struct stat st = {};
  return stat(absolutePath(path).c_str(), &st) == 0;
}

bool SdCardStorage::writeText(const char* path, const String& text, bool append) {
  if (!isReady()) {
    setErrorText("not mounted");
    return false;
  }

  FILE* file = fopen(absolutePath(path).c_str(), append ? "ab" : "wb");
  if (!file) {
    setErrorText("open failed");
    return false;
  }

  size_t written = fwrite(text.c_str(), 1, text.length(), file);
  fclose(file);

  if (written != text.length()) {
    setErrorText("write failed");
    return false;
  }

  setErrorText("ok");
  return true;
}

bool SdCardStorage::readText(const char* path, String& out, size_t maxBytes) const {
  out = "";
  if (!isReady()) {
    return false;
  }

  FILE* file = fopen(absolutePath(path).c_str(), "rb");
  if (!file) {
    return false;
  }

  size_t readBytes = 0;
  while (readBytes < maxBytes) {
    int c = fgetc(file);
    if (c == EOF) {
      break;
    }
    out += static_cast<char>(c);
    ++readBytes;
  }

  fclose(file);
  return true;
}

bool SdCardStorage::remove(const char* path) {
  if (!isReady()) {
    setErrorText("not mounted");
    return false;
  }

  bool ok = unlink(absolutePath(path).c_str()) == 0;
  setErrorText(ok ? "ok" : "remove failed");
  return ok;
}

bool SdCardStorage::appendLine(const char* path, const String& line) {
  String text = line;
  text += "\n";
  return writeText(path, text, true);
}

void SdCardStorage::printInfo(Stream& out) const {
  if (!mounted_ || !card_) {
    out.printf("SD card: not mounted (%s)\n", lastError_);
    return;
  }

  out.printf("SD card: mounted at %s\n", mountPoint_);
  out.printf("SD card size: %.2f MB\n", cardSizeBytes_ / (1024.0 * 1024.0));
  out.printf("SD card sector size: %u bytes\n", card_->csd.sector_size);
}

String SdCardStorage::absolutePath(const char* path) const {
  if (!path || path[0] == '\0') {
    return String(mountPoint_);
  }

  String value(path);
  if (value.startsWith(mountPoint_)) {
    return value;
  }
  if (!value.startsWith("/")) {
    value = "/" + value;
  }
  return String(mountPoint_) + value;
}

bool SdCardStorage::isReady() const {
  return mounted_ && card_ && sdmmc_get_status(card_) == ESP_OK;
}

void SdCardStorage::setError(esp_err_t err) {
  if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_RESPONSE || err == ESP_ERR_NOT_FOUND) {
    setErrorText("NO CARD");
    return;
  }

  const char* name = esp_err_to_name(err);
  snprintf(lastError_, sizeof(lastError_), "%s", name ? name : "unknown");
}

void SdCardStorage::setErrorText(const char* text) {
  snprintf(lastError_, sizeof(lastError_), "%s", text ? text : "unknown");
}
