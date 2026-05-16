#include "ClipboardManager.h"

#include <cstring>
#include <utility>

namespace {

constexpr int kOpenClipboardAttempts = 8;
constexpr DWORD kOpenClipboardDelayMs = 25;

ClipboardOperationResult MakeClipboardSuccess() {
  return {
    .success = true,
    .error = {
      .code = AppErrorCode::kClipboardBusy,
      .hresult = S_OK,
      .win32Error = ERROR_SUCCESS,
    },
  };
}

ClipboardOperationResult MakeClipboardFailure(DWORD win32Error) {
  if (win32Error == ERROR_SUCCESS) {
    win32Error = ERROR_CLIPBOARD_NOT_OPEN;
  }

  return {
    .success = false,
    .error = {
      .code = AppErrorCode::kClipboardBusy,
      .hresult = HRESULT_FROM_WIN32(win32Error),
      .win32Error = win32Error,
    },
  };
}

ClipboardReadResult MakeClipboardReadSuccess(std::wstring text, bool hasText) {
  return {
    .success = true,
    .hasText = hasText,
    .text = std::move(text),
    .error = {
      .code = AppErrorCode::kClipboardBusy,
      .hresult = S_OK,
      .win32Error = ERROR_SUCCESS,
    },
  };
}

ClipboardReadResult MakeClipboardReadFailure(DWORD win32Error) {
  if (win32Error == ERROR_SUCCESS) {
    win32Error = ERROR_CLIPBOARD_NOT_OPEN;
  }

  return {
    .success = false,
    .hasText = false,
    .text = {},
    .error = {
      .code = AppErrorCode::kClipboardBusy,
      .hresult = HRESULT_FROM_WIN32(win32Error),
      .win32Error = win32Error,
    },
  };
}

class ClipboardScope {
 public:
  ClipboardScope() {
    for (int attempt = 0; attempt < kOpenClipboardAttempts; ++attempt) {
      if (OpenClipboard(nullptr) == TRUE) {
        open_ = true;
        errorCode_ = ERROR_SUCCESS;
        return;
      }

      errorCode_ = GetLastError();
      Sleep(kOpenClipboardDelayMs);
    }
  }

  ClipboardScope(const ClipboardScope&) = delete;
  ClipboardScope& operator=(const ClipboardScope&) = delete;

  ~ClipboardScope() {
    if (open_) {
      CloseClipboard();
    }
  }

  [[nodiscard]] bool is_open() const {
    return open_;
  }

  [[nodiscard]] DWORD error_code() const {
    return errorCode_;
  }

 private:
  bool open_ = false;
  DWORD errorCode_ = ERROR_CLIPBOARD_NOT_OPEN;
};

class GlobalLockScope {
 public:
  explicit GlobalLockScope(HGLOBAL handle) : handle_(handle) {
    if (handle_ != nullptr) {
      data_ = GlobalLock(handle_);
    }
  }

  GlobalLockScope(const GlobalLockScope&) = delete;
  GlobalLockScope& operator=(const GlobalLockScope&) = delete;

  ~GlobalLockScope() {
    if (data_ != nullptr) {
      GlobalUnlock(handle_);
    }
  }

  [[nodiscard]] void* data() const {
    return data_;
  }

 private:
  HGLOBAL handle_ = nullptr;
  void* data_ = nullptr;
};

ClipboardFormatSnapshot SnapshotFormat(UINT format, HGLOBAL handle) {
  ClipboardFormatSnapshot snapshot{};
  snapshot.format = format;

  const SIZE_T size = GlobalSize(handle);
  if (size == 0) {
    return snapshot;
  }

  GlobalLockScope lock(handle);
  if (lock.data() == nullptr) {
    return snapshot;
  }

  snapshot.bytes.resize(size);
  std::memcpy(snapshot.bytes.data(), lock.data(), size);
  return snapshot;
}

}  // namespace

ClipboardOperationResult ClipboardManager::Snapshot(ClipboardSnapshot* snapshot) const {
  if (snapshot == nullptr) {
    return MakeClipboardFailure(ERROR_INVALID_PARAMETER);
  }

  ClipboardScope clipboard;
  if (!clipboard.is_open()) {
    return MakeClipboardFailure(clipboard.error_code());
  }

  snapshot->hadClipboardData = CountClipboardFormats() != 0;
  snapshot->formats.clear();

  UINT format = 0;
  for (;;) {
    format = EnumClipboardFormats(format);
    if (format == 0) {
      const DWORD errorCode = GetLastError();
      if (errorCode != ERROR_SUCCESS) {
        return MakeClipboardFailure(errorCode);
      }

      break;
    }

    HANDLE handle = GetClipboardData(format);
    if (handle == nullptr) {
      continue;
    }

    ClipboardFormatSnapshot formatSnapshot =
      SnapshotFormat(format, static_cast<HGLOBAL>(handle));
    if (!formatSnapshot.bytes.empty()) {
      snapshot->formats.push_back(std::move(formatSnapshot));
    }
  }

  if (snapshot->hadClipboardData && snapshot->formats.empty()) {
    return MakeClipboardFailure(ERROR_NOT_SUPPORTED);
  }

  return MakeClipboardSuccess();
}

