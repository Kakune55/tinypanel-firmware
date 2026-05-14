#include "SdCardStorage.h"

#include <BoardConfig.h>
#include <cstring>
#include <dirent.h>
#include <esp_vfs_fat.h>
#include <ff.h>
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

uint64_t SdCardStorage::usedBytes() const {
  if (!mounted_) {
    return 0;
  }

  FATFS* fs = nullptr;
  DWORD freeClusters = 0;
  if (f_getfree("0:", &freeClusters, &fs) != FR_OK || fs == nullptr) {
    return 0;
  }

  const uint64_t bytesPerCluster = static_cast<uint64_t>(fs->csize) * fs->ssize;
  const uint64_t total = static_cast<uint64_t>(fs->n_fatent - 2) * bytesPerCluster;
  const uint64_t free = static_cast<uint64_t>(freeClusters) * bytesPerCluster;
  return total > free ? total - free : 0;
}

bool SdCardStorage::exists(const char* path) const {
  if (!isReady()) {
    return false;
  }

  struct stat st = {};
  return stat(absolutePath(path).c_str(), &st) == 0;
}

bool SdCardStorage::makeDir(const char* path) {
  if (!isReady()) {
    setErrorText("not mounted");
    return false;
  }

  const String fullPath = absolutePath(path);
  struct stat st = {};
  if (stat(fullPath.c_str(), &st) == 0) {
    const bool ok = S_ISDIR(st.st_mode);
    setErrorText(ok ? "ok" : "not a dir");
    return ok;
  }

  const bool ok = mkdir(fullPath.c_str(), 0755) == 0;
  setErrorText(ok ? "ok" : "mkdir failed");
  return ok;
}

bool SdCardStorage::ensureDir(const char* path) {
  if (!isReady()) {
    setErrorText("not mounted");
    return false;
  }
  if (!path || path[0] == '\0' || strcmp(path, "/") == 0) {
    setErrorText("ok");
    return true;
  }

  String value(path);
  if (!value.startsWith("/")) {
    value = "/" + value;
  }
  while (value.length() > 1 && value.endsWith("/")) {
    value.remove(value.length() - 1);
  }

  int start = 1;
  while (start < static_cast<int>(value.length())) {
    int slash = value.indexOf('/', start);
    String part = slash < 0 ? value : value.substring(0, slash);
    if (!makeDir(part.c_str())) {
      return false;
    }
    if (slash < 0) {
      break;
    }
    start = slash + 1;
  }

  setErrorText("ok");
  return true;
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
