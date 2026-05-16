#pragma once

#include <windows.h>

#include <string>

struct HotkeyBinding {
  UINT modifiers = 0;
  UINT virtualKey = 0;
  std::wstring displayText{};
};

enum class HotkeyRegistrationStatus {
  kSuccess,
  kInvalidShortcut,
  kRegistrationFailed,
};

struct HotkeyRegistrationResult {
  HotkeyRegistrationStatus status = HotkeyRegistrationStatus::kSuccess;
  DWORD errorCode = ERROR_SUCCESS;
};

 class HotkeyManager {
  public:
   static constexpr int kHotkeyId = 1;

  HotkeyManager() = default;

  [[nodiscard]] HotkeyRegistrationResult Register(
    HWND windowHandle,
    const std::wstring& hotkeyText);
  void Unregister(HWND windowHandle);

  [[nodiscard]] bool IsRegistered() const;
  [[nodiscard]] bool IsHotkeyMessage(WPARAM wparam) const;
  [[nodiscard]] const HotkeyBinding& binding() const;

  [[nodiscard]] static const std::wstring& DefaultHotkeyText();

 private:
  [[nodiscard]] static bool TryParseHotkey(
    const std::wstring& hotkeyText,
    HotkeyBinding* binding);

  static constexpr int kReplacementHotkeyId = 2;

  HotkeyBinding binding_{};
  bool registered_ = false;
};
