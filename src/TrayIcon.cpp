#include "TrayIcon.h"

#include <cwchar>

namespace {

constexpr UINT kTrayIconId = 1;
constexpr int kIconResourceId = 101;
constexpr wchar_t kTooltip[] = L"Windows AI Rewrite";

}  // namespace

TrayIcon::TrayIcon() {
  notifyIconData_.cbSize = sizeof(notifyIconData_);
  notifyIconData_.uID = kTrayIconId;
  notifyIconData_.uCallbackMessage = kCallbackMessage;
  notifyIconData_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  wcscpy_s(notifyIconData_.szTip, kTooltip);
}

TrayIcon::~TrayIcon() {
  Remove();
  if (menuHandle_ != nullptr) {
    DestroyMenu(menuHandle_);
    menuHandle_ = nullptr;
  }
}

bool TrayIcon::Create(HWND windowHandle, HINSTANCE instanceHandle) {
  notifyIconData_.hWnd = windowHandle;
  notifyIconData_.hIcon = LoadIconW(instanceHandle, MAKEINTRESOURCEW(kIconResourceId));
  if (notifyIconData_.hIcon == nullptr) {
    notifyIconData_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  }

  return AddIcon(NIM_ADD) && EnsureMenu();
}

bool TrayIcon::ReAdd() {
  return AddIcon(NIM_ADD);
}

TrayMenuCommand TrayIcon::ShowContextMenu(HWND ownerWindow) const {
  if (menuHandle_ == nullptr || ownerWindow == nullptr) {
    return TrayMenuCommand::kNone;
  }

  POINT cursorPosition{};
  if (GetCursorPos(&cursorPosition) != TRUE) {
    return TrayMenuCommand::kNone;
  }

  SetForegroundWindow(ownerWindow);
  const UINT command = TrackPopupMenuEx(
    menuHandle_,
    TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON,
    cursorPosition.x,
    cursorPosition.y,
    ownerWindow,
    nullptr);
  PostMessageW(ownerWindow, WM_NULL, 0, 0);
  return static_cast<TrayMenuCommand>(command);
}

void TrayIcon::Remove() {
  if (!iconAdded_) {
    return;
  }

  Shell_NotifyIconW(NIM_DELETE, &notifyIconData_);
  iconAdded_ = false;
}

bool TrayIcon::AddIcon(DWORD operation) {
  if (notifyIconData_.hWnd == nullptr) {
    return false;
  }

  if (Shell_NotifyIconW(operation, &notifyIconData_) != TRUE) {
    return false;
  }

  notifyIconData_.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &notifyIconData_);
  iconAdded_ = true;
  return true;
}

bool TrayIcon::EnsureMenu() {
  if (menuHandle_ != nullptr) {
    return true;
  }

  menuHandle_ = CreatePopupMenu();
  if (menuHandle_ == nullptr) {
    return false;
  }

  if (AppendMenuW(
        menuHandle_,
        MF_STRING,
        static_cast<UINT_PTR>(TrayMenuCommand::kRewriteSelection),
        L"Rewrite Selection") != TRUE) {
    return false;
  }

  if (AppendMenuW(
        menuHandle_,
        MF_STRING,
        static_cast<UINT_PTR>(TrayMenuCommand::kSettings),
        L"Settings") != TRUE) {
    return false;
  }

  if (AppendMenuW(menuHandle_, MF_SEPARATOR, 0, nullptr) != TRUE) {
    return false;
  }

  if (AppendMenuW(
        menuHandle_,
        MF_STRING,
        static_cast<UINT_PTR>(TrayMenuCommand::kAbout),
        L"About") != TRUE) {
    return false;
  }

  if (AppendMenuW(menuHandle_, MF_SEPARATOR, 0, nullptr) != TRUE) {
    return false;
  }

  return AppendMenuW(
           menuHandle_,
           MF_STRING,
           static_cast<UINT_PTR>(TrayMenuCommand::kExit),
           L"Exit") == TRUE;
}
