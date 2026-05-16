#pragma once

#include <windows.h>

enum class AppErrorCode : int {
  kEmptySelection = 1001,
  kUnsupportedSelection = 1002,
  kElevatedTargetUnsupported = 1003,
  kSecureDesktopUnsupported = 1004,
  kPasswordFieldUnsupported = 1005,
  kClipboardBusy = 1006,
  kMissingCredential = 1007,
  kAuthFailed = 1008,
  kNetworkTimeout = 1009,
  kOpenAIError = 1010,
  kHotkeyConflict = 1011,
  kAlreadyRunning = 1012,
  kUnexpectedInternalError = 1099,
};

struct AppError {
  AppErrorCode code = AppErrorCode::kUnexpectedInternalError;
  HRESULT hresult = S_OK;
  DWORD win32Error = ERROR_SUCCESS;
};

[[nodiscard]] const wchar_t* AppErrorCodeName(AppErrorCode code);
