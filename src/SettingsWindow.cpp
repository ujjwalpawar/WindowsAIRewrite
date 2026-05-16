#include "SettingsWindow.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <winhttp.h>

#include <nlohmann/json.hpp>

#include "UiControlIds.h"
#include "UserMessages.h"
#include "Win32WindowActivation.h"
#include "Win32UiTheme.h"

namespace {

constexpr wchar_t kWindowClassName[] = L"WindowsAIRewriteSettingsWindow";
constexpr wchar_t kFirstRunTitle[] = L"Set up Windows AI Rewrite";
constexpr wchar_t kSettingsTitle[] = L"Windows AI Rewrite settings";
constexpr int kFirstRunWindowWidth = 620;
constexpr int kFirstRunWindowHeight = 360;
constexpr int kSettingsWindowWidth = 660;
constexpr int kSettingsWindowHeight = 540;
constexpr int kContentLeft = Win32UiTheme::Space::kXl;
constexpr int kContentTop = Win32UiTheme::Space::kLg;
constexpr int kFirstRunContentWidth = kFirstRunWindowWidth - (Win32UiTheme::Space::kXl * 2);
constexpr int kSettingsContentWidth = kSettingsWindowWidth - (Win32UiTheme::Space::kXl * 2);
constexpr int kHeadingHeight = 30;
constexpr int kDisclosureTop = kContentTop + kHeadingHeight + Win32UiTheme::Space::kMd;
constexpr int kDisclosureHeight = 56;
constexpr int kSettingsStatusTop = kContentTop + kHeadingHeight + Win32UiTheme::Space::kLg;
constexpr int kSettingsFirstRowTop = kSettingsStatusTop + Win32UiTheme::Metric::kLabelHeight +
                                     Win32UiTheme::Space::kLg;
constexpr int kLabelWidth = 168;
constexpr int kInputLeft = kContentLeft + kLabelWidth + Win32UiTheme::Space::kMd;
constexpr int kInputWidth = kSettingsContentWidth - kLabelWidth - Win32UiTheme::Space::kMd;
constexpr int kRowGap = Win32UiTheme::Metric::kInputHeight + Win32UiTheme::Space::kLg;
constexpr int kButtonGap = Win32UiTheme::Space::kSm;
constexpr int kTimeoutInputWidth = 96;
constexpr int kTimeoutUnitOffset = kTimeoutInputWidth + Win32UiTheme::Space::kSm;
constexpr int kHotkeyInputWidth = kInputWidth - Win32UiTheme::Metric::kButtonWidth - kButtonGap;
constexpr int kHotkeyRowGap = kRowGap + Win32UiTheme::Metric::kLabelHeight + Win32UiTheme::Space::kXs;
constexpr int kModelComboWidth = kInputWidth - Win32UiTheme::Metric::kButtonWidth - kButtonGap;
constexpr int kModelRowGap = kRowGap + Win32UiTheme::Metric::kLabelHeight + Win32UiTheme::Space::kXs;
constexpr wchar_t kDefaultModelChoice[] = L"gpt-5.1-mini";
constexpr const wchar_t* kModelChoices[] = {
  L"gpt-5.5",
  L"gpt-5.5-pro",
  L"gpt-5.4",
  L"gpt-5.4-mini",
  L"gpt-5.3-chat-latest",
  L"gpt-5.2",
  L"gpt-5.1",
  L"gpt-5",
  kDefaultModelChoice,
  L"gpt-5-mini",
  L"gpt-4.1",
  L"gpt-4.1-mini",
  L"gpt-4o",
  L"gpt-4o-mini",
  L"o4-mini",
  L"o3",
};
constexpr wchar_t kAppDirectoryName[] = L"WindowsAIRewrite";
constexpr wchar_t kModelsFileName[] = L"models.json";
constexpr wchar_t kOpenAIHost[] = L"api.openai.com";
constexpr wchar_t kOpenAIModelsPath[] = L"/v1/models";
constexpr wchar_t kUserAgent[] = L"WindowsAIRewrite/0.1";
constexpr DWORD kHttpsPort = INTERNET_DEFAULT_HTTPS_PORT;
constexpr DWORD kModelFetchTimeoutMs = 30000;
constexpr DWORD kReadBufferSize = 4096;

enum class ModelFetchStatus {
  kSuccess,
  kRequestFailed,
  kInvalidResponse,
};

struct ModelFetchResult {
  ModelFetchStatus status = ModelFetchStatus::kRequestFailed;
  std::vector<std::wstring> modelIds{};
  DWORD detail = ERROR_SUCCESS;
};

class WinHttpHandle {
 public:
  WinHttpHandle() = default;
  WinHttpHandle(const WinHttpHandle&) = delete;
  WinHttpHandle& operator=(const WinHttpHandle&) = delete;

  WinHttpHandle(WinHttpHandle&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
  }

  WinHttpHandle& operator=(WinHttpHandle&& other) noexcept {
    if (this != &other) {
      reset();
      handle_ = other.handle_;
      other.handle_ = nullptr;
    }

    return *this;
  }

  ~WinHttpHandle() {
    reset();
  }

  void reset(HINTERNET handle = nullptr) {
    if (handle_ != nullptr) {
      WinHttpCloseHandle(handle_);
    }
    handle_ = handle;
  }

  [[nodiscard]] HINTERNET get() const {
    return handle_;
  }

  [[nodiscard]] explicit operator bool() const {
    return handle_ != nullptr;
  }

 private:
  HINTERNET handle_ = nullptr;
};

int ClampToRange(int value, int lowerBound, int upperBound) {
  if (upperBound < lowerBound) {
    return lowerBound;
  }

  if (value < lowerBound) {
    return lowerBound;
  }

  if (value > upperBound) {
    return upperBound;
  }

  return value;
}

bool IsUsableOwnerWindow(HWND ownerWindow) {
  if (ownerWindow == nullptr || !IsWindowVisible(ownerWindow) || IsIconic(ownerWindow)) {
    return false;
  }

  RECT ownerRect{};
  if (GetWindowRect(ownerWindow, &ownerRect) != TRUE) {
    return false;
  }

  return (ownerRect.right - ownerRect.left) > 1 && (ownerRect.bottom - ownerRect.top) > 1;
}

bool TryGetWorkAreaForWindow(HWND referenceWindow, RECT* workArea) {
  if (referenceWindow == nullptr || workArea == nullptr) {
    return false;
  }

  const HMONITOR monitor = MonitorFromWindow(referenceWindow, MONITOR_DEFAULTTONEAREST);
  if (monitor != nullptr) {
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (GetMonitorInfoW(monitor, &monitorInfo) == TRUE) {
      *workArea = monitorInfo.rcWork;
      return true;
    }
  }

  return SystemParametersInfoW(SPI_GETWORKAREA, 0, workArea, 0) == TRUE;
}

std::wstring GetEnvironmentVariableValue(const wchar_t* variableName) {
  const DWORD size = GetEnvironmentVariableW(variableName, nullptr, 0);
  if (size == 0) {
    return L"";
  }

  std::wstring value(size - 1, L'\0');
  GetEnvironmentVariableW(variableName, value.data(), size);
  return value;
}

std::filesystem::path GetModelsPath() {
  const std::wstring localAppData = GetEnvironmentVariableValue(L"LOCALAPPDATA");
  if (localAppData.empty()) {
    return {};
  }

  return std::filesystem::path(localAppData) / kAppDirectoryName / kModelsFileName;
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
  if (WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        result.data(),
        size,
        nullptr,
        nullptr) <= 0) {
    return {};
  }

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
  if (MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size) <= 0) {
    return {};
  }

  return result;
}