ClipboardOperationResult ClipboardManager::Restore(const ClipboardSnapshot& snapshot) const {
  ClipboardScope clipboard;
  if (!clipboard.is_open()) {
    return MakeClipboardFailure(clipboard.error_code());
  }

  if (EmptyClipboard() != TRUE) {
    return MakeClipboardFailure(GetLastError());
  }

  if (!snapshot.hadClipboardData) {
    return MakeClipboardSuccess();
  }

  for (const ClipboardFormatSnapshot& formatSnapshot : snapshot.formats) {
    if (formatSnapshot.bytes.empty()) {
      continue;
    }

    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, formatSnapshot.bytes.size());
    if (handle == nullptr) {
      return MakeClipboardFailure(GetLastError());
    }

    {
      GlobalLockScope lock(handle);
      if (lock.data() == nullptr) {
        const DWORD errorCode = GetLastError();
        GlobalFree(handle);
        return MakeClipboardFailure(errorCode);
      }

      std::memcpy(lock.data(), formatSnapshot.bytes.data(), formatSnapshot.bytes.size());
    }

    if (SetClipboardData(formatSnapshot.format, handle) == nullptr) {
      const DWORD errorCode = GetLastError();
      GlobalFree(handle);
      return MakeClipboardFailure(errorCode);
    }

    handle = nullptr;
  }

  return MakeClipboardSuccess();
}

ClipboardReadResult ClipboardManager::ReadUnicodeText() const {
  ClipboardScope clipboard;
  if (!clipboard.is_open()) {
    return MakeClipboardReadFailure(clipboard.error_code());
  }

  if (IsClipboardFormatAvailable(CF_UNICODETEXT) != TRUE) {
    return MakeClipboardReadSuccess({}, false);
  }

  HANDLE handle = GetClipboardData(CF_UNICODETEXT);
  if (handle == nullptr) {
    return MakeClipboardReadFailure(GetLastError());
  }

  GlobalLockScope lock(static_cast<HGLOBAL>(handle));
  if (lock.data() == nullptr) {
    return MakeClipboardReadFailure(GetLastError());
  }

  const SIZE_T byteCount = GlobalSize(static_cast<HGLOBAL>(handle));
  if (byteCount < sizeof(wchar_t) || (byteCount % sizeof(wchar_t)) != 0) {
    return MakeClipboardReadFailure(ERROR_INVALID_DATA);
  }

  const wchar_t* text = static_cast<const wchar_t*>(lock.data());
  const size_t characterCapacity = byteCount / sizeof(wchar_t);
  size_t length = 0;
  while (length < characterCapacity && text[length] != L'\0') {
    ++length;
  }

  if (length == characterCapacity) {
    return MakeClipboardReadFailure(ERROR_INVALID_DATA);
  }

  return MakeClipboardReadSuccess(std::wstring(text, length), true);
}

ClipboardOperationResult ClipboardManager::WriteUnicodeText(const std::wstring& text) const {
  ClipboardScope clipboard;
  if (!clipboard.is_open()) {
    return MakeClipboardFailure(clipboard.error_code());
  }

  if (EmptyClipboard() != TRUE) {
    return MakeClipboardFailure(GetLastError());
  }

  const SIZE_T byteCount = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, byteCount);
  if (handle == nullptr) {
    return MakeClipboardFailure(GetLastError());
  }

  {
    GlobalLockScope lock(handle);
    if (lock.data() == nullptr) {
      const DWORD errorCode = GetLastError();
      GlobalFree(handle);
      return MakeClipboardFailure(errorCode);
    }

    std::memcpy(lock.data(), text.c_str(), byteCount);
  }

  if (SetClipboardData(CF_UNICODETEXT, handle) == nullptr) {
    const DWORD errorCode = GetLastError();
    GlobalFree(handle);
    return MakeClipboardFailure(errorCode);
  }

  return MakeClipboardSuccess();
}
