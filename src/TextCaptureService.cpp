#include "TextCaptureService.h"

#include <oleauto.h>
#include <uiautomation.h>

#include <future>
#include <string>
#include <thread>
#include <utility>

#include "SafeLog.h"

namespace {

constexpr int kClipboardPollAttempts = 12;
constexpr DWORD kClipboardPollDelayMs = 50;

TextCaptureResult MakeCaptureSuccess(std::wstring text) {
  return {
    .success = true,
    .text = std::move(text),
    .error = {
      .code = AppErrorCode::kUnexpectedInternalError,
      .hresult = S_OK,
      .win32Error = ERROR_SUCCESS,
    },
  };
}

TextCaptureResult MakeCaptureFailure(AppErrorCode code, HRESULT hresult, DWORD win32Error) {
  return {
    .success = false,
    .text = {},
    .error = {
      .code = code,
      .hresult = hresult,
      .win32Error = win32Error,
    },
  };
}

bool IsAccessDenied(HRESULT hresult, DWORD win32Error) {
  return hresult == E_ACCESSDENIED || hresult == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED) ||
         win32Error == ERROR_ACCESS_DENIED;
}

TextCaptureResult MapUiAutomationFailure(HRESULT hresult) {
  if (IsAccessDenied(hresult, ERROR_SUCCESS)) {
    return MakeCaptureFailure(AppErrorCode::kElevatedTargetUnsupported, hresult, ERROR_ACCESS_DENIED);
  }

  if (hresult == UIA_E_ELEMENTNOTAVAILABLE || hresult == UIA_E_TIMEOUT) {
    return MakeCaptureFailure(AppErrorCode::kUnsupportedSelection, hresult, ERROR_SUCCESS);
  }

  return MakeCaptureFailure(AppErrorCode::kUnsupportedSelection, hresult, ERROR_SUCCESS);
}

class ComInitializationScope {
 public:
  explicit ComInitializationScope(DWORD flags) : hresult_(CoInitializeEx(nullptr, flags)) {
    shouldUninitialize_ = SUCCEEDED(hresult_);
  }

  ComInitializationScope(const ComInitializationScope&) = delete;
  ComInitializationScope& operator=(const ComInitializationScope&) = delete;

  ~ComInitializationScope() {
    if (shouldUninitialize_) {
      CoUninitialize();
    }
  }

  [[nodiscard]] bool succeeded() const {
    return SUCCEEDED(hresult_) || hresult_ == S_FALSE;
  }

  [[nodiscard]] HRESULT hresult() const {
    return hresult_;
  }

 private:
  HRESULT hresult_ = E_FAIL;
  bool shouldUninitialize_ = false;
};

template <typename T>
class ComPtr {
 public:
  ComPtr() = default;

  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;

  ComPtr(ComPtr&& other) noexcept : value_(other.value_) {
    other.value_ = nullptr;
  }

  ComPtr& operator=(ComPtr&& other) noexcept {
    if (this != &other) {
      reset();
      value_ = other.value_;
      other.value_ = nullptr;
    }

    return *this;
  }

  ~ComPtr() {
    reset();
  }

  [[nodiscard]] T* get() const {
    return value_;
  }

  [[nodiscard]] T** put() {
    reset();
    return &value_;
  }

  [[nodiscard]] IUnknown** put_unknown() {
    reset();
    return reinterpret_cast<IUnknown**>(&value_);
  }

  [[nodiscard]] T* operator->() const {
    return value_;
  }

  [[nodiscard]] explicit operator bool() const {
    return value_ != nullptr;
  }

  void reset() {
    if (value_ != nullptr) {
      value_->Release();
      value_ = nullptr;
    }
  }

 private:
  T* value_ = nullptr;
};

class BstrScope {
 public:
  BstrScope() = default;

  BstrScope(const BstrScope&) = delete;
  BstrScope& operator=(const BstrScope&) = delete;

  ~BstrScope() {
    if (value_ != nullptr) {
      SysFreeString(value_);
    }
  }

  [[nodiscard]] BSTR* put() {
    if (value_ != nullptr) {
      SysFreeString(value_);
      value_ = nullptr;
    }

    return &value_;
  }

  [[nodiscard]] std::wstring ToWString() const {
    if (value_ == nullptr) {
      return {};
    }

    return std::wstring(value_, value_ + SysStringLen(value_));
  }

 private:
  BSTR value_ = nullptr;
};

