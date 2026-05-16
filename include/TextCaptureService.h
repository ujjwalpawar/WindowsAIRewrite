#pragma once

#include <windows.h>

#include <string>

#include "AppError.h"
#include "ClipboardManager.h"

struct TextCaptureResult {
  bool success = false;
  std::wstring text{};
  AppError error{
    .code = AppErrorCode::kUnexpectedInternalError,
    .hresult = S_OK,
    .win32Error = ERROR_SUCCESS,
  };
};

class TextCaptureService {
 public:
  TextCaptureService() = default;
  explicit TextCaptureService(ClipboardManager clipboardManager);

  [[nodiscard]] TextCaptureResult CaptureSelectedText(bool restoreClipboard) const;

 private:
  [[nodiscard]] TextCaptureResult CaptureWithUiAutomation() const;
  [[nodiscard]] TextCaptureResult CaptureWithClipboardFallback(bool restoreClipboard) const;

  ClipboardManager clipboardManager_{};
};
