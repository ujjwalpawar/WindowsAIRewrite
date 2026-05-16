#pragma once

#include <windows.h>

#include <memory>
#include <string>

#include "HotkeyManager.h"
#include "SettingsStore.h"
#include "SettingsWindow.h"
#include "TrayIcon.h"

class RewriteController;

enum class AppStartupStatus {
  kReady,
  kAlreadyRunning,
  kStartupFailed,
  kHotkeyConflict,
  kInvalidHotkey,
};

class App {
 public:
  explicit App(HINSTANCE instanceHandle);
  ~App();

  [[nodiscard]] int Run();

 private:
  static constexpr wchar_t kWindowClassName[] = L"WindowsAIRewriteHiddenWindow";
  static constexpr wchar_t kSingleInstanceMutexName[] =
    L"Local\\WindowsAIRewrite.SingleInstance";

  [[nodiscard]] bool Initialize();
  [[nodiscard]] bool AcquireSingleInstance();
  [[nodiscard]] bool RegisterWindowClass();
  [[nodiscard]] bool CreateHiddenWindow();
  [[nodiscard]] bool InitializeTrayIcon();
  void InitializeHotkey();
  [[nodiscard]] int RunMessageLoop() const;
  void Shutdown(bool destroyWindow);
  void ReleaseSingleInstance();
  void ShowStatusDialog(const std::wstring& message, UINT type) const;
  void ShowAboutDialog() const;
  void StartRewriteSelection();
  void ShowFirstRunSetupIfNeeded();
  void ShowSettingsDialog();
  void HandleTrayCommand(TrayMenuCommand command);

  LRESULT HandleMessage(HWND windowHandle, UINT message, WPARAM wparam, LPARAM lparam);
  static LRESULT CALLBACK WindowProc(
    HWND windowHandle,
    UINT message,
    WPARAM wparam,
    LPARAM lparam);

  HINSTANCE instanceHandle_ = nullptr;
  HWND windowHandle_ = nullptr;
  HANDLE singleInstanceMutex_ = nullptr;
  ATOM windowClassAtom_ = 0;
  UINT taskbarCreatedMessage_ = 0;
  AppStartupStatus startupStatus_ = AppStartupStatus::kReady;
  bool ownsSingleInstanceMutex_ = false;
  bool shuttingDown_ = false;
  SettingsStore settingsStore_{};
  AppSettings settings_ = SettingsStore::DefaultSettings();
  HotkeyManager hotkeyManager_{};
  TrayIcon trayIcon_{};
  std::unique_ptr<RewriteController> rewriteController_{};
};
