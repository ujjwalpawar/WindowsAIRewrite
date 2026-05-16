#include "RewriteController.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "AppError.h"
#include "ClipboardManager.h"
#include "CredentialStore.h"
#include "OpenAIClient.h"
#include "PreviewWindow.h"
#include "SafeLog.h"
#include "SettingsStore.h"
#include "TextCaptureService.h"
#include "UserMessages.h"

namespace {

constexpr wchar_t kAppTitle[] = L"Windows AI Rewrite";
constexpr wchar_t kGeneratingRewriteText[] = L"Generating rewrite...";
constexpr DWORD kPasteRestoreDelayMs = 120;

enum class WorkerStage {
  kCaptureCompleted,
  kRewriteCompleted,
};

struct WorkerResult {
  WorkerStage stage = WorkerStage::kCaptureCompleted;
  bool success = false;
  bool shouldOpenSettings = false;
  bool restoreClipboard = true;
  HWND targetWindow = nullptr;
  std::wstring selectedText{};
  std::wstring rewrittenText{};
  AppError error{
    .code = AppErrorCode::kUnexpectedInternalError,
    .hresult = S_OK,
    .win32Error = ERROR_SUCCESS,
  };
};

struct EffectiveSettings {
  AppSettings settings = SettingsStore::DefaultSettings();
};

AppError MakeError(AppErrorCode code, HRESULT hresult, DWORD win32Error) {
  return {
    .code = code,
    .hresult = hresult,
    .win32Error = win32Error,
  };
}

WorkerResult MakeFailure(
  WorkerStage stage,
  AppError error,
  bool restoreClipboard,
  HWND targetWindow,
  bool shouldOpenSettings = false) {
  return {
    .stage = stage,
    .success = false,
    .shouldOpenSettings = shouldOpenSettings,
    .restoreClipboard = restoreClipboard,
    .targetWindow = targetWindow,
    .selectedText = {},
    .rewrittenText = {},
    .error = error,
  };
}

WorkerResult MakeCaptureSuccess(std::wstring selectedText, bool restoreClipboard, HWND targetWindow) {
  return {
    .stage = WorkerStage::kCaptureCompleted,
    .success = true,
    .shouldOpenSettings = false,
    .restoreClipboard = restoreClipboard,
    .targetWindow = targetWindow,
    .selectedText = std::move(selectedText),
    .rewrittenText = {},
    .error = MakeError(AppErrorCode::kUnexpectedInternalError, S_OK, ERROR_SUCCESS),
  };
}

WorkerResult MakeRewriteSuccess(std::wstring rewrittenText, bool restoreClipboard, HWND targetWindow) {
  return {
    .stage = WorkerStage::kRewriteCompleted,
    .success = true,
    .shouldOpenSettings = false,
    .restoreClipboard = restoreClipboard,
    .targetWindow = targetWindow,
    .selectedText = {},
    .rewrittenText = std::move(rewrittenText),
    .error = MakeError(AppErrorCode::kUnexpectedInternalError, S_OK, ERROR_SUCCESS),
  };
}

EffectiveSettings LoadEffectiveSettings() {
  EffectiveSettings effective{};
  const SettingsLoadResult loadResult = SettingsStore{}.Load();
  if (loadResult.status == SettingsStoreStatus::kSuccess) {
    effective.settings = loadResult.settings;
  }

  if (effective.settings.hotkey.empty()) {
    effective.settings.hotkey = SettingsStore::DefaultSettings().hotkey;
  }
  if (effective.settings.model.empty()) {
    effective.settings.model = SettingsStore::DefaultSettings().model;
  }
  if (effective.settings.requestTimeoutSeconds <= 0) {
    effective.settings.requestTimeoutSeconds = SettingsStore::DefaultSettings().requestTimeoutSeconds;
  }

  return effective;
}

AppError CredentialErrorFromReadResult(const CredentialReadResult& credentialResult) {
  switch (credentialResult.status) {
    case CredentialStoreStatus::kNotFound:
      return MakeError(
        AppErrorCode::kMissingCredential,
        HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
        ERROR_NOT_FOUND);
    case CredentialStoreStatus::kInvalidArgument:
      return MakeError(
        AppErrorCode::kUnexpectedInternalError,
        HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER),
        ERROR_INVALID_PARAMETER);
    case CredentialStoreStatus::kInvalidData:
      return MakeError(
        AppErrorCode::kUnexpectedInternalError,
        HRESULT_FROM_WIN32(ERROR_INVALID_DATA),
        ERROR_INVALID_DATA);
    case CredentialStoreStatus::kPlatformError:
      return MakeError(
        AppErrorCode::kUnexpectedInternalError,
        HRESULT_FROM_WIN32(credentialResult.errorCode),
        credentialResult.errorCode);
    case CredentialStoreStatus::kSuccess:
    default:
      return MakeError(AppErrorCode::kUnexpectedInternalError, S_OK, ERROR_SUCCESS);
  }
}

