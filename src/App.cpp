#include "App.h"

#include <memory>

#include "CredentialStore.h"
#include "RewriteController.h"
#include "UserMessages.h"

namespace {

constexpr wchar_t kAppTitle[] = L"Windows AI Rewrite";

}  // namespace

App::App(HINSTANCE instanceHandle) : instanceHandle_(instanceHandle) {
  rewriteController_ = std::make_unique<RewriteController>(instanceHandle_, [this]() {
    ShowSettingsDialog();
  });
}

App::~App() {
  Shutdown(false);
  if (windowClassAtom_ != 0) {
    UnregisterClassW(kWindowClassName, instanceHandle_);
  }
}

int App::Run() {
  if (!Initialize()) {
    Shutdown(windowHandle_ != nullptr);
    return startupStatus_ == AppStartupStatus::kAlreadyRunning ? 0 : 1;
  }

  return RunMessageLoop();
}

bool App::Initialize() {
  if (!AcquireSingleInstance()) {
    return false;
  }

  const SettingsLoadResult loadResult = settingsStore_.Load();
  settings_ = loadResult.settings;

  taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
  if (taskbarCreatedMessage_ == 0) {
    startupStatus_ = AppStartupStatus::kStartupFailed;
    return false;
  }

  if (!RegisterWindowClass() || !CreateHiddenWindow() || !InitializeTrayIcon()) {
    startupStatus_ = AppStartupStatus::kStartupFailed;
    return false;
  }

  InitializeHotkey();
  ShowFirstRunSetupIfNeeded();
  return true;
}

bool App::AcquireSingleInstance() {
  singleInstanceMutex_ = CreateMutexW(nullptr, TRUE, kSingleInstanceMutexName);
  if (singleInstanceMutex_ == nullptr) {
    startupStatus_ = AppStartupStatus::kStartupFailed;
    return false;
  }

  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    startupStatus_ = AppStartupStatus::kAlreadyRunning;
    ShowStatusDialog(UserMessage(UserMessageId::kAlreadyRunning), MB_ICONINFORMATION | MB_OK);
    ReleaseSingleInstance();
    return false;
  }

  ownsSingleInstanceMutex_ = true;
  return true;
}

bool App::RegisterWindowClass() {
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = &App::WindowProc;
  windowClass.hInstance = instanceHandle_;
  windowClass.lpszClassName = kWindowClassName;

  windowClassAtom_ = RegisterClassExW(&windowClass);
  return windowClassAtom_ != 0;
}

bool App::CreateHiddenWindow() {
  windowHandle_ = CreateWindowExW(
    0,
    kWindowClassName,
    kAppTitle,
    WS_OVERLAPPED,
    0,
    0,
    0,
    0,
    nullptr,
    nullptr,
    instanceHandle_,
    this);
  return windowHandle_ != nullptr;
}

bool App::InitializeTrayIcon() {
  return trayIcon_.Create(windowHandle_, instanceHandle_);
}

void App::InitializeHotkey() {
  const std::wstring configuredHotkey = settings_.hotkey;
  const std::wstring& fallbackHotkey = HotkeyManager::DefaultHotkeyText();
  if (configuredHotkey.empty()) {
    const HotkeyRegistrationResult fallbackResult = hotkeyManager_.Register(windowHandle_, fallbackHotkey);
    if (fallbackResult.status == HotkeyRegistrationStatus::kSuccess) {
      settings_.hotkey = fallbackHotkey;
      startupStatus_ = AppStartupStatus::kReady;
      return;
    }

    startupStatus_ = fallbackResult.status == HotkeyRegistrationStatus::kInvalidShortcut
                       ? AppStartupStatus::kInvalidHotkey
                       : AppStartupStatus::kHotkeyConflict;
    const std::wstring message = fallbackResult.status == HotkeyRegistrationStatus::kInvalidShortcut
                                   ? L"The configured hotkey is invalid. Update the settings file to use a shortcut like Ctrl+Alt+Shift+R."
                                   : UserMessage(UserMessageId::kHotkeyConflict);
    ShowStatusDialog(message, MB_ICONWARNING | MB_OK);
    return;
  }

  HotkeyRegistrationResult registrationResult = hotkeyManager_.Register(windowHandle_, configuredHotkey);

  if (registrationResult.status == HotkeyRegistrationStatus::kInvalidShortcut &&
      configuredHotkey != fallbackHotkey) {
    const HotkeyRegistrationResult fallbackResult = hotkeyManager_.Register(windowHandle_, fallbackHotkey);
    if (fallbackResult.status == HotkeyRegistrationStatus::kSuccess) {
      settings_.hotkey = fallbackHotkey;
      startupStatus_ = AppStartupStatus::kReady;
      return;
    }

    registrationResult = fallbackResult;
  }

  if (registrationResult.status == HotkeyRegistrationStatus::kSuccess) {
    startupStatus_ = AppStartupStatus::kReady;
    return;
  }

  startupStatus_ = registrationResult.status == HotkeyRegistrationStatus::kInvalidShortcut
                     ? AppStartupStatus::kInvalidHotkey
                     : AppStartupStatus::kHotkeyConflict;

  const std::wstring message = registrationResult.status == HotkeyRegistrationStatus::kInvalidShortcut
                                 ? L"The configured hotkey is invalid. Update the settings file to use a shortcut like Ctrl+Alt+Shift+R."
                                 : UserMessage(UserMessageId::kHotkeyConflict);

  ShowStatusDialog(message, MB_ICONWARNING | MB_OK);
}

