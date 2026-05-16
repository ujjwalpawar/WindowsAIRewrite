#include "AppError.h"

const wchar_t* AppErrorCodeName(AppErrorCode code) {
  switch (code) {
    case AppErrorCode::kEmptySelection:
      return L"EmptySelection";
    case AppErrorCode::kUnsupportedSelection:
      return L"UnsupportedSelection";
    case AppErrorCode::kElevatedTargetUnsupported:
      return L"ElevatedTargetUnsupported";
    case AppErrorCode::kSecureDesktopUnsupported:
      return L"SecureDesktopUnsupported";
    case AppErrorCode::kPasswordFieldUnsupported:
      return L"PasswordFieldUnsupported";
    case AppErrorCode::kClipboardBusy:
      return L"ClipboardBusy";
    case AppErrorCode::kMissingCredential:
      return L"MissingCredential";
    case AppErrorCode::kAuthFailed:
      return L"AuthFailed";
    case AppErrorCode::kNetworkTimeout:
      return L"NetworkTimeout";
    case AppErrorCode::kOpenAIError:
      return L"OpenAIError";
    case AppErrorCode::kHotkeyConflict:
      return L"HotkeyConflict";
    case AppErrorCode::kAlreadyRunning:
      return L"AlreadyRunning";
    case AppErrorCode::kUnexpectedInternalError:
    default:
      return L"UnexpectedInternalError";
  }
}
