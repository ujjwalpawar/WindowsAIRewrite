#include "SafeLog.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <string_view>
#include <sstream>

namespace {

std::wstring_view EventName(SafeLogEvent event) {
  switch (event) {
    case SafeLogEvent::kAppStart:
      return L"AppStart";
    case SafeLogEvent::kAppStop:
      return L"AppStop";
    case SafeLogEvent::kHotkeyRegister:
      return L"HotkeyRegister";
    case SafeLogEvent::kHotkeyConflict:
      return L"HotkeyConflict";
    case SafeLogEvent::kRewriteRequest:
      return L"RewriteRequest";
    case SafeLogEvent::kClipboardRestore:
      return L"ClipboardRestore";
    case SafeLogEvent::kOpenAIRequest:
      return L"OpenAIRequest";
    case SafeLogEvent::kOpenAIResponse:
      return L"OpenAIResponse";
    case SafeLogEvent::kError:
    default:
      return L"Error";
  }
}

std::wstring_view LabelName(SafeLogLabel label) {
  switch (label) {
    case SafeLogLabel::kStartup:
      return L"Startup";
    case SafeLogLabel::kHotkey:
      return L"Hotkey";
    case SafeLogLabel::kSelection:
      return L"Selection";
    case SafeLogLabel::kClipboard:
      return L"Clipboard";
    case SafeLogLabel::kNetwork:
      return L"Network";
    case SafeLogLabel::kCredential:
      return L"Credential";
    case SafeLogLabel::kOpenAI:
      return L"OpenAI";
    case SafeLogLabel::kTray:
      return L"Tray";
    case SafeLogLabel::kSettings:
      return L"Settings";
    case SafeLogLabel::kUnsupported:
      return L"Unsupported";
    case SafeLogLabel::kInternal:
      return L"Internal";
    case SafeLogLabel::kNone:
    default:
      return L"None";
  }
}

std::filesystem::path LogPath() {
  wchar_t localAppData[MAX_PATH]{};
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return {};
  }

  std::filesystem::path path = localAppData;
  path /= L"WindowsAIRewrite";
  path /= L"logs";
  path /= L"privacy.log";
  return path;
}

std::wstring Timestamp() {
  SYSTEMTIME systemTime{};
  GetLocalTime(&systemTime);

  std::wostringstream stream;
  stream << std::setfill(L'0') << std::setw(4) << systemTime.wYear << L'-'
         << std::setw(2) << systemTime.wMonth << L'-' << std::setw(2) << systemTime.wDay
         << L'T' << std::setw(2) << systemTime.wHour << L':' << std::setw(2)
         << systemTime.wMinute << L':' << std::setw(2) << systemTime.wSecond;
  return stream.str();
}

}  // namespace

bool WriteSafeLog(
  SafeLogEvent event,
  int statusCode,
  HRESULT hresult,
  DWORD win32Error,
  SafeLogLabel label) {
  const std::filesystem::path path = LogPath();
  if (path.empty()) {
    return false;
  }

  std::error_code errorCode;
  std::filesystem::create_directories(path.parent_path(), errorCode);
  if (errorCode) {
    return false;
  }

  std::wofstream file(path, std::ios::app);
  if (!file.is_open()) {
    return false;
  }

  const std::wstring line =
    Timestamp() + L" event=" + std::wstring(EventName(event)) + L" status=" +
    std::to_wstring(statusCode) + L" hresult=0x" + [&]() {
      std::wostringstream stream;
      stream << std::uppercase << std::hex << static_cast<unsigned long>(hresult);
      return stream.str();
    }() + L" win32=" + std::to_wstring(win32Error) + L" label=" +
    std::wstring(LabelName(label));

  file << line << L'\n';
  OutputDebugStringW(line.c_str());
  OutputDebugStringW(L"\n");
  return true;
}