int App::RunMessageLoop() const {
  MSG message{};

  while (true) {
    const BOOL result = GetMessageW(&message, nullptr, 0, 0);
    if (result == -1) {
      return 1;
    }

    if (result == 0) {
      return static_cast<int>(message.wParam);
    }

    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
}

void App::Shutdown(bool destroyWindow) {
  if (shuttingDown_) {
    return;
  }

  shuttingDown_ = true;

  if (windowHandle_ != nullptr) {
    hotkeyManager_.Unregister(windowHandle_);
    trayIcon_.Remove();
  }

  ReleaseSingleInstance();

  if (destroyWindow && windowHandle_ != nullptr) {
    HWND windowToDestroy = windowHandle_;
    windowHandle_ = nullptr;
    DestroyWindow(windowToDestroy);
  }
}

void App::ReleaseSingleInstance() {
  if (singleInstanceMutex_ == nullptr) {
    return;
  }

  if (ownsSingleInstanceMutex_) {
    ReleaseMutex(singleInstanceMutex_);
    ownsSingleInstanceMutex_ = false;
  }

  CloseHandle(singleInstanceMutex_);
  singleInstanceMutex_ = nullptr;
}

void App::ShowStatusDialog(const std::wstring& message, UINT type) const {
  MessageBoxW(windowHandle_, message.c_str(), kAppTitle, type);
}

void App::ShowAboutDialog() const {
  ShowStatusDialog(L"Windows AI Rewrite\nVersion 0.1.0", MB_ICONINFORMATION | MB_OK);
}

void App::StartRewriteSelection() {
  if (rewriteController_ == nullptr || windowHandle_ == nullptr) {
    return;
  }

  rewriteController_->RequestRewrite(windowHandle_);
}

void App::ShowFirstRunSetupIfNeeded() {
  if (CredentialStore{}.Exists()) {
    return;
  }

  SettingsWindow settingsWindow(instanceHandle_, settingsStore_, CredentialStore{});
  settingsWindow.ShowFirstRunSetup(windowHandle_);
}

void App::ShowSettingsDialog() {
  SettingsWindow settingsWindow(instanceHandle_, settingsStore_, CredentialStore{});
  const SettingsWindowResult result = settingsWindow.ShowSettings(windowHandle_);
  if (result.action != SettingsWindowAction::kSaved) {
    return;
  }

  settings_ = result.settings;
  InitializeHotkey();
}

void App::HandleTrayCommand(TrayMenuCommand command) {
  switch (command) {
    case TrayMenuCommand::kRewriteSelection:
      StartRewriteSelection();
      return;
    case TrayMenuCommand::kSettings:
      ShowSettingsDialog();
      return;
    case TrayMenuCommand::kAbout:
      ShowAboutDialog();
      return;
    case TrayMenuCommand::kExit:
      Shutdown(true);
      return;
    case TrayMenuCommand::kNone:
    default:
      return;
  }
}

LRESULT App::HandleMessage(HWND windowHandle, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == taskbarCreatedMessage_) {
    trayIcon_.ReAdd();
    return 0;
  }

  if (message == RewriteController::kWorkerCompletedMessage) {
    if (rewriteController_ != nullptr) {
      rewriteController_->HandleWorkerCompleted(windowHandle, lparam);
    }
    return 0;
  }

  switch (message) {
    case WM_CLOSE:
      Shutdown(true);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_NCDESTROY:
      windowHandle_ = nullptr;
      break;
    case WM_HOTKEY:
      if (hotkeyManager_.IsHotkeyMessage(wparam)) {
        StartRewriteSelection();
        return 0;
      }
      break;
    default:
      break;
  }

  if (message == TrayIcon::kCallbackMessage) {
    const UINT trayNotification = LOWORD(static_cast<DWORD>(lparam));

    if (lparam == WM_CONTEXTMENU || lparam == WM_RBUTTONUP ||
        trayNotification == WM_CONTEXTMENU || trayNotification == WM_RBUTTONUP) {
      HandleTrayCommand(trayIcon_.ShowContextMenu(windowHandle));
      return 0;
    }

    if (lparam == WM_LBUTTONDBLCLK || trayNotification == WM_LBUTTONDBLCLK) {
      StartRewriteSelection();
      return 0;
    }
  }

  return DefWindowProcW(windowHandle, message, wparam, lparam);
}

LRESULT CALLBACK App::WindowProc(
  HWND windowHandle,
  UINT message,
  WPARAM wparam,
  LPARAM lparam) {
  App* app = nullptr;

  if (message == WM_NCCREATE) {
    const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    app = static_cast<App*>(createStruct->lpCreateParams);
    SetWindowLongPtrW(windowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    if (app != nullptr) {
      app->windowHandle_ = windowHandle;
    }
  } else {
    app = reinterpret_cast<App*>(GetWindowLongPtrW(windowHandle, GWLP_USERDATA));
  }

  if (app == nullptr) {
    return DefWindowProcW(windowHandle, message, wparam, lparam);
  }

  const LRESULT result = app->HandleMessage(windowHandle, message, wparam, lparam);
  if (message == WM_NCDESTROY) {
    SetWindowLongPtrW(windowHandle, GWLP_USERDATA, 0);
    app->windowHandle_ = nullptr;
  }

  return result;
}