UserMessageId MessageIdForError(AppErrorCode code) {
  switch (code) {
    case AppErrorCode::kEmptySelection:
      return UserMessageId::kEmptySelection;
    case AppErrorCode::kUnsupportedSelection:
      return UserMessageId::kUnsupportedSelection;
    case AppErrorCode::kElevatedTargetUnsupported:
      return UserMessageId::kElevatedTargetUnsupported;
    case AppErrorCode::kSecureDesktopUnsupported:
      return UserMessageId::kSecureDesktopUnsupported;
    case AppErrorCode::kPasswordFieldUnsupported:
      return UserMessageId::kPasswordFieldUnsupported;
    case AppErrorCode::kClipboardBusy:
      return UserMessageId::kClipboardBusy;
    case AppErrorCode::kMissingCredential:
      return UserMessageId::kMissingCredential;
    case AppErrorCode::kAuthFailed:
      return UserMessageId::kAuthFailed;
    case AppErrorCode::kNetworkTimeout:
      return UserMessageId::kNetworkTimeout;
    case AppErrorCode::kOpenAIError:
      return UserMessageId::kOpenAIError;
    case AppErrorCode::kHotkeyConflict:
      return UserMessageId::kHotkeyConflict;
    case AppErrorCode::kAlreadyRunning:
      return UserMessageId::kAlreadyRunning;
    case AppErrorCode::kUnexpectedInternalError:
    default:
      return UserMessageId::kUnexpectedInternalError;
  }
}

SafeLogLabel LogLabelForError(AppErrorCode code) {
  switch (code) {
    case AppErrorCode::kEmptySelection:
    case AppErrorCode::kUnsupportedSelection:
    case AppErrorCode::kElevatedTargetUnsupported:
    case AppErrorCode::kSecureDesktopUnsupported:
    case AppErrorCode::kPasswordFieldUnsupported:
      return SafeLogLabel::kSelection;
    case AppErrorCode::kClipboardBusy:
      return SafeLogLabel::kClipboard;
    case AppErrorCode::kMissingCredential:
    case AppErrorCode::kAuthFailed:
      return SafeLogLabel::kCredential;
    case AppErrorCode::kNetworkTimeout:
      return SafeLogLabel::kNetwork;
    case AppErrorCode::kOpenAIError:
      return SafeLogLabel::kOpenAI;
    case AppErrorCode::kHotkeyConflict:
      return SafeLogLabel::kHotkey;
    case AppErrorCode::kAlreadyRunning:
      return SafeLogLabel::kStartup;
    case AppErrorCode::kUnexpectedInternalError:
    default:
      return SafeLogLabel::kInternal;
  }
}

AppError MapPasteFailure(DWORD win32Error) {
  if (win32Error == ERROR_ACCESS_DENIED) {
    return MakeError(
      AppErrorCode::kElevatedTargetUnsupported,
      HRESULT_FROM_WIN32(win32Error),
      win32Error);
  }

  return MakeError(
    AppErrorCode::kUnsupportedSelection,
    HRESULT_FROM_WIN32(win32Error),
    win32Error);
}

bool SendPasteShortcut(AppError* error) {
  constexpr UINT kPasteInputCount = 4;

  if (error == nullptr) {
    return false;
  }

  INPUT inputs[4]{};
  inputs[0].type = INPUT_KEYBOARD;
  inputs[0].ki.wVk = VK_CONTROL;
  inputs[1].type = INPUT_KEYBOARD;
  inputs[1].ki.wVk = 'V';
  inputs[2].type = INPUT_KEYBOARD;
  inputs[2].ki.wVk = 'V';
  inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
  inputs[3].type = INPUT_KEYBOARD;
  inputs[3].ki.wVk = VK_CONTROL;
  inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

  SetLastError(ERROR_SUCCESS);
  const UINT sent = SendInput(kPasteInputCount, inputs, sizeof(INPUT));
  if (sent == kPasteInputCount) {
    *error = MakeError(AppErrorCode::kUnexpectedInternalError, S_OK, ERROR_SUCCESS);
    return true;
  }

  DWORD errorCode = GetLastError();
  if (errorCode == ERROR_SUCCESS) {
    errorCode = ERROR_GEN_FAILURE;
  }
  *error = MapPasteFailure(errorCode);
  return false;
}

void LogError(AppError error) {
  static_cast<void>(WriteSafeLog(
    SafeLogEvent::kError,
    static_cast<int>(error.code),
    error.hresult,
    error.win32Error,
    LogLabelForError(error.code)));
}