TextCaptureResult CaptureSelectedTextOnMtaThread() {
  ComInitializationScope com(COINIT_MULTITHREADED);
  if (!com.succeeded()) {
    return MapUiAutomationFailure(com.hresult());
  }

  ComPtr<IUIAutomation> automation;
  HRESULT hresult = CoCreateInstance(
    CLSID_CUIAutomation,
    nullptr,
    CLSCTX_INPROC_SERVER,
    IID_PPV_ARGS(automation.put()));
  if (FAILED(hresult) || !automation) {
    return MapUiAutomationFailure(hresult);
  }

  ComPtr<IUIAutomationElement> focusedElement;
  hresult = automation->GetFocusedElement(focusedElement.put());
  if (FAILED(hresult) || !focusedElement) {
    return MapUiAutomationFailure(hresult);
  }

  BOOL isPassword = FALSE;
  hresult = focusedElement->get_CurrentIsPassword(&isPassword);
  if (SUCCEEDED(hresult) && isPassword == TRUE) {
    return MakeCaptureFailure(AppErrorCode::kPasswordFieldUnsupported, hresult, ERROR_SUCCESS);
  }

  ComPtr<IUnknown> patternUnknown;
  hresult = focusedElement->GetCurrentPattern(UIA_TextPatternId, patternUnknown.put_unknown());
  if (hresult == E_NOINTERFACE || !patternUnknown) {
    return MakeCaptureFailure(AppErrorCode::kUnsupportedSelection, hresult, ERROR_SUCCESS);
  }

  if (FAILED(hresult)) {
    return MapUiAutomationFailure(hresult);
  }

  ComPtr<IUIAutomationTextPattern> textPattern;
  hresult = patternUnknown->QueryInterface(IID_PPV_ARGS(textPattern.put()));
  if (hresult == E_NOINTERFACE || !textPattern) {
    return MakeCaptureFailure(AppErrorCode::kUnsupportedSelection, hresult, ERROR_SUCCESS);
  }

  if (FAILED(hresult)) {
    return MapUiAutomationFailure(hresult);
  }

  SupportedTextSelection selectionSupport = SupportedTextSelection_None;
  hresult = textPattern->get_SupportedTextSelection(&selectionSupport);
  if (FAILED(hresult)) {
    return MapUiAutomationFailure(hresult);
  }

  if (selectionSupport == SupportedTextSelection_None) {
    return MakeCaptureFailure(AppErrorCode::kUnsupportedSelection, S_OK, ERROR_SUCCESS);
  }

  ComPtr<IUIAutomationTextRangeArray> selections;
  hresult = textPattern->GetSelection(selections.put());
  if (FAILED(hresult) || !selections) {
    return MapUiAutomationFailure(hresult);
  }

  int selectionCount = 0;
  hresult = selections->get_Length(&selectionCount);
  if (FAILED(hresult)) {
    return MapUiAutomationFailure(hresult);
  }

  if (selectionCount <= 0) {
    return MakeCaptureFailure(AppErrorCode::kEmptySelection, S_OK, ERROR_SUCCESS);
  }

  std::wstring combinedText;
  for (int index = 0; index < selectionCount; ++index) {
    ComPtr<IUIAutomationTextRange> range;
    hresult = selections->GetElement(index, range.put());
    if (FAILED(hresult) || !range) {
      return MapUiAutomationFailure(hresult);
    }

    BstrScope text;
    hresult = range->GetText(-1, text.put());
    if (FAILED(hresult)) {
      return MapUiAutomationFailure(hresult);
    }

    combinedText += text.ToWString();
  }

  if (combinedText.empty()) {
    return MakeCaptureFailure(AppErrorCode::kEmptySelection, S_OK, ERROR_SUCCESS);
  }

  return MakeCaptureSuccess(combinedText);
}

AppErrorCode MapSendInputFailureCode(DWORD win32Error) {
  if (win32Error == ERROR_ACCESS_DENIED) {
    return AppErrorCode::kElevatedTargetUnsupported;
  }

  return AppErrorCode::kUnsupportedSelection;
}

