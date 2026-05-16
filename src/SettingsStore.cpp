#include "SettingsStore.h"

#include <windows.h>

#include <fstream>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace {

constexpr wchar_t kAppDirectoryName[] = L"WindowsAIRewrite";
constexpr wchar_t kSettingsFileName[] = L"settings.json";

std::wstring GetEnvironmentVariableValue(const wchar_t* variableName) {
  const DWORD size = GetEnvironmentVariableW(variableName, nullptr, 0);
  if (size == 0) {
    return L"";
  }

  std::wstring value(size - 1, L'\0');
  GetEnvironmentVariableW(variableName, value.data(), size);
  return value;
}

std::filesystem::path GetDefaultSettingsPath() {
  const std::wstring localAppData = GetEnvironmentVariableValue(L"LOCALAPPDATA");
  if (localAppData.empty()) {
    return {};
  }

  return std::filesystem::path(localAppData) / kAppDirectoryName / kSettingsFileName;
}

std::string Utf8FromWide(const std::wstring& value) {
  if (value.empty()) {
    return {};
  }

  const int size = WideCharToMultiByte(
    CP_UTF8,
    0,
    value.c_str(),
    static_cast<int>(value.size()),
    nullptr,
    0,
    nullptr,
    nullptr);
  if (size <= 0) {
    return {};
  }

  std::string result(size, '\0');
  WideCharToMultiByte(
    CP_UTF8,
    0,
    value.c_str(),
    static_cast<int>(value.size()),
    result.data(),
    size,
    nullptr,
    nullptr);
  return result;
}

std::wstring WideFromUtf8(const std::string& value) {
  if (value.empty()) {
    return {};
  }

  const int size = MultiByteToWideChar(
    CP_UTF8,
    MB_ERR_INVALID_CHARS,
    value.data(),
    static_cast<int>(value.size()),
    nullptr,
    0);
  if (size <= 0) {
    return {};
  }

  std::wstring result(size, L'\0');
  MultiByteToWideChar(
    CP_UTF8,
    MB_ERR_INVALID_CHARS,
    value.data(),
    static_cast<int>(value.size()),
    result.data(),
    size);
  return result;
}

bool TryReadWideString(
  const nlohmann::json& json,
  const char* key,
  std::wstring* destination) {
  if (!json.contains(key)) {
    return true;
  }

  if (!json.at(key).is_string()) {
    return false;
  }

  const std::string utf8Value = json.at(key).get<std::string>();
  const std::wstring wideValue = WideFromUtf8(utf8Value);
  if (!utf8Value.empty() && wideValue.empty()) {
    return false;
  }

  *destination = wideValue;
  return true;
}

nlohmann::json SerializeSettings(const AppSettings& settings) {
  return nlohmann::json{
    {"hotkey", Utf8FromWide(settings.hotkey)},
    {"model", Utf8FromWide(settings.model)},
    {"requestTimeoutSeconds", settings.requestTimeoutSeconds},
    {"restoreClipboard", settings.restoreClipboard},
    {"launchAtStartup", settings.launchAtStartup},
  };
}

bool ParseSettings(const nlohmann::json& json, AppSettings* settings) {
  if (settings == nullptr || !json.is_object()) {
    return false;
  }

  AppSettings parsed = SettingsStore::DefaultSettings();

  if (!TryReadWideString(json, "hotkey", &parsed.hotkey)) {
    return false;
  }

  if (!TryReadWideString(json, "model", &parsed.model)) {
    return false;
  }

  if (json.contains("requestTimeoutSeconds")) {
    if (!json.at("requestTimeoutSeconds").is_number_integer()) {
      return false;
    }
    parsed.requestTimeoutSeconds = json.at("requestTimeoutSeconds").get<int>();
  }

  if (json.contains("restoreClipboard")) {
    if (!json.at("restoreClipboard").is_boolean()) {
      return false;
    }
    parsed.restoreClipboard = json.at("restoreClipboard").get<bool>();
  }

  if (json.contains("launchAtStartup")) {
    if (!json.at("launchAtStartup").is_boolean()) {
      return false;
    }
    parsed.launchAtStartup = json.at("launchAtStartup").get<bool>();
  }

  *settings = parsed;
  return true;
}

}  // namespace

SettingsStore::SettingsStore() : settingsPath_(GetDefaultSettingsPath()) {}

SettingsStore::SettingsStore(std::filesystem::path settingsPath)
  : settingsPath_(std::move(settingsPath)) {}

SettingsLoadResult SettingsStore::Load() const {
  const AppSettings defaults = DefaultSettings();

  if (settingsPath_.empty()) {
    return {
      .status = SettingsStoreStatus::kIoError,
      .settings = defaults,
      .fileCreated = false,
    };
  }

  if (!std::filesystem::exists(settingsPath_)) {
    const SettingsOperationResult saveResult = Save(defaults);
    return {
      .status = saveResult.status,
      .settings = defaults,
      .fileCreated = saveResult.status == SettingsStoreStatus::kSuccess,
    };
  }

  std::ifstream input(settingsPath_, std::ios::binary);
  if (!input.is_open()) {
    return {
      .status = SettingsStoreStatus::kIoError,
      .settings = defaults,
      .fileCreated = false,
    };
  }

  nlohmann::json json;
  try {
    input >> json;
  } catch (const nlohmann::json::exception&) {
    return {
      .status = SettingsStoreStatus::kParseError,
      .settings = defaults,
      .fileCreated = false,
    };
  }

  AppSettings parsedSettings = defaults;
  if (!ParseSettings(json, &parsedSettings)) {
    return {
      .status = SettingsStoreStatus::kInvalidData,
      .settings = defaults,
      .fileCreated = false,
    };
  }

  return {
    .status = SettingsStoreStatus::kSuccess,
    .settings = parsedSettings,
    .fileCreated = false,
  };
}

SettingsOperationResult SettingsStore::Save(const AppSettings& settings) const {
  if (settingsPath_.empty()) {
    return {.status = SettingsStoreStatus::kIoError};
  }

  std::error_code errorCode;
  std::filesystem::create_directories(settingsPath_.parent_path(), errorCode);
  if (errorCode) {
    return {.status = SettingsStoreStatus::kIoError};
  }

  std::ofstream output(settingsPath_, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return {.status = SettingsStoreStatus::kIoError};
  }

  output << SerializeSettings(settings).dump(2) << '\n';
  if (!output.good()) {
    return {.status = SettingsStoreStatus::kIoError};
  }

  return {.status = SettingsStoreStatus::kSuccess};
}

const std::filesystem::path& SettingsStore::settings_path() const {
  return settingsPath_;
}

AppSettings SettingsStore::DefaultSettings() {
  return {};
}
