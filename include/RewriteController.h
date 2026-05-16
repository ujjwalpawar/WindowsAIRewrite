#pragma once

#include <windows.h>

#include <atomic>
#include <functional>
#include <memory>
#include <thread>

class PreviewWindow;

class RewriteController {
 public:
  using ShowSettingsCallback = std::function<void()>;

  static constexpr UINT kWorkerCompletedMessage = WM_APP + 101;

  RewriteController();
  RewriteController(HINSTANCE instanceHandle, ShowSettingsCallback showSettingsCallback);
  RewriteController(const RewriteController&) = delete;
  RewriteController& operator=(const RewriteController&) = delete;
  ~RewriteController();

  void RequestRewrite(HWND ownerWindow);
  void HandleWorkerCompleted(HWND ownerWindow, LPARAM lparam);
  [[nodiscard]] bool IsOperationInFlight() const;

 private:
  struct SharedState {
    std::atomic_bool inFlight = false;
  };

  void CompleteOperation();
  void JoinFinishedWorker();

  HINSTANCE instanceHandle_ = nullptr;
  ShowSettingsCallback showSettingsCallback_{};
  std::shared_ptr<SharedState> sharedState_ = std::make_shared<SharedState>();
  std::thread workerThread_{};
  PreviewWindow* activePreviewWindow_ = nullptr;
  bool previewCanceled_ = false;
  bool rewriteCompleted_ = false;
};