void ShowErrorMessageBox(HWND ownerWindow, AppError error) {
  LogError(error);
  MessageBoxW(
    ownerWindow,
    UserMessage(MessageIdForError(error.code)),
    kAppTitle,
    MB_ICONWARNING | MB_OK);
}

void LogClipboardRestoreResult(const ClipboardOperationResult& restoreResult) {
  static_cast<void>(WriteSafeLog(
    SafeLogEvent::kClipboardRestore,
    restoreResult.success ? 1 : static_cast<int>(restoreResult.error.code),
    restoreResult.error.hresult,
    restoreResult.error.win32Error,
    SafeLogLabel::kClipboard));
}

void BestEffortRestoreClipboard(const ClipboardManager& clipboardManager, const ClipboardSnapshot& snapshot) {
  const ClipboardOperationResult restoreResult = clipboardManager.Restore(snapshot);
  LogClipboardRestoreResult(restoreResult);
}

void PrepareTargetForPaste(HWND targetWindow) {
  if (targetWindow == nullptr || !IsWindow(targetWindow)) {
    return;
  }

  if (IsIconic(targetWindow) == TRUE) {
    ShowWindow(targetWindow, SW_RESTORE);
  }

  SetForegroundWindow(targetWindow);
  BringWindowToTop(targetWindow);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

}  // namespace

RewriteController::RewriteController() = default;

RewriteController::RewriteController(
  HINSTANCE instanceHandle,
  ShowSettingsCallback showSettingsCallback)
  : instanceHandle_(instanceHandle), showSettingsCallback_(std::move(showSettingsCallback)) {}

RewriteController::~RewriteController() {
  if (workerThread_.joinable()) {
    workerThread_.join();
  }
}

void RewriteController::RequestRewrite(HWND ownerWindow) {
  if (ownerWindow == nullptr) {
    return;
  }

  bool expected = false;
  if (!sharedState_->inFlight.compare_exchange_strong(expected, true)) {
    return;
  }

  JoinFinishedWorker();

  const HWND targetWindow = GetForegroundWindow();

  static_cast<void>(WriteSafeLog(
    SafeLogEvent::kRewriteRequest,
    0,
    S_OK,
    ERROR_SUCCESS,
    SafeLogLabel::kSelection));

  std::shared_ptr<SharedState> sharedState = sharedState_;
  workerThread_ = std::thread([ownerWindow, sharedState, targetWindow]() {
    const auto postWorkerResult = [ownerWindow, sharedState](WorkerResult workerResult) {
      auto* postedResult = new WorkerResult(std::move(workerResult));
      if (PostMessageW(
            ownerWindow,
            RewriteController::kWorkerCompletedMessage,
            0,
            reinterpret_cast<LPARAM>(postedResult)) != FALSE) {
        return true;
      }

      delete postedResult;
      sharedState->inFlight.store(false);
      return false;
    };

    const EffectiveSettings effectiveSettings = LoadEffectiveSettings();
    const bool restoreClipboard = effectiveSettings.settings.restoreClipboard;

    const CredentialReadResult credentialResult = CredentialStore{}.Read();
    if (credentialResult.status != CredentialStoreStatus::kSuccess || credentialResult.value.empty()) {
      const AppError error =
        credentialResult.status == CredentialStoreStatus::kSuccess && credentialResult.value.empty()
          ? MakeError(
              AppErrorCode::kMissingCredential,
              HRESULT_FROM_WIN32(ERROR_NOT_FOUND),
              ERROR_NOT_FOUND)
          : CredentialErrorFromReadResult(credentialResult);
      postWorkerResult(MakeFailure(
        WorkerStage::kCaptureCompleted,
        error,
        restoreClipboard,
        targetWindow,
        error.code == AppErrorCode::kMissingCredential));
      return;
    }

    const TextCaptureResult captureResult =
      TextCaptureService{}.CaptureSelectedText(restoreClipboard);
    if (!captureResult.success) {
      postWorkerResult(MakeFailure(
        WorkerStage::kCaptureCompleted,
        captureResult.error,
        restoreClipboard,
        targetWindow));
      return;
    }

    std::wstring apiKey = credentialResult.value;
    if (!postWorkerResult(MakeCaptureSuccess(captureResult.text, restoreClipboard, targetWindow))) {
      std::fill(apiKey.begin(), apiKey.end(), L'\0');
      return;
    }

    const OpenAIRewriteResult rewriteResult =
      OpenAIClient{}.RewriteText(captureResult.text, apiKey);
    std::fill(apiKey.begin(), apiKey.end(), L'\0');

    if (!rewriteResult.success) {
      postWorkerResult(MakeFailure(
        WorkerStage::kRewriteCompleted,
        rewriteResult.error,
        restoreClipboard,
        targetWindow));
      return;
    }

    postWorkerResult(MakeRewriteSuccess(rewriteResult.rewrittenText, restoreClipboard, targetWindow));
  });
}