template <typename Character>
void ClearString(std::basic_string<Character>* value) {
  if (value == nullptr) {
    return;
  }

  std::fill(value->begin(), value->end(), Character{});
  value->clear();
}

void CenterWindowWithinWorkArea(HWND windowHandle, const RECT& workArea) {
  RECT windowRect{};
  GetWindowRect(windowHandle, &windowRect);

  const int windowWidth = windowRect.right - windowRect.left;
  const int windowHeight = windowRect.bottom - windowRect.top;
  const int workWidth = static_cast<int>(workArea.right - workArea.left);
  const int workHeight = static_cast<int>(workArea.bottom - workArea.top);

  int x = workArea.left + ((workWidth - windowWidth) / 2);
  int y = workArea.top + ((workHeight - windowHeight) / 2);

  x = ClampToRange(x, static_cast<int>(workArea.left), static_cast<int>(workArea.right - windowWidth));
  y = ClampToRange(y, static_cast<int>(workArea.top), static_cast<int>(workArea.bottom - windowHeight));

  SetWindowPos(windowHandle, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

HFONT CreatePointFont(int pointSize, int weight, const wchar_t* faceName) {
  HDC screen = GetDC(nullptr);
  const int pixels = screen == nullptr
                       ? pointSize
                       : MulDiv(pointSize, GetDeviceCaps(screen, LOGPIXELSY), 72);
  if (screen != nullptr) {
    ReleaseDC(nullptr, screen);
  }

  return CreateFontW(
    -pixels,
    0,
    0,
    0,
    weight,
    FALSE,
    FALSE,
    FALSE,
    DEFAULT_CHARSET,
    OUT_DEFAULT_PRECIS,
    CLIP_DEFAULT_PRECIS,
    CLEARTYPE_QUALITY,
    DEFAULT_PITCH | FF_DONTCARE,
    faceName);
}

HMENU ControlHandle(int controlId) {
  return reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId));
}

void SetControlFont(HWND control, HFONT font) {
  if (control != nullptr && font != nullptr) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  }
}

HWND CreateStaticText(
  HWND parent,
  HINSTANCE instanceHandle,
  const wchar_t* text,
  int x,
  int y,
  int width,
  int height,
  HFONT font,
  int controlId = 0) {
  HWND control = CreateWindowExW(
    0,
    L"STATIC",
    text,
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    x,
    y,
    width,
    height,
    parent,
    controlId == 0 ? nullptr : ControlHandle(controlId),
    instanceHandle,
    nullptr);
  SetControlFont(control, font);
  return control;
}

HWND CreateInput(
  HWND parent,
  HINSTANCE instanceHandle,
  const wchar_t* value,
  int controlId,
  int x,
  int y,
  int width,
  DWORD style,
  HFONT font) {
  HWND control = CreateWindowExW(
    WS_EX_CLIENTEDGE,
    L"EDIT",
    value,
    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_LEFT | ES_AUTOHSCROLL | style,
    x,
    y,
    width,
    Win32UiTheme::Metric::kInputHeight,
    parent,
    ControlHandle(controlId),
    instanceHandle,
    nullptr);
  SetControlFont(control, font);
  return control;
}

HWND CreateComboBox(
  HWND parent,
  HINSTANCE instanceHandle,
  int controlId,
  int x,
  int y,
  int width,
  HFONT font) {
  HWND control = CreateWindowExW(
    WS_EX_CLIENTEDGE,
    L"COMBOBOX",
    nullptr,
    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN | CBS_HASSTRINGS,
    x,
    y,
    width,
    Win32UiTheme::Metric::kInputHeight * 6,
    parent,
    ControlHandle(controlId),
    instanceHandle,
    nullptr);
  SetControlFont(control, font);
  return control;
}

HWND CreateButton(
  HWND parent,
  HINSTANCE instanceHandle,
  const wchar_t* text,
  int controlId,
  int x,
  int y,
  int width,
  HFONT font) {
  HWND control = CreateWindowExW(
    0,
    L"BUTTON",
    text,
    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
    x,
    y,
    width,
    Win32UiTheme::Metric::kButtonHeight,
    parent,
    ControlHandle(controlId),
    instanceHandle,
    nullptr);
  SetControlFont(control, font);
  return control;
}

HWND CreateCheckbox(
  HWND parent,
  HINSTANCE instanceHandle,
  const wchar_t* text,
  int controlId,
  int x,
  int y,
  int width,
  bool checked,
  HFONT font) {
  HWND control = CreateWindowExW(
    0,
    L"BUTTON",
    text,
    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
    x,
    y,
    width,
    Win32UiTheme::Metric::kInputHeight,
    parent,
    ControlHandle(controlId),
    instanceHandle,
    nullptr);
  SetControlFont(control, font);
  SendMessageW(control, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
  return control;
}

std::wstring Trim(const std::wstring& value) {
  size_t first = 0;
  while (first < value.size() && iswspace(value[first]) != 0) {
    ++first;
  }

  size_t last = value.size();
  while (last > first && iswspace(value[last - 1]) != 0) {
    --last;
  }

  return value.substr(first, last - first);
}

bool AppendUniqueModel(std::vector<std::wstring>* modelIds, const std::wstring& modelId) {
  if (modelIds == nullptr || modelId.empty()) {
    return false;
  }

  if (std::find(modelIds->begin(), modelIds->end(), modelId) != modelIds->end()) {
    return false;
  }

  modelIds->push_back(modelId);
  return true;
}

bool IsSoraModelId(const std::wstring& modelId);
bool IsNewerModelId(const std::wstring& left, const std::wstring& right);

std::vector<std::wstring> NormalizeModelIds(std::vector<std::wstring> modelIds) {
  modelIds.erase(
    std::remove_if(
      modelIds.begin(),
      modelIds.end(),
      [](const std::wstring& modelId) { return modelId.empty() || IsSoraModelId(modelId); }),
    modelIds.end());
  std::sort(modelIds.begin(), modelIds.end(), IsNewerModelId);
  modelIds.erase(std::unique(modelIds.begin(), modelIds.end()), modelIds.end());
  return modelIds;
}

bool StartsWith(std::wstring_view value, std::wstring_view prefix) {
  return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool IsDigit(wchar_t character) {
  return character >= L'0' && character <= L'9';
}

std::wstring LowerAscii(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
    return character >= L'A' && character <= L'Z' ? static_cast<wchar_t>(character - L'A' + L'a')
                                                  : character;
  });
  return value;
}

bool IsSoraModelId(const std::wstring& modelId) {
  return LowerAscii(modelId).find(L"sora") != std::wstring::npos;
}

std::vector<int> ExtractVersionParts(std::wstring_view modelId) {
  std::vector<int> versionParts;
  for (size_t index = 0; index < modelId.size();) {
    if (!IsDigit(modelId[index])) {
      ++index;
      continue;
    }

    int value = 0;
    while (index < modelId.size() && IsDigit(modelId[index])) {
      value = (value * 10) + static_cast<int>(modelId[index] - L'0');
      ++index;
    }
    versionParts.push_back(value);
  }
  return versionParts;
}

int ModelFamilyRank(std::wstring_view modelId) {
  if (StartsWith(modelId, L"gpt-")) {
    return 0;
  }

  if (modelId.size() >= 2 && modelId[0] == L'o' && IsDigit(modelId[1])) {
    return 1;
  }

  return 2;
}

