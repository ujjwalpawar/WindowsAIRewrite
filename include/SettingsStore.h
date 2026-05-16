#pragma once

#include <filesystem>
#include <string>

struct AppSettings {
  std::wstring hotkey = L"Ctrl+Alt+Shift+R";
  std::wstring model = L"gpt-4.1-mini";
  int requestTimeoutSeconds = 45;
  bool restoreClipboard = true;
  bool launchAtStartup = false;
};

enum class SettingsStoreStatus {
  kSuccess,
  kIoError,
  kParseError,
  kInvalidData,
};

struct SettingsLoadResult {
  SettingsStoreStatus status = SettingsStoreStatus::kSuccess;
  AppSettings settings{};
  bool fileCreated = false;
};

struct SettingsOperationResult {
  SettingsStoreStatus status = SettingsStoreStatus::kSuccess;
};

class SettingsStore {
 public:
  SettingsStore();
  explicit SettingsStore(std::filesystem::path settingsPath);

  [[nodiscard]] SettingsLoadResult Load() const;
  [[nodiscard]] SettingsOperationResult Save(const AppSettings& settings) const;

  [[nodiscard]] const std::filesystem::path& settings_path() const;
  [[nodiscard]] static AppSettings DefaultSettings();

 private:
  std::filesystem::path settingsPath_;
};
