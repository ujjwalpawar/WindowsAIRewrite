#include "OpenAIClient.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "SafeLog.h"

namespace {

constexpr wchar_t kOpenAIHost[] = L"api.openai.com";
constexpr wchar_t kOpenAIPath[] = L"/v1/responses";
constexpr wchar_t kUserAgent[] = L"WindowsAIRewrite/0.1";
constexpr wchar_t kRewriteInstruction[] =
  L"Rewrite the provided text to improve clarity, grammar, and concision while "
  L"preserving the original meaning. Return only the rewritten text.";
constexpr DWORD kHttpsPort = INTERNET_DEFAULT_HTTPS_PORT;
constexpr DWORD kReadBufferSize = 4096;

OpenAIRewriteResult MakeSuccess(std::wstring rewrittenText) {
  return {
    .success = true,
    .rewrittenText = std::move(rewrittenText),
    .error = {
      .code = AppErrorCode::kUnexpectedInternalError,
      .hresult = S_OK,
      .win32Error = ERROR_SUCCESS,
    },
  };
}

OpenAIRewriteResult MakeFailure(AppErrorCode code, HRESULT hresult, DWORD win32Error) {
  return {
    .success = false,
    .rewrittenText = {},
    .error = {
      .code = code,
      .hresult = hresult,
      .win32Error = win32Error,
    },
  };
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

AppSettings EffectiveSettings(const SettingsStore& settingsStore) {
  AppSettings settings = SettingsStore::DefaultSettings();
  const SettingsLoadResult loadResult = settingsStore.Load();
  if (loadResult.status == SettingsStoreStatus::kSuccess) {
    settings = loadResult.settings;
  }

  const AppSettings defaults = SettingsStore::DefaultSettings();
  if (settings.model.empty()) {
    settings.model = defaults.model;
  }
  if (settings.requestTimeoutSeconds <= 0) {
    settings.requestTimeoutSeconds = defaults.requestTimeoutSeconds;
  }

  return settings;
}

DWORD EffectiveTimeoutMilliseconds(const AppSettings& settings) {
  constexpr DWORD kMaximumTimeoutSeconds = 300;
  const int clampedSeconds = std::clamp(settings.requestTimeoutSeconds, 1, static_cast<int>(kMaximumTimeoutSeconds));
  return static_cast<DWORD>(clampedSeconds) * 1000;
}

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

bool SetRequestTimeouts(HINTERNET handle, DWORD timeoutMs) {
  const int timeout = static_cast<int>(timeoutMs);
  return WinHttpSetTimeouts(handle, timeout, timeout, timeout, timeout) == TRUE;
}

OpenAIRewriteResult MapTransportFailure(DWORD win32Error) {
  switch (win32Error) {
    case ERROR_TIMEOUT:
    case ERROR_WINHTTP_TIMEOUT:
      return MakeFailure(
        AppErrorCode::kNetworkTimeout,
        HRESULT_FROM_WIN32(win32Error),
        win32Error);
    default:
      return MakeFailure(
        AppErrorCode::kOpenAIError,
        HRESULT_FROM_WIN32(win32Error),
        win32Error);
  }
}

OpenAIRewriteResult MapHttpFailure(DWORD httpStatus) {
  if (httpStatus == 401 || httpStatus == 403) {
    return MakeFailure(AppErrorCode::kAuthFailed, S_OK, httpStatus);
  }

  if (httpStatus == 429 || httpStatus >= 500) {
    return MakeFailure(AppErrorCode::kOpenAIError, S_OK, httpStatus);
  }

  return MakeFailure(AppErrorCode::kOpenAIError, S_OK, httpStatus);
}

SafeLogLabel LabelForResult(const OpenAIRewriteResult& result) {
  switch (result.error.code) {
    case AppErrorCode::kNetworkTimeout:
      return SafeLogLabel::kNetwork;
    case AppErrorCode::kMissingCredential:
      return SafeLogLabel::kCredential;
    case AppErrorCode::kAuthFailed:
    case AppErrorCode::kOpenAIError:
      return SafeLogLabel::kOpenAI;
    default:
      return SafeLogLabel::kInternal;
  }
}

bool IsTextLikeContentItem(const nlohmann::json& contentItem) {
  if (!contentItem.is_object()) {
    return false;
  }

  if (!contentItem.contains("type") || !contentItem.at("type").is_string()) {
    return contentItem.contains("text") || contentItem.contains("value");
  }

  const std::string type = contentItem.at("type").get<std::string>();
  return type.find("text") != std::string::npos;
}

void AppendTextLikeValue(const nlohmann::json& contentItem, std::wstring* destination) {
  if (destination == nullptr || !IsTextLikeContentItem(contentItem)) {
    return;
  }

  if (contentItem.contains("text")) {
    const nlohmann::json& textField = contentItem.at("text");
    if (textField.is_string()) {
      const std::wstring value = WideFromUtf8(textField.get<std::string>());
      if (!value.empty()) {
        destination->append(value);
      }
      return;
    }

    if (textField.is_object() && textField.contains("value") && textField.at("value").is_string()) {
      const std::wstring value = WideFromUtf8(textField.at("value").get<std::string>());
      if (!value.empty()) {
        destination->append(value);
      }
      return;
    }
  }

  if (contentItem.contains("value") && contentItem.at("value").is_string()) {
    const std::wstring value = WideFromUtf8(contentItem.at("value").get<std::string>());
    if (!value.empty()) {
      destination->append(value);
    }
  }
}

std::wstring ExtractOutputText(const nlohmann::json& responseJson) {
  if (responseJson.contains("output_text") && responseJson.at("output_text").is_string()) {
    const std::wstring outputText = WideFromUtf8(responseJson.at("output_text").get<std::string>());
    if (!outputText.empty()) {
      return outputText;
    }
  }

  if (!responseJson.contains("output") || !responseJson.at("output").is_array()) {
    return {};
  }

  std::wstring combinedText;
  for (const nlohmann::json& outputItem : responseJson.at("output")) {
    if (!outputItem.is_object() || !outputItem.contains("content") || !outputItem.at("content").is_array()) {
      continue;
    }

    for (const nlohmann::json& contentItem : outputItem.at("content")) {
      AppendTextLikeValue(contentItem, &combinedText);
    }
  }

  return combinedText;
}

OpenAIRewriteResult ParseOpenAIResponseBody(const std::string& responseBodyUtf8, DWORD httpStatus) {
  if (httpStatus < 200 || httpStatus >= 300) {
    return MapHttpFailure(httpStatus);
  }

  nlohmann::json responseJson;
  try {
    responseJson = nlohmann::json::parse(responseBodyUtf8);
  } catch (const nlohmann::json::exception&) {
    return MakeFailure(AppErrorCode::kOpenAIError, S_OK, httpStatus);
  }

  if (responseJson.contains("error") && !responseJson.at("error").is_null()) {
    return MakeFailure(AppErrorCode::kOpenAIError, S_OK, httpStatus);
  }

  const std::wstring rewrittenText = ExtractOutputText(responseJson);
  if (rewrittenText.empty()) {
    return MakeFailure(AppErrorCode::kOpenAIError, S_OK, httpStatus);
  }

  return MakeSuccess(rewrittenText);
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

}  // namespace

OpenAIClient::OpenAIClient() = default;

OpenAIClient::OpenAIClient(SettingsStore settingsStore)
  : settingsStore_(std::move(settingsStore)) {}

OpenAIRewriteResult OpenAIClient::RewriteText(
  const std::wstring& selectedText,
  const std::wstring& apiKey) const {
  if (selectedText.empty()) {
    return MakeFailure(AppErrorCode::kEmptySelection, S_OK, ERROR_SUCCESS);
  }

  if (apiKey.empty()) {
    return MakeFailure(
      AppErrorCode::kMissingCredential,
      HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
      ERROR_NOT_FOUND);
  }

  const AppSettings settings = EffectiveSettings(settingsStore_);
  const std::string modelUtf8 = Utf8FromWide(settings.model);
  const std::string instructionUtf8 = Utf8FromWide(kRewriteInstruction);
  const std::string inputUtf8 = Utf8FromWide(selectedText);
  if ((settings.model.size() > 0 && modelUtf8.empty()) || inputUtf8.empty() || instructionUtf8.empty()) {
    return MakeFailure(
      AppErrorCode::kUnexpectedInternalError,
      HRESULT_FROM_WIN32(ERROR_NO_UNICODE_TRANSLATION),
      ERROR_NO_UNICODE_TRANSLATION);
  }

  const nlohmann::json requestJson = {
    {"model", modelUtf8},
    {"instructions", instructionUtf8},
    {"input", inputUtf8},
  };
  const std::string requestBodyUtf8 = requestJson.dump();
  const DWORD timeoutMs = EffectiveTimeoutMilliseconds(settings);

  WriteSafeLog(SafeLogEvent::kOpenAIRequest, 0, S_OK, ERROR_SUCCESS, SafeLogLabel::kOpenAI);

  WinHttpHandle session;
  session.reset(WinHttpOpen(
    kUserAgent,
    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
    WINHTTP_NO_PROXY_NAME,
    WINHTTP_NO_PROXY_BYPASS,
    0));
  if (!session) {
    const DWORD errorCode = GetLastError();
    const OpenAIRewriteResult result = MapTransportFailure(errorCode);
    WriteSafeLog(
      SafeLogEvent::kOpenAIResponse,
      static_cast<int>(result.error.code),
      result.error.hresult,
      result.error.win32Error,
      LabelForResult(result));
    return result;
  }

  if (!SetRequestTimeouts(session.get(), timeoutMs)) {
    const DWORD errorCode = GetLastError();
    const OpenAIRewriteResult result = MapTransportFailure(errorCode);
    WriteSafeLog(
      SafeLogEvent::kOpenAIResponse,
      static_cast<int>(result.error.code),
      result.error.hresult,
      result.error.win32Error,
      LabelForResult(result));
    return result;
  }

  WinHttpHandle connection;
  connection.reset(WinHttpConnect(session.get(), kOpenAIHost, kHttpsPort, 0));
  if (!connection) {
    const DWORD errorCode = GetLastError();
    const OpenAIRewriteResult result = MapTransportFailure(errorCode);
    WriteSafeLog(
      SafeLogEvent::kOpenAIResponse,
      static_cast<int>(result.error.code),
      result.error.hresult,
      result.error.win32Error,
      LabelForResult(result));
    return result;
  }

  WinHttpHandle request;
  request.reset(WinHttpOpenRequest(
    connection.get(),
    L"POST",
    kOpenAIPath,
    nullptr,
    WINHTTP_NO_REFERER,
    WINHTTP_DEFAULT_ACCEPT_TYPES,
    WINHTTP_FLAG_SECURE));
  if (!request) {
    const DWORD errorCode = GetLastError();
    const OpenAIRewriteResult result = MapTransportFailure(errorCode);
    WriteSafeLog(
      SafeLogEvent::kOpenAIResponse,
      static_cast<int>(result.error.code),
      result.error.hresult,
      result.error.win32Error,
      LabelForResult(result));
    return result;
  }

  if (!SetRequestTimeouts(request.get(), timeoutMs)) {
    const DWORD errorCode = GetLastError();
    const OpenAIRewriteResult result = MapTransportFailure(errorCode);
    WriteSafeLog(
      SafeLogEvent::kOpenAIResponse,
      static_cast<int>(result.error.code),
      result.error.hresult,
      result.error.win32Error,
      LabelForResult(result));
    return result;
  }

  std::wstring headers = L"Author" L"ization: Bearer " + apiKey +
                         L"\r\nContent-Type: application/json\r\n";
  if (WinHttpSendRequest(
        request.get(),
        headers.c_str(),
        static_cast<DWORD>(headers.size()),
        const_cast<char*>(requestBodyUtf8.data()),
        static_cast<DWORD>(requestBodyUtf8.size()),
        static_cast<DWORD>(requestBodyUtf8.size()),
        0) != TRUE) {
    const DWORD errorCode = GetLastError();
    std::fill(headers.begin(), headers.end(), L'\0');
    const OpenAIRewriteResult result = MapTransportFailure(errorCode);
    WriteSafeLog(
      SafeLogEvent::kOpenAIResponse,
      static_cast<int>(result.error.code),
      result.error.hresult,
      result.error.win32Error,
      LabelForResult(result));
    return result;
  }
  std::fill(headers.begin(), headers.end(), L'\0');

  if (WinHttpReceiveResponse(request.get(), nullptr) != TRUE) {
    const DWORD errorCode = GetLastError();
    const OpenAIRewriteResult result = MapTransportFailure(errorCode);
    WriteSafeLog(
      SafeLogEvent::kOpenAIResponse,
      static_cast<int>(result.error.code),
      result.error.hresult,
      result.error.win32Error,
      LabelForResult(result));
    return result;
  }

  DWORD httpStatus = 0;
  if (!QueryStatusCode(request.get(), &httpStatus)) {
    const DWORD errorCode = GetLastError();
    const OpenAIRewriteResult result = MapTransportFailure(errorCode);
    WriteSafeLog(
      SafeLogEvent::kOpenAIResponse,
      static_cast<int>(result.error.code),
      result.error.hresult,
      result.error.win32Error,
      LabelForResult(result));
    return result;
  }

  std::string responseBodyUtf8;
  if (!ReadResponseBody(request.get(), &responseBodyUtf8)) {
    const DWORD errorCode = GetLastError();
    const OpenAIRewriteResult result = MapTransportFailure(errorCode);
    WriteSafeLog(
      SafeLogEvent::kOpenAIResponse,
      static_cast<int>(result.error.code),
      result.error.hresult,
      result.error.win32Error,
      LabelForResult(result));
    return result;
  }

  const OpenAIRewriteResult result = ParseOpenAIResponseBody(responseBodyUtf8, httpStatus);
  WriteSafeLog(
    SafeLogEvent::kOpenAIResponse,
    static_cast<int>(httpStatus),
    result.error.hresult,
    result.error.win32Error,
    result.success ? SafeLogLabel::kOpenAI : LabelForResult(result));
  return result;
}