int PreferredModelRank(std::wstring_view modelId) {
  constexpr std::wstring_view kPreferredModels[] = {
    L"gpt-5.5",
    L"gpt-5.5-pro",
    L"gpt-5.4",
    L"gpt-5.4-mini",
    L"gpt-5.3-chat-latest",
    L"gpt-5.2",
    L"gpt-5.1",
    L"gpt-5",
  };

  for (size_t index = 0; index < std::size(kPreferredModels); ++index) {
    if (modelId == kPreferredModels[index]) {
      return static_cast<int>(index);
    }
  }

  return static_cast<int>(std::size(kPreferredModels));
}

int ModelVariantRank(std::wstring_view modelId) {
  if (modelId.find(L"-pro") != std::wstring_view::npos) {
    return 1;
  }
  if (modelId.find(L"-chat-latest") != std::wstring_view::npos) {
    return 2;
  }
  if (modelId.find(L"-latest") != std::wstring_view::npos) {
    return 3;
  }
  if (modelId.find(L"-mini") != std::wstring_view::npos) {
    return 4;
  }
  return 0;
}

bool IsNewerModelId(const std::wstring& left, const std::wstring& right) {
  const std::wstring leftModelId = LowerAscii(left);
  const std::wstring rightModelId = LowerAscii(right);

  const int leftFamilyRank = ModelFamilyRank(leftModelId);
  const int rightFamilyRank = ModelFamilyRank(rightModelId);
  if (leftFamilyRank != rightFamilyRank) {
    return leftFamilyRank < rightFamilyRank;
  }

  const std::vector<int> leftVersionParts = ExtractVersionParts(leftModelId);
  const std::vector<int> rightVersionParts = ExtractVersionParts(rightModelId);
  const size_t sharedPartCount = std::min(leftVersionParts.size(), rightVersionParts.size());
  for (size_t index = 0; index < sharedPartCount; ++index) {
    if (leftVersionParts[index] != rightVersionParts[index]) {
      return leftVersionParts[index] > rightVersionParts[index];
    }
  }
  if (leftVersionParts.size() != rightVersionParts.size()) {
    return leftVersionParts.size() > rightVersionParts.size();
  }

  const int leftPreferredRank = PreferredModelRank(leftModelId);
  const int rightPreferredRank = PreferredModelRank(rightModelId);
  if (leftPreferredRank != rightPreferredRank) {
    return leftPreferredRank < rightPreferredRank;
  }

  const int leftVariantRank = ModelVariantRank(leftModelId);
  const int rightVariantRank = ModelVariantRank(rightModelId);
  if (leftVariantRank != rightVariantRank) {
    return leftVariantRank < rightVariantRank;
  }

  return leftModelId < rightModelId;
}

std::string LowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

bool ContainsAny(std::string_view value, const std::vector<std::string_view>& needles) {
  return std::any_of(needles.begin(), needles.end(), [value](std::string_view needle) {
    return value.find(needle) != std::string_view::npos;
  });
}

bool IsUsefulModelId(const std::string& modelId) {
  if (modelId.empty()) {
    return false;
  }

  const std::string lowerModelId = LowerAscii(modelId);
  return !ContainsAny(
    lowerModelId,
    {
      "audio",
      "dall-e",
      "embedding",
      "image",
      "moderation",
      "realtime",
      "sora",
      "speech",
      "transcribe",
      "transcription",
      "tts",
      "whisper",
    });
}

std::vector<std::wstring> BuildModelChoices(
  const std::vector<std::wstring>& dynamicModelIds,
  const std::wstring& savedModel) {
  std::vector<std::wstring> choices;
  for (const wchar_t* modelChoice : kModelChoices) {
    AppendUniqueModel(&choices, modelChoice);
  }

  for (const std::wstring& modelId : NormalizeModelIds(dynamicModelIds)) {
    if (IsSoraModelId(modelId)) {
      continue;
    }
    AppendUniqueModel(&choices, modelId);
  }

  const std::wstring modelToSelect = savedModel.empty() || IsSoraModelId(savedModel) ? kDefaultModelChoice : savedModel;
  AppendUniqueModel(&choices, modelToSelect);
  return choices;
}

void PopulateModelComboBox(
  HWND comboBox,
  const std::wstring& savedModel,
  const std::vector<std::wstring>& dynamicModelIds) {
  if (comboBox == nullptr) {
    return;
  }

  SendMessageW(comboBox, CB_RESETCONTENT, 0, 0);
  const std::vector<std::wstring> choices = BuildModelChoices(dynamicModelIds, savedModel);
  for (const std::wstring& modelChoice : choices) {
    SendMessageW(comboBox, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(modelChoice.c_str()));
  }

  const std::wstring modelToSelect = savedModel.empty() || IsSoraModelId(savedModel) ? kDefaultModelChoice : savedModel;
  SendMessageW(comboBox, CB_SELECTSTRING, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(modelToSelect.c_str()));
}

std::vector<std::wstring> LoadCachedModelIds() {
  const std::filesystem::path modelsPath = GetModelsPath();
  if (modelsPath.empty()) {
    return {};
  }

  std::error_code errorCode;
  if (!std::filesystem::exists(modelsPath, errorCode) || errorCode) {
    return {};
  }

  std::ifstream input(modelsPath, std::ios::binary);
  if (!input.is_open()) {
    return {};
  }

  nlohmann::json json;
  try {
    input >> json;
  } catch (const nlohmann::json::exception&) {
    return {};
  }

  const nlohmann::json* modelArray = nullptr;
  if (json.is_array()) {
    modelArray = &json;
  } else if (json.is_object() && json.contains("models") && json.at("models").is_array()) {
    modelArray = &json.at("models");
  }

  if (modelArray == nullptr) {
    return {};
  }

  std::vector<std::wstring> modelIds;
  for (const nlohmann::json& modelIdJson : *modelArray) {
    if (!modelIdJson.is_string()) {
      continue;
    }

    const std::string modelIdUtf8 = modelIdJson.get<std::string>();
    if (!IsUsefulModelId(modelIdUtf8)) {
      continue;
    }

    AppendUniqueModel(&modelIds, WideFromUtf8(modelIdUtf8));
  }

  return NormalizeModelIds(std::move(modelIds));
}

bool SaveCachedModelIds(std::vector<std::wstring> modelIds) {
  const std::filesystem::path modelsPath = GetModelsPath();
  if (modelsPath.empty()) {
    return false;
  }

  modelIds = NormalizeModelIds(std::move(modelIds));
  std::error_code errorCode;
  std::filesystem::create_directories(modelsPath.parent_path(), errorCode);
  if (errorCode) {
    return false;
  }

  nlohmann::json json;
  json["models"] = nlohmann::json::array();
  for (const std::wstring& modelId : modelIds) {
    const std::string modelIdUtf8 = Utf8FromWide(modelId);
    if (!modelIdUtf8.empty()) {
      json["models"].push_back(modelIdUtf8);
    }
  }

  std::ofstream output(modelsPath, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return false;
  }

  output << json.dump(2) << '\n';
  return output.good();
}

bool SetRequestTimeouts(HINTERNET handle, DWORD timeoutMs) {
  const int timeout = static_cast<int>(timeoutMs);
  return WinHttpSetTimeouts(handle, timeout, timeout, timeout, timeout) == TRUE;
}

