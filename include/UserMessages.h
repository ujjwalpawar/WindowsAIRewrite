#pragma once

enum class UserMessageId : int {
  kEmptySelection = 1,
  kUnsupportedSelection = 2,
  kElevatedTargetUnsupported = 3,
  kSecureDesktopUnsupported = 4,
  kPasswordFieldUnsupported = 5,
  kClipboardBusy = 6,
  kMissingCredential = 7,
  kAuthFailed = 8,
  kInvalidApiKey = kAuthFailed,
  kNetworkTimeout = 9,
  kOpenAIError = 10,
  kHotkeyConflict = 11,
  kAlreadyRunning = 12,
  kUnexpectedInternalError = 13,
};

[[nodiscard]] const wchar_t* UserMessage(UserMessageId messageId);
[[nodiscard]] const wchar_t* FirstRunDisclosure();
