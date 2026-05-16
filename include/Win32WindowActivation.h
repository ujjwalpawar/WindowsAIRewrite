#pragma once

#include <windows.h>

namespace Win32WindowActivation {

inline void FlashWindowForAttention(HWND windowHandle) {
  FLASHWINFO flashInfo{};
  flashInfo.cbSize = sizeof(flashInfo);
  flashInfo.hwnd = windowHandle;
  flashInfo.dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG;
  flashInfo.uCount = 3;
  flashInfo.dwTimeout = 0;
  FlashWindowEx(&flashInfo);
}

inline void ShowAndActivateWindow(HWND windowHandle) {
  if (windowHandle == nullptr) {
    return;
  }

  ShowWindow(windowHandle, SW_SHOWNORMAL);
  SetWindowPos(
    windowHandle,
    HWND_TOPMOST,
    0,
    0,
    0,
    0,
    SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
  SetWindowPos(
    windowHandle,
    HWND_NOTOPMOST,
    0,
    0,
    0,
    0,
    SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
  BringWindowToTop(windowHandle);
  SetActiveWindow(windowHandle);

  if (SetForegroundWindow(windowHandle) != TRUE && GetForegroundWindow() != windowHandle) {
    FlashWindowForAttention(windowHandle);
  }
}

}  // namespace Win32WindowActivation
