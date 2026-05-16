#pragma once

#include <windows.h>

#include <shellapi.h>

enum class TrayMenuCommand : UINT {
  kNone = 0,
  kRewriteSelection = 1001,
  kSettings = 1002,
  kAbout = 1003,
  kExit = 1004,
};

class TrayIcon {
 public:
  static constexpr UINT kCallbackMessage = WM_APP + 1;

  TrayIcon();
  ~TrayIcon();

  [[nodiscard]] bool Create(HWND windowHandle, HINSTANCE instanceHandle);
  [[nodiscard]] bool ReAdd();
  [[nodiscard]] TrayMenuCommand ShowContextMenu(HWND ownerWindow) const;
  void Remove();

 private:
  [[nodiscard]] bool AddIcon(DWORD operation);
  [[nodiscard]] bool EnsureMenu();

  NOTIFYICONDATAW notifyIconData_{};
  HMENU menuHandle_ = nullptr;
  bool iconAdded_ = false;
};