TextCaptureResult SendCopyShortcut() {
  constexpr UINT kInputCount = 4;
  INPUT inputs[4]{};

  inputs[0].type = INPUT_KEYBOARD;
  inputs[0].ki.wVk = VK_CONTROL;

  inputs[1].type = INPUT_KEYBOARD;
  inputs[1].ki.wVk = 'C';

  inputs[2].type = INPUT_KEYBOARD;
  inputs[2].ki.wVk = 'C';
  inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

  inputs[3].type = INPUT_KEYBOARD;
  inputs[3].ki.wVk = VK_CONTROL;
  inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

  SetLastError(ERROR_SUCCESS);
  const UINT sent = SendInput(kInputCount, inputs, sizeof(INPUT));
  if (sent == kInputCount) {
    return MakeCaptureSuccess({});
  }

  DWORD errorCode = GetLastError();
  if (errorCode == ERROR_SUCCESS) {
    errorCode = ERROR_GEN_FAILURE;
  }

  const AppErrorCode code = MapSendInputFailureCode(errorCode);
  return MakeCaptureFailure(code, HRESULT_FROM_WIN32(errorCode), errorCode);
}

}  // namespace

TextCaptureService::TextCaptureService(ClipboardManager clipboardManager)
  : clipboardManager_(std::move(clipboardManager)) {}

TextCaptureResult TextCaptureService::CaptureSelectedText(bool restoreClipboard) const {
  const TextCaptureResult uiAutomationResult = CaptureWithUiAutomation();
  if (uiAutomationResult.success) {
    return uiAutomationResult;
  }

  if (uiAutomationResult.error.code == AppErrorCode::kEmptySelection ||
      uiAutomationResult.error.code == AppErrorCode::kPasswordFieldUnsupported ||
      uiAutomationResult.error.code == AppErrorCode::kSecureDesktopUnsupported) {
    return uiAutomationResult;
  }

  const TextCaptureResult clipboardResult = CaptureWithClipboardFallback(restoreClipboard);
  if (clipboardResult.success) {
    return clipboardResult;
  }

  if (uiAutomationResult.error.code == AppErrorCode::kElevatedTargetUnsupported &&
      clipboardResult.error.code == AppErrorCode::kUnsupportedSelection) {
    return uiAutomationResult;
  }

  return clipboardResult;
}

TextCaptureResult TextCaptureService::CaptureWithUiAutomation() const {
  std::promise<TextCaptureResult> promise;
  std::future<TextCaptureResult> result = promise.get_future();

  std::thread worker([promise = std::move(promise)]() mutable {
    promise.set_value(CaptureSelectedTextOnMtaThread());
  });
  worker.join();

  return result.get();
}

TextCaptureResult TextCaptureService::CaptureWithClipboardFallback(bool restoreClipboard) const {
  ClipboardSnapshot snapshot{};
  if (restoreClipboard) {
    const ClipboardOperationResult snapshotResult = clipboardManager_.Snapshot(&snapshot);
    if (!snapshotResult.success) {
      return MakeCaptureFailure(
        snapshotResult.error.code,
        snapshotResult.error.hresult,
        snapshotResult.error.win32Error);
    }
  }

  const DWORD initialSequence = GetClipboardSequenceNumber();

  const TextCaptureResult sendInputResult = SendCopyShortcut();
  if (!sendInputResult.success) {
    return sendInputResult;
  }

  TextCaptureResult finalResult =
    MakeCaptureFailure(AppErrorCode::kUnsupportedSelection, S_OK, ERROR_SUCCESS);

  for (int attempt = 0; attempt < kClipboardPollAttempts; ++attempt) {
    Sleep(kClipboardPollDelayMs);

    const ClipboardReadResult readResult = clipboardManager_.ReadUnicodeText();
    if (!readResult.success) {
      finalResult = MakeCaptureFailure(
        readResult.error.code,
        readResult.error.hresult,
        readResult.error.win32Error);
      continue;
    }

    if (!readResult.hasText) {
      continue;
    }

    if (GetClipboardSequenceNumber() == initialSequence && readResult.text.empty()) {
      continue;
    }

    if (readResult.text.empty()) {
      finalResult = MakeCaptureFailure(AppErrorCode::kEmptySelection, S_OK, ERROR_SUCCESS);
      break;
    }

    finalResult = MakeCaptureSuccess(readResult.text);
    break;
  }

  if (restoreClipboard) {
    const ClipboardOperationResult restoreResult = clipboardManager_.Restore(snapshot);
    WriteSafeLog(
      SafeLogEvent::kClipboardRestore,
      restoreResult.success ? 1 : 0,
      restoreResult.error.hresult,
      restoreResult.error.win32Error,
      SafeLogLabel::kClipboard);

    if (!restoreResult.success && finalResult.success) {
      finalResult = MakeCaptureFailure(
        restoreResult.error.code,
        restoreResult.error.hresult,
        restoreResult.error.win32Error);
    }
  }

  return finalResult;
}
