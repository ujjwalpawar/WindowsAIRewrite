#pragma once

#include <windows.h>

#include <string>

#include "AppError.h"
#include "SettingsStore.h"

struct OpenAIRewriteResult {
  bool success = false;
  std::wstring rewrittenText{};
  AppError error{
    .code = AppErrorCode::kUnexpectedInternalError,
    .hresult = S_OK,
    .win32Error = ERROR_SUCCESS,
  };
};

class OpenAIClient {
 public:
  OpenAIClient();
  explicit OpenAIClient(SettingsStore settingsStore);

  [[nodiscard]] OpenAIRewriteResult RewriteText(
    const std::wstring& selectedText,
    const std::wstring& apiKey) const;

 private:
  SettingsStore settingsStore_{};
};