bool ReadResponseBody(HINTERNET requestHandle, std::string* responseBodyUtf8) {
  if (responseBodyUtf8 == nullptr) {
    return false;
  }

  std::vector<char> buffer(kReadBufferSize);
  DWORD bytesRead = 0;
  do {
    bytesRead = 0;
    if (WinHttpReadData(requestHandle, buffer.data(), kReadBufferSize, &bytesRead) != TRUE) {
      return false;
    }

    if (bytesRead > 0) {
      responseBodyUtf8->append(buffer.data(), bytesRead);
    }
  } while (bytesRead > 0);

  return true;
}

bool QueryStatusCode(HINTERNET requestHandle, DWORD* httpStatus) {
  if (httpStatus == nullptr) {
    return false;
  }

  DWORD statusCode = 0;
  DWORD statusCodeSize = sizeof(statusCode);
  if (WinHttpQueryHeaders(
        requestHandle,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusCodeSize,
        WINHTTP_NO_HEADER_INDEX) != TRUE) {
    return false;
  }

  *httpStatus = statusCode;
  return true;
}

ModelFetchResult ParseModelsResponseBody(const std::string& responseBodyUtf8, DWORD httpStatus) {
  if (httpStatus < 200 || httpStatus >= 300) {
    return {.status = ModelFetchStatus::kRequestFailed, .modelIds = {}, .detail = httpStatus};
  }

  nlohmann::json responseJson;
  try {
    responseJson = nlohmann::json::parse(responseBodyUtf8);
  } catch (const nlohmann::json::exception&) {
    return {.status = ModelFetchStatus::kInvalidResponse, .modelIds = {}, .detail = httpStatus};
  }

  if (!responseJson.is_object() || !responseJson.contains("data") || !responseJson.at("data").is_array()) {
    return {.status = ModelFetchStatus::kInvalidResponse, .modelIds = {}, .detail = httpStatus};
  }

  std::vector<std::wstring> modelIds;
  for (const nlohmann::json& modelJson : responseJson.at("data")) {
    if (!modelJson.is_object() || !modelJson.contains("id") || !modelJson.at("id").is_string()) {
      continue;
    }

    const std::string modelIdUtf8 = modelJson.at("id").get<std::string>();
    if (!IsUsefulModelId(modelIdUtf8)) {
      continue;
    }

    AppendUniqueModel(&modelIds, WideFromUtf8(modelIdUtf8));
  }

  modelIds = NormalizeModelIds(std::move(modelIds));
  if (modelIds.empty()) {
    return {.status = ModelFetchStatus::kInvalidResponse, .modelIds = {}, .detail = httpStatus};
  }

  return {.status = ModelFetchStatus::kSuccess, .modelIds = std::move(modelIds), .detail = httpStatus};
}

ModelFetchResult FetchOpenAIModelIds(const std::wstring& apiKey) {
  WinHttpHandle session;
  session.reset(WinHttpOpen(
    kUserAgent,
    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
    WINHTTP_NO_PROXY_NAME,
    WINHTTP_NO_PROXY_BYPASS,
    0));
  if (!session) {
    return {.status = ModelFetchStatus::kRequestFailed, .modelIds = {}, .detail = GetLastError()};
  }

  if (!SetRequestTimeouts(session.get(), kModelFetchTimeoutMs)) {
    return {.status = ModelFetchStatus::kRequestFailed, .modelIds = {}, .detail = GetLastError()};
  }

  WinHttpHandle connection;
  connection.reset(WinHttpConnect(session.get(), kOpenAIHost, kHttpsPort, 0));
  if (!connection) {
    return {.status = ModelFetchStatus::kRequestFailed, .modelIds = {}, .detail = GetLastError()};
  }

  WinHttpHandle request;
  request.reset(WinHttpOpenRequest(
    connection.get(),
    L"GET",
    kOpenAIModelsPath,
    nullptr,
    WINHTTP_NO_REFERER,
    WINHTTP_DEFAULT_ACCEPT_TYPES,
    WINHTTP_FLAG_SECURE));
  if (!request) {
    return {.status = ModelFetchStatus::kRequestFailed, .modelIds = {}, .detail = GetLastError()};
  }

  if (!SetRequestTimeouts(request.get(), kModelFetchTimeoutMs)) {
    return {.status = ModelFetchStatus::kRequestFailed, .modelIds = {}, .detail = GetLastError()};
  }

  std::wstring headers = L"Author" L"ization: Bearer " + apiKey + L"\r\n";
  if (WinHttpSendRequest(
        request.get(),
        headers.c_str(),
        static_cast<DWORD>(headers.size()),
        WINHTTP_NO_REQUEST_DATA,
        0,
        0,
        0) != TRUE) {
    const DWORD errorCode = GetLastError();
    ClearString(&headers);
    return {.status = ModelFetchStatus::kRequestFailed, .modelIds = {}, .detail = errorCode};
  }
  ClearString(&headers);

  if (WinHttpReceiveResponse(request.get(), nullptr) != TRUE) {
    return {.status = ModelFetchStatus::kRequestFailed, .modelIds = {}, .detail = GetLastError()};
  }

  DWORD httpStatus = 0;
  if (!QueryStatusCode(request.get(), &httpStatus)) {
    return {.status = ModelFetchStatus::kRequestFailed, .modelIds = {}, .detail = GetLastError()};
  }

  std::string responseBodyUtf8;
  if (!ReadResponseBody(request.get(), &responseBodyUtf8)) {
    return {.status = ModelFetchStatus::kRequestFailed, .modelIds = {}, .detail = GetLastError()};
  }

  return ParseModelsResponseBody(responseBodyUtf8, httpStatus);
}

bool IsSuccessOrMissing(CredentialStoreStatus status) {
  return status == CredentialStoreStatus::kSuccess || status == CredentialStoreStatus::kNotFound;
}

bool IsHotkeyCaptureMessage(UINT message) {
  return message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
}

bool IsMessageForWindow(HWND rootWindow, HWND messageWindow) {
  return rootWindow != nullptr &&
         (messageWindow == rootWindow || IsChild(rootWindow, messageWindow) == TRUE);
}

bool IsKeyDown(int virtualKey) {
  return (GetKeyState(virtualKey) & 0x8000) != 0;
}

bool IsModifierVirtualKey(UINT virtualKey) {
  switch (virtualKey) {
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
      return true;
    default:
      return false;
  }
}

bool TryGetHotkeyKeyName(UINT virtualKey, std::wstring* keyName) {
  if (keyName == nullptr) {
    return false;
  }

  if ((virtualKey >= L'A' && virtualKey <= L'Z') ||
      (virtualKey >= L'0' && virtualKey <= L'9')) {
    *keyName = std::wstring(1, static_cast<wchar_t>(virtualKey));
    return true;
  }

  if (virtualKey >= VK_F1 && virtualKey <= VK_F24) {
    *keyName = L"F" + std::to_wstring(virtualKey - VK_F1 + 1);
    return true;
  }

  switch (virtualKey) {
    case VK_SPACE:
      *keyName = L"Space";
      return true;
    case VK_TAB:
      *keyName = L"Tab";
      return true;
    case VK_RETURN:
      *keyName = L"Enter";
      return true;
    default:
      return false;
  }
}

std::wstring BuildCapturedHotkeyText(UINT virtualKey) {
  std::wstring keyName;
  if (!TryGetHotkeyKeyName(virtualKey, &keyName)) {
    return {};
  }

  std::wstring hotkeyText;
  if (IsKeyDown(VK_CONTROL)) {
    hotkeyText += L"Ctrl";
  }
  if (IsKeyDown(VK_MENU)) {
    if (!hotkeyText.empty()) {
      hotkeyText += L"+";
    }
    hotkeyText += L"Alt";
  }
  if (IsKeyDown(VK_SHIFT)) {
    if (!hotkeyText.empty()) {
      hotkeyText += L"+";
    }
    hotkeyText += L"Shift";
  }

  if (hotkeyText.empty()) {
    return {};
  }

  hotkeyText += L"+";
  hotkeyText += keyName;
  return hotkeyText;
}

}  // namespace

