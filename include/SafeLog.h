#pragma once

#include <windows.h>

enum class SafeLogEvent : int {
  kAppStart = 1,
  kAppStop = 2,
  kHotkeyRegister = 3,
  kHotkeyConflict = 4,
  kRewriteRequest = 5,
  kClipboardRestore = 6,
  kOpenAIRequest = 7,
  kOpenAIResponse = 8,
  kError = 9,
};

enum class SafeLogLabel : int {
  kNone = 0,
  kStartup = 1,
  kHotkey = 2,
  kSelection = 3,
  kClipboard = 4,
  kNetwork = 5,
  kCredential = 6,
  kOpenAI = 7,
  kTray = 8,
  kSettings = 9,
  kUnsupported = 10,
  kInternal = 11,
};

[[nodiscard]] bool WriteSafeLog(
  SafeLogEvent event,
  int statusCode,
  HRESULT hresult = S_OK,
  DWORD win32Error = ERROR_SUCCESS,
  SafeLogLabel label = SafeLogLabel::kNone);
