#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "AppError.h"

struct ClipboardFormatSnapshot {
  UINT format = 0;
  std::vector<unsigned char> bytes{};
};

struct ClipboardSnapshot {
  bool hadClipboardData = false;
  std::vector<ClipboardFormatSnapshot> formats{};
};

struct ClipboardOperationResult {
  bool success = false;
  AppError error{
    .code = AppErrorCode::kClipboardBusy,
    .hresult = S_OK,
    .win32Error = ERROR_SUCCESS,
  };
};

struct ClipboardReadResult {
  bool success = false;
  bool hasText = false;
  std::wstring text{};
  AppError error{
    .code = AppErrorCode::kClipboardBusy,
    .hresult = S_OK,
    .win32Error = ERROR_SUCCESS,
  };
};

class ClipboardManager {
 public:
  [[nodiscard]] ClipboardOperationResult Snapshot(ClipboardSnapshot* snapshot) const;
  [[nodiscard]] ClipboardOperationResult Restore(const ClipboardSnapshot& snapshot) const;
  [[nodiscard]] ClipboardReadResult ReadUnicodeText() const;
  [[nodiscard]] ClipboardOperationResult WriteUnicodeText(const std::wstring& text) const;
};