void RewriteController::HandleWorkerCompleted(HWND ownerWindow, LPARAM lparam) {
  std::unique_ptr<WorkerResult> workerResult(reinterpret_cast<WorkerResult*>(lparam));
  if (!workerResult) {
    CompleteOperation();
    return;
  }

  if (workerResult->stage == WorkerStage::kRewriteCompleted) {
    rewriteCompleted_ = true;
    if (!workerResult->success) {
      if (previewCanceled_) {
        LogError(workerResult->error);
        CompleteOperation();
        return;
      }
      if (activePreviewWindow_ != nullptr) {
        LogError(workerResult->error);
        activePreviewWindow_->SetRewriteError(UserMessage(MessageIdForError(workerResult->error.code)));
        return;
      }
      ShowErrorMessageBox(ownerWindow, workerResult->error);
      CompleteOperation();
      return;
    }

    if (previewCanceled_) {
      CompleteOperation();
      return;
    }
    if (activePreviewWindow_ != nullptr) {
      activePreviewWindow_->SetRewriteText(std::move(workerResult->rewrittenText));
      return;
    }

    CompleteOperation();
    return;
  }

  if (!workerResult->success) {
    ShowErrorMessageBox(ownerWindow, workerResult->error);
    if (workerResult->shouldOpenSettings && showSettingsCallback_) {
      showSettingsCallback_();
    }
    CompleteOperation();
    return;
  }

  previewCanceled_ = false;
  rewriteCompleted_ = false;

  PreviewWindow previewWindow(instanceHandle_);
  activePreviewWindow_ = &previewWindow;
  const PreviewWindowResult previewResult = previewWindow.Show(
    ownerWindow,
    workerResult->selectedText,
    kGeneratingRewriteText,
    false);
  activePreviewWindow_ = nullptr;

  if (previewResult.action == PreviewWindowAction::kCancel) {
    previewCanceled_ = true;
    if (rewriteCompleted_) {
      CompleteOperation();
    }
    return;
  }

  ClipboardManager clipboardManager;
  switch (previewResult.action) {
    case PreviewWindowAction::kCancel:
      CompleteOperation();
      return;
    case PreviewWindowAction::kCopy: {
      const ClipboardOperationResult copyResult =
        clipboardManager.WriteUnicodeText(previewResult.rewrittenText);
      if (!copyResult.success) {
        ShowErrorMessageBox(ownerWindow, copyResult.error);
      }
      CompleteOperation();
      return;
    }
    case PreviewWindowAction::kReplace:
      break;
  }

  ClipboardSnapshot snapshot{};
  bool snapshotCaptured = false;
  if (workerResult->restoreClipboard) {
    const ClipboardOperationResult snapshotResult = clipboardManager.Snapshot(&snapshot);
    if (!snapshotResult.success) {
      ShowErrorMessageBox(ownerWindow, snapshotResult.error);
      CompleteOperation();
      return;
    }
    snapshotCaptured = true;
  }

  const ClipboardOperationResult writeResult =
    clipboardManager.WriteUnicodeText(previewResult.rewrittenText);
  if (!writeResult.success) {
    if (snapshotCaptured) {
      BestEffortRestoreClipboard(clipboardManager, snapshot);
    }
    ShowErrorMessageBox(ownerWindow, writeResult.error);
    CompleteOperation();
    return;
  }

  PrepareTargetForPaste(workerResult->targetWindow);

  AppError pasteError{};
  if (!SendPasteShortcut(&pasteError)) {
    if (snapshotCaptured) {
      BestEffortRestoreClipboard(clipboardManager, snapshot);
    }
    ShowErrorMessageBox(ownerWindow, pasteError);
    CompleteOperation();
    return;
  }

  if (snapshotCaptured) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kPasteRestoreDelayMs));
    const ClipboardOperationResult restoreResult = clipboardManager.Restore(snapshot);
    LogClipboardRestoreResult(restoreResult);
    if (!restoreResult.success) {
      ShowErrorMessageBox(ownerWindow, restoreResult.error);
    }
  }

  CompleteOperation();
}

bool RewriteController::IsOperationInFlight() const {
  return sharedState_->inFlight.load();
}

void RewriteController::CompleteOperation() {
  sharedState_->inFlight.store(false);
  JoinFinishedWorker();
}

void RewriteController::JoinFinishedWorker() {
  if (workerThread_.joinable()) {
    workerThread_.join();
  }
}