SettingsWindow::SettingsWindow(HINSTANCE instanceHandle) : instanceHandle_(instanceHandle) {}

SettingsWindow::SettingsWindow(
  HINSTANCE instanceHandle,
  SettingsStore settingsStore,
  CredentialStore credentialStore)
  : instanceHandle_(instanceHandle),
    settingsStore_(std::move(settingsStore)),
    credentialStore_(std::move(credentialStore)) {}

SettingsWindow::~SettingsWindow() {
  DestroyThemeObjects();
}

bool SettingsWindow::ShowFirstRunSetup(HWND ownerWindow) {
  settings_ = SettingsStore::DefaultSettings();
  result_ = {
    .action = SettingsWindowAction::kCancel,
    .settings = settings_,
    .apiKeyConfigured = credentialStore_.Exists(),
  };
  return ShowModal(ownerWindow, Mode::kFirstRun) && result_.action == SettingsWindowAction::kSaved;
}

SettingsWindowResult SettingsWindow::ShowSettings(HWND ownerWindow) {
  const SettingsLoadResult loadResult = settingsStore_.Load();
  settings_ = loadResult.settings;
  apiKeyConfigured_ = credentialStore_.Exists();
  result_ = {
    .action = SettingsWindowAction::kCancel,
    .settings = settings_,
    .apiKeyConfigured = apiKeyConfigured_,
  };
  static_cast<void>(ShowModal(ownerWindow, Mode::kSettings));
  return result_;
}

bool SettingsWindow::ShowModal(HWND ownerWindow, Mode mode) {
  ownerWindow_ = ownerWindow;
  mode_ = mode;
  apiKeyConfigured_ = credentialStore_.Exists();
  capturingHotkey_ = false;
  apiKeyEdit_ = nullptr;
  apiKeyStatus_ = nullptr;
  hotkeyEdit_ = nullptr;
  hotkeyStatus_ = nullptr;
  modelCombo_ = nullptr;
  updateModelsButton_ = nullptr;
  modelStatus_ = nullptr;
  timeoutEdit_ = nullptr;
  restoreClipboardCheck_ = nullptr;
  launchAtStartupCheck_ = nullptr;
  modelStatusIsError_ = false;

  if (!EnsureWindowClass() || !CreateThemeObjects()) {
    return false;
  }

  const int windowWidth = mode_ == Mode::kFirstRun ? kFirstRunWindowWidth : kSettingsWindowWidth;
  const int windowHeight = mode_ == Mode::kFirstRun ? kFirstRunWindowHeight : kSettingsWindowHeight;

  windowHandle_ = CreateWindowExW(
    WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
    kWindowClassName,
    mode_ == Mode::kFirstRun ? kFirstRunTitle : kSettingsTitle,
    WS_CAPTION | WS_SYSMENU,
    CW_USEDEFAULT,
    CW_USEDEFAULT,
    windowWidth,
    windowHeight,
    ownerWindow_,
    nullptr,
    instanceHandle_,
    this);

  if (windowHandle_ == nullptr) {
    return false;
  }

  if (ownerWindow_ != nullptr) {
    EnableWindow(ownerWindow_, FALSE);
  }

  CenterOverOwner();
  Win32WindowActivation::ShowAndActivateWindow(windowHandle_);
  UpdateWindow(windowHandle_);

  MSG message{};
  while (windowHandle_ != nullptr) {
    const BOOL getMessageResult = GetMessageW(&message, nullptr, 0, 0);
    if (getMessageResult <= 0) {
      break;
    }

    if (capturingHotkey_ && IsHotkeyCaptureMessage(message.message) &&
        IsMessageForWindow(windowHandle_, message.hwnd) &&
        HandleHotkeyCapture(message.wParam, message.lParam)) {
      continue;
    }

    if (windowHandle_ == nullptr || !IsDialogMessageW(windowHandle_, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }

  if (ownerWindow_ != nullptr) {
    EnableWindow(ownerWindow_, TRUE);
    SetActiveWindow(ownerWindow_);
  }

  return true;
}

bool SettingsWindow::EnsureWindowClass() {
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = &SettingsWindow::WindowProc;
  windowClass.hInstance = instanceHandle_;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.lpszClassName = kWindowClassName;

  if (RegisterClassExW(&windowClass) != 0) {
    return true;
  }

  return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool SettingsWindow::CreateThemeObjects() {
  DestroyThemeObjects();
  headingFont_ = CreatePointFont(
    Win32UiTheme::Metric::kHeadingPointSize,
    FW_SEMIBOLD,
    Win32UiTheme::kHeadingFontName);
  bodyFont_ = CreatePointFont(
    Win32UiTheme::Metric::kBodyPointSize,
    FW_NORMAL,
    Win32UiTheme::kBodyFontName);
  canvasBrush_ = CreateSolidBrush(Win32UiTheme::Color::kCanvas);
  fieldBrush_ = CreateSolidBrush(Win32UiTheme::Color::kField);

  return headingFont_ != nullptr && bodyFont_ != nullptr && canvasBrush_ != nullptr &&
         fieldBrush_ != nullptr;
}

void SettingsWindow::DestroyThemeObjects() {
  if (headingFont_ != nullptr) {
    DeleteObject(headingFont_);
    headingFont_ = nullptr;
  }
  if (bodyFont_ != nullptr) {
    DeleteObject(bodyFont_);
    bodyFont_ = nullptr;
  }
  if (canvasBrush_ != nullptr) {
    DeleteObject(canvasBrush_);
    canvasBrush_ = nullptr;
  }
  if (fieldBrush_ != nullptr) {
    DeleteObject(fieldBrush_);
    fieldBrush_ = nullptr;
  }
}

void SettingsWindow::CreateFirstRunControls() {
  CreateStaticText(
    windowHandle_,
    instanceHandle_,
    L"Connect your rewrite engine",
    kContentLeft,
    kContentTop,
    kFirstRunContentWidth,
    kHeadingHeight,
    headingFont_);
  CreateStaticText(
    windowHandle_,
    instanceHandle_,
    FirstRunDisclosure(),
    kContentLeft,
    kDisclosureTop,
    kFirstRunContentWidth,
    kDisclosureHeight,
    bodyFont_);

  const int apiKeyLabelTop = kDisclosureTop + kDisclosureHeight + Win32UiTheme::Space::kLg;
  CreateStaticText(
    windowHandle_,
    instanceHandle_,
    L"OpenAI API key",
    kContentLeft,
    apiKeyLabelTop,
    kLabelWidth,
    Win32UiTheme::Metric::kLabelHeight,
    bodyFont_);
  apiKeyEdit_ = CreateInput(
    windowHandle_,
    instanceHandle_,
    L"",
    UiControlId::kSettingsApiKeyEdit,
    kInputLeft,
    apiKeyLabelTop,
    kInputWidth - Win32UiTheme::Space::kXl,
    ES_PASSWORD,
    bodyFont_);

  apiKeyStatus_ = CreateStaticText(
    windowHandle_,
    instanceHandle_,
    L"API key will be saved to Windows Credential Manager only.",
    kInputLeft,
    apiKeyLabelTop + Win32UiTheme::Metric::kInputHeight + Win32UiTheme::Space::kXs,
    kInputWidth,
    Win32UiTheme::Metric::kLabelHeight,
    bodyFont_);

  const int saveLeft = kFirstRunWindowWidth - Win32UiTheme::Space::kXl -
                       Win32UiTheme::Metric::kButtonWidth;
  const int cancelLeft = saveLeft - kButtonGap - Win32UiTheme::Metric::kButtonWidth;
  const int buttonTop = kFirstRunWindowHeight - Win32UiTheme::Space::kXxl -
                        Win32UiTheme::Metric::kButtonHeight;
  CreateButton(
    windowHandle_,
    instanceHandle_,
    L"Cancel",
    UiControlId::kSettingsCancelButton,
    cancelLeft,
    buttonTop,
    Win32UiTheme::Metric::kButtonWidth,
    bodyFont_);
  HWND saveButton = CreateButton(
    windowHandle_,
    instanceHandle_,
    L"Save key",
    UiControlId::kSettingsSaveButton,
    saveLeft,
    buttonTop,
    Win32UiTheme::Metric::kButtonWidth,
    bodyFont_);
  SetFocus(apiKeyEdit_ != nullptr ? apiKeyEdit_ : saveButton);
}

void SettingsWindow::CreateSettingsControls() {
  CreateStaticText(
    windowHandle_,
    instanceHandle_,
    L"Rewrite preferences",
    kContentLeft,
    kContentTop,
    kSettingsContentWidth,
    kHeadingHeight,
    headingFont_);

  apiKeyStatus_ = CreateStaticText(
    windowHandle_,
    instanceHandle_,
    L"",
    kContentLeft,
    kSettingsStatusTop,
    kSettingsContentWidth,
    Win32UiTheme::Metric::kLabelHeight,
    bodyFont_,
    UiControlId::kSettingsApiKeyStatus);
  RefreshApiKeyStatus();

  int rowTop = kSettingsFirstRowTop;
  CreateStaticText(
    windowHandle_,
    instanceHandle_,
    L"Add or replace API key",
    kContentLeft,
    rowTop,
    kLabelWidth,
    Win32UiTheme::Metric::kLabelHeight,
    bodyFont_);
  apiKeyEdit_ = CreateInput(
    windowHandle_,
    instanceHandle_,
    L"",
    UiControlId::kSettingsApiKeyEdit,
    kInputLeft,
    rowTop,
    kInputWidth - Win32UiTheme::Metric::kButtonWidth - kButtonGap,
    ES_PASSWORD,
    bodyFont_);
  CreateButton(
    windowHandle_,
    instanceHandle_,
    L"Remove key",
    UiControlId::kSettingsDeleteApiKeyButton,
    kInputLeft + kInputWidth - Win32UiTheme::Metric::kButtonWidth,
    rowTop,
    Win32UiTheme::Metric::kButtonWidth,
    bodyFont_);

  rowTop += kRowGap;
  CreateStaticText(windowHandle_, instanceHandle_, L"Hotkey", kContentLeft, rowTop, kLabelWidth,
    Win32UiTheme::Metric::kLabelHeight, bodyFont_);
  hotkeyEdit_ = CreateInput(windowHandle_, instanceHandle_, settings_.hotkey.c_str(),
    UiControlId::kSettingsHotkeyEdit, kInputLeft, rowTop, kHotkeyInputWidth, 0, bodyFont_);
  CreateButton(windowHandle_, instanceHandle_, L"Register hotkey",
    UiControlId::kSettingsRegisterHotkeyButton,
    kInputLeft + kHotkeyInputWidth + kButtonGap, rowTop, Win32UiTheme::Metric::kButtonWidth,
    bodyFont_);
  hotkeyStatus_ = CreateStaticText(windowHandle_, instanceHandle_,
    L"Click Register hotkey, then press a modifier and key. Choose a combination not already used.", kInputLeft,
    rowTop + Win32UiTheme::Metric::kInputHeight + Win32UiTheme::Space::kXs, kInputWidth,
    Win32UiTheme::Metric::kLabelHeight, bodyFont_, UiControlId::kSettingsHotkeyStatus);

  rowTop += kHotkeyRowGap;
  CreateStaticText(windowHandle_, instanceHandle_, L"Model ID (editable)", kContentLeft, rowTop, kLabelWidth,
    Win32UiTheme::Metric::kLabelHeight, bodyFont_);
  modelCombo_ = CreateComboBox(windowHandle_, instanceHandle_, UiControlId::kSettingsModelCombo,
    kInputLeft, rowTop, kModelComboWidth, bodyFont_);
  updateModelsButton_ = CreateButton(windowHandle_, instanceHandle_, L"Update models",
    UiControlId::kSettingsUpdateModelsButton,
    kInputLeft + kModelComboWidth + kButtonGap, rowTop, Win32UiTheme::Metric::kButtonWidth,
    bodyFont_);
  const std::vector<std::wstring> cachedModelIds = LoadCachedModelIds();
  PopulateModelComboBox(modelCombo_, settings_.model, cachedModelIds);
  modelStatus_ = CreateStaticText(windowHandle_, instanceHandle_,
    cachedModelIds.empty() ? L"Type a custom model ID or fetch the latest OpenAI list."
                           : L"Cached OpenAI model list loaded. Click Update models to refresh.",
    kInputLeft, rowTop + Win32UiTheme::Metric::kInputHeight + Win32UiTheme::Space::kXs,
    kInputWidth, Win32UiTheme::Metric::kLabelHeight, bodyFont_, UiControlId::kSettingsModelStatus);

  rowTop += kModelRowGap;
  CreateStaticText(windowHandle_, instanceHandle_, L"Request timeout", kContentLeft, rowTop,
    kLabelWidth, Win32UiTheme::Metric::kLabelHeight, bodyFont_);
  timeoutEdit_ = CreateInput(windowHandle_, instanceHandle_,
    std::to_wstring(settings_.requestTimeoutSeconds).c_str(), UiControlId::kSettingsTimeoutEdit,
    kInputLeft, rowTop, kTimeoutInputWidth, 0, bodyFont_);
  CreateStaticText(windowHandle_, instanceHandle_, L"seconds", kInputLeft + kTimeoutUnitOffset, rowTop,
    kInputWidth - kTimeoutUnitOffset, Win32UiTheme::Metric::kLabelHeight, bodyFont_);

  rowTop += kRowGap;
  restoreClipboardCheck_ = CreateCheckbox(windowHandle_, instanceHandle_,
    L"Restore clipboard after capture fallback", UiControlId::kSettingsRestoreClipboardCheck,
    kInputLeft, rowTop, kInputWidth, settings_.restoreClipboard, bodyFont_);

  rowTop += kRowGap;
  launchAtStartupCheck_ = CreateCheckbox(windowHandle_, instanceHandle_,
    L"Launch at startup when installer support is available", UiControlId::kSettingsLaunchAtStartupCheck,
    kInputLeft, rowTop, kInputWidth, settings_.launchAtStartup, bodyFont_);

  const int saveLeft = kSettingsWindowWidth - Win32UiTheme::Space::kXl -
                       Win32UiTheme::Metric::kButtonWidth;
  const int cancelLeft = saveLeft - kButtonGap - Win32UiTheme::Metric::kButtonWidth;
  const int buttonTop = kSettingsWindowHeight - Win32UiTheme::Space::kXxl -
                        Win32UiTheme::Metric::kButtonHeight;
  CreateButton(windowHandle_, instanceHandle_, L"Cancel", UiControlId::kSettingsCancelButton,
    cancelLeft, buttonTop, Win32UiTheme::Metric::kButtonWidth, bodyFont_);
  HWND saveButton = CreateButton(windowHandle_, instanceHandle_, L"Save",
    UiControlId::kSettingsSaveButton, saveLeft, buttonTop, Win32UiTheme::Metric::kButtonWidth,
    bodyFont_);
  SetFocus(hotkeyEdit_ != nullptr ? hotkeyEdit_ : saveButton);
}

void SettingsWindow::SaveFirstRunApiKey() {
  std::wstring apiKey = Trim(ReadControlText(apiKeyEdit_));
  if (apiKey.empty()) {
    MessageBoxW(windowHandle_, L"Enter an API key before saving.", kFirstRunTitle, MB_OK | MB_ICONWARNING);
    return;
  }

  const CredentialOperationResult writeResult = credentialStore_.Write(apiKey);
  ClearString(&apiKey);
  SetWindowTextW(apiKeyEdit_, L"");
  if (writeResult.status != CredentialStoreStatus::kSuccess) {
    MessageBoxW(windowHandle_, L"The API key could not be saved to Credential Manager.", kFirstRunTitle,
      MB_OK | MB_ICONERROR);
    return;
  }

  apiKeyConfigured_ = true;
  result_.settings = settings_;
  result_.apiKeyConfigured = true;
  CloseWithAction(SettingsWindowAction::kSaved);
}

void SettingsWindow::SaveSettings() {
  AppSettings updatedSettings = settings_;
  updatedSettings.hotkey = Trim(ReadControlText(hotkeyEdit_));
  updatedSettings.model = Trim(ReadSelectedModel());
  if (updatedSettings.hotkey.empty() || updatedSettings.model.empty()) {
    MessageBoxW(windowHandle_, L"Hotkey and model are required.", kSettingsTitle, MB_OK | MB_ICONWARNING);
    return;
  }

  int timeoutSeconds = 0;
  if (!TryReadTimeoutSeconds(&timeoutSeconds)) {
    MessageBoxW(windowHandle_, L"Request timeout must be a whole number from 1 to 600 seconds.",
      kSettingsTitle, MB_OK | MB_ICONWARNING);
    return;
  }

  updatedSettings.requestTimeoutSeconds = timeoutSeconds;
  updatedSettings.restoreClipboard =
    SendMessageW(restoreClipboardCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
  updatedSettings.launchAtStartup =
    SendMessageW(launchAtStartupCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;

  const SettingsOperationResult saveResult = settingsStore_.Save(updatedSettings);
  if (saveResult.status != SettingsStoreStatus::kSuccess) {
    MessageBoxW(windowHandle_, L"Settings could not be saved.", kSettingsTitle, MB_OK | MB_ICONERROR);
    return;
  }

  std::wstring apiKey = Trim(ReadControlText(apiKeyEdit_));
  if (!apiKey.empty()) {
    const CredentialOperationResult writeResult = credentialStore_.Write(apiKey);
    ClearString(&apiKey);
    SetWindowTextW(apiKeyEdit_, L"");
    if (writeResult.status != CredentialStoreStatus::kSuccess) {
      MessageBoxW(windowHandle_, L"The API key could not be saved to Credential Manager.", kSettingsTitle,
        MB_OK | MB_ICONERROR);
      return;
    }
    apiKeyConfigured_ = true;
  }

  settings_ = updatedSettings;
  result_ = {
    .action = SettingsWindowAction::kSaved,
    .settings = settings_,
    .apiKeyConfigured = apiKeyConfigured_,
  };
  CloseWithAction(SettingsWindowAction::kSaved);
}

void SettingsWindow::DeleteApiKey() {
  const CredentialOperationResult deleteResult = credentialStore_.Delete();
  if (!IsSuccessOrMissing(deleteResult.status)) {
    MessageBoxW(windowHandle_, L"The API key could not be deleted from Credential Manager.", kSettingsTitle,
      MB_OK | MB_ICONERROR);
    return;
  }

  apiKeyConfigured_ = false;
  SetWindowTextW(apiKeyEdit_, L"");
  RefreshApiKeyStatus();
  MessageBoxW(windowHandle_, L"No API key saved.", kSettingsTitle, MB_OK | MB_ICONINFORMATION);
}

void SettingsWindow::RefreshApiKeyStatus() {
  if (apiKeyStatus_ == nullptr) {
    return;
  }

  SetWindowTextW(apiKeyStatus_, apiKeyConfigured_ ? L"API key available" : L"No API key saved");
}

void SettingsWindow::SetModelStatus(const std::wstring& text, bool isError) {
  modelStatusIsError_ = isError;
  if (modelStatus_ != nullptr) {
    SetWindowTextW(modelStatus_, text.c_str());
    InvalidateRect(modelStatus_, nullptr, TRUE);
  }
}

void SettingsWindow::UpdateModels() {
  CredentialReadResult readResult = credentialStore_.Read();
  if (readResult.status != CredentialStoreStatus::kSuccess || readResult.value.empty()) {
    SetModelStatus(L"Save an OpenAI API key before updating models.", true);
    return;
  }

  std::wstring currentModel = Trim(ReadSelectedModel());
  if (currentModel.empty()) {
    currentModel = settings_.model;
  }

  std::wstring apiKey = std::move(readResult.value);
  SetModelStatus(L"Fetching the latest OpenAI model list...", false);
  if (updateModelsButton_ != nullptr) {
    EnableWindow(updateModelsButton_, FALSE);
  }

  const ModelFetchResult fetchResult = FetchOpenAIModelIds(apiKey);
  ClearString(&apiKey);
  ClearString(&readResult.value);

  if (updateModelsButton_ != nullptr) {
    EnableWindow(updateModelsButton_, TRUE);
  }

  if (fetchResult.status != ModelFetchStatus::kSuccess) {
    SetModelStatus(L"Could not update models from OpenAI. Existing suggestions were kept.", true);
    return;
  }

  PopulateModelComboBox(modelCombo_, currentModel, fetchResult.modelIds);
  if (!SaveCachedModelIds(fetchResult.modelIds)) {
    SetModelStatus(L"Model list updated, but the local cache could not be saved.", true);
    return;
  }

  SetModelStatus(L"Model list updated from OpenAI.", false);
}

void SettingsWindow::StartHotkeyCapture() {
  capturingHotkey_ = true;
  if (hotkeyStatus_ != nullptr) {
    SetWindowTextW(hotkeyStatus_, L"Press a key combination now...");
    InvalidateRect(hotkeyStatus_, nullptr, TRUE);
  }
  if (windowHandle_ != nullptr) {
    SetFocus(windowHandle_);
  }
}

bool SettingsWindow::HandleHotkeyCapture(WPARAM wparam, LPARAM /*lparam*/) {
  const UINT virtualKey = static_cast<UINT>(wparam);
  if (IsModifierVirtualKey(virtualKey)) {
    return true;
  }

  std::wstring keyName;
  if (!TryGetHotkeyKeyName(virtualKey, &keyName)) {
    if (hotkeyStatus_ != nullptr) {
      SetWindowTextW(hotkeyStatus_, L"Use A-Z, 0-9, F1-F24, Space, Tab, or Enter.");
    }
    return true;
  }

  const std::wstring hotkeyText = BuildCapturedHotkeyText(virtualKey);
  if (hotkeyText.empty()) {
    if (hotkeyStatus_ != nullptr) {
      SetWindowTextW(hotkeyStatus_, L"Hold Ctrl, Alt, or Shift with a key.");
    }
    return true;
  }

  capturingHotkey_ = false;
  if (hotkeyEdit_ != nullptr) {
    SetWindowTextW(hotkeyEdit_, hotkeyText.c_str());
    SetFocus(hotkeyEdit_);
    SendMessageW(hotkeyEdit_, EM_SETSEL, 0, -1);
  }
  if (hotkeyStatus_ != nullptr) {
    const std::wstring statusText = L"Captured: " + hotkeyText;
    SetWindowTextW(hotkeyStatus_, statusText.c_str());
    InvalidateRect(hotkeyStatus_, nullptr, TRUE);
  }

  return true;
}

void SettingsWindow::CloseWithAction(SettingsWindowAction action) {
  result_.action = action;
  if (windowHandle_ != nullptr) {
    DestroyWindow(windowHandle_);
  }
}

void SettingsWindow::CenterOverOwner() const {
  if (windowHandle_ == nullptr) {
    return;
  }

  RECT windowRect{};
  GetWindowRect(windowHandle_, &windowRect);

  if (IsUsableOwnerWindow(ownerWindow_)) {
    RECT anchorRect{};
    GetWindowRect(ownerWindow_, &anchorRect);

    RECT workArea{};
    if (!TryGetWorkAreaForWindow(ownerWindow_, &workArea) &&
        !TryGetWorkAreaForWindow(windowHandle_, &workArea)) {
      return;
    }

    const int windowWidth = windowRect.right - windowRect.left;
    const int windowHeight = windowRect.bottom - windowRect.top;
    const int anchorWidth = anchorRect.right - anchorRect.left;
    const int anchorHeight = anchorRect.bottom - anchorRect.top;
    int x = anchorRect.left + ((anchorWidth - windowWidth) / 2);
    int y = anchorRect.top + ((anchorHeight - windowHeight) / 2);
    x = ClampToRange(x, static_cast<int>(workArea.left), static_cast<int>(workArea.right - windowWidth));
    y = ClampToRange(y, static_cast<int>(workArea.top), static_cast<int>(workArea.bottom - windowHeight));
    SetWindowPos(windowHandle_, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
    return;
  }

  RECT workArea{};
  if (!TryGetWorkAreaForWindow(windowHandle_, &workArea)) {
    return;
  }

  CenterWindowWithinWorkArea(windowHandle_, workArea);
}

std::wstring SettingsWindow::ReadControlText(HWND control) const {
  if (control == nullptr) {
    return {};
  }

  const int length = GetWindowTextLengthW(control);
  if (length <= 0) {
    return {};
  }

  std::wstring text(static_cast<size_t>(length) + 1, L'\0');
  GetWindowTextW(control, text.data(), length + 1);
  text.resize(static_cast<size_t>(length));
  return text;
}

std::wstring SettingsWindow::ReadSelectedModel() const {
  return ReadControlText(modelCombo_);
}

bool SettingsWindow::TryReadTimeoutSeconds(int* timeoutSeconds) const {
  if (timeoutSeconds == nullptr) {
    return false;
  }

  const std::wstring timeoutText = Trim(ReadControlText(timeoutEdit_));
  if (timeoutText.empty()) {
    return false;
  }

  wchar_t* end = nullptr;
  const long parsed = std::wcstol(timeoutText.c_str(), &end, 10);
  if (end == timeoutText.c_str() || *end != L'\0' || parsed < 1 || parsed > 600) {
    return false;
  }

  *timeoutSeconds = static_cast<int>(parsed);
  return true;
}

LRESULT SettingsWindow::HandleMessage(HWND windowHandle, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE:
      if (mode_ == Mode::kFirstRun) {
        CreateFirstRunControls();
      } else {
        CreateSettingsControls();
      }
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case UiControlId::kSettingsSaveButton:
          if (mode_ == Mode::kFirstRun) {
            SaveFirstRunApiKey();
          } else {
            SaveSettings();
          }
          return 0;
        case UiControlId::kSettingsDeleteApiKeyButton:
          DeleteApiKey();
          return 0;
        case UiControlId::kSettingsRegisterHotkeyButton:
          if (mode_ == Mode::kSettings) {
            StartHotkeyCapture();
          }
          return 0;
        case UiControlId::kSettingsUpdateModelsButton:
          if (mode_ == Mode::kSettings) {
            UpdateModels();
          }
          return 0;
        case UiControlId::kSettingsCancelButton:
          CloseWithAction(SettingsWindowAction::kCancel);
          return 0;
        default:
          break;
      }
      break;
    case WM_CLOSE:
      CloseWithAction(SettingsWindowAction::kCancel);
      return 0;
    case WM_NCDESTROY:
      windowHandle_ = nullptr;
      break;
    case WM_ERASEBKGND: {
      RECT clientRect{};
      GetClientRect(windowHandle, &clientRect);
      FillRect(reinterpret_cast<HDC>(wparam), &clientRect, canvasBrush_);
      return 1;
    }
    case WM_CTLCOLORSTATIC: {
      HDC deviceContext = reinterpret_cast<HDC>(wparam);
      const HWND control = reinterpret_cast<HWND>(lparam);
      if (control == apiKeyStatus_) {
        SetTextColor(deviceContext, apiKeyConfigured_ ? Win32UiTheme::Color::kAccent
                                                      : Win32UiTheme::Color::kDanger);
      } else if (control == hotkeyStatus_) {
        SetTextColor(deviceContext, Win32UiTheme::Color::kAccent);
      } else if (control == modelStatus_) {
        SetTextColor(deviceContext, modelStatusIsError_ ? Win32UiTheme::Color::kDanger
                                                        : Win32UiTheme::Color::kAccent);
      } else {
        SetTextColor(deviceContext, Win32UiTheme::Color::kInk);
      }
      SetBkColor(deviceContext, Win32UiTheme::Color::kCanvas);
      return reinterpret_cast<LRESULT>(canvasBrush_);
    }
    case WM_CTLCOLOREDIT: {
      HDC deviceContext = reinterpret_cast<HDC>(wparam);
      SetTextColor(deviceContext, Win32UiTheme::Color::kInk);
      SetBkColor(deviceContext, Win32UiTheme::Color::kField);
      return reinterpret_cast<LRESULT>(fieldBrush_);
    }
    default:
      break;
  }

  return DefWindowProcW(windowHandle, message, wparam, lparam);
}

LRESULT CALLBACK SettingsWindow::WindowProc(
  HWND windowHandle,
  UINT message,
  WPARAM wparam,
  LPARAM lparam) {
  SettingsWindow* settingsWindow = nullptr;

  if (message == WM_NCCREATE) {
    const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    settingsWindow = static_cast<SettingsWindow*>(createStruct->lpCreateParams);
    SetWindowLongPtrW(windowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(settingsWindow));
    if (settingsWindow != nullptr) {
      settingsWindow->windowHandle_ = windowHandle;
    }
  } else {
    settingsWindow = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(windowHandle, GWLP_USERDATA));
  }

  if (settingsWindow == nullptr) {
    return DefWindowProcW(windowHandle, message, wparam, lparam);
  }

  const LRESULT result = settingsWindow->HandleMessage(windowHandle, message, wparam, lparam);
  if (message == WM_NCDESTROY) {
    SetWindowLongPtrW(windowHandle, GWLP_USERDATA, 0);
    settingsWindow->windowHandle_ = nullptr;
  }

  return result;
}
