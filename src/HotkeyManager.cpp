#include "HotkeyManager.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace {

const std::wstring kDefaultHotkeyText = L"Ctrl+Alt+Shift+R";

bool SameShortcut(const HotkeyBinding& lhs, const HotkeyBinding& rhs) {
  return lhs.modifiers == rhs.modifiers && lhs.virtualKey == rhs.virtualKey;
}

HotkeyRegistrationResult MakeRegistrationFailure(DWORD errorCode) {
  return {
    .status = HotkeyRegistrationStatus::kRegistrationFailed,
    .errorCode = errorCode == ERROR_SUCCESS ? ERROR_GEN_FAILURE : errorCode,
  };
}

bool TryRegisterBinding(
  HWND windowHandle,
  int hotkeyId,
  const HotkeyBinding& binding,
  DWORD* errorCode) {
  if (RegisterHotKey(windowHandle, hotkeyId, binding.modifiers, binding.virtualKey) == TRUE) {
    if (errorCode != nullptr) {
      *errorCode = ERROR_SUCCESS;
    }
    return true;
  }

  if (errorCode != nullptr) {
    *errorCode = GetLastError();
  }
  return false;
}

std::wstring Trim(const std::wstring& value) {
  const auto first = std::find_if_not(value.begin(), value.end(), [](wchar_t ch) {
    return iswspace(ch) != 0;
  });
  const auto last = std::find_if_not(value.rbegin(), value.rend(), [](wchar_t ch) {
    return iswspace(ch) != 0;
  }).base();
  if (first >= last) {
    return {};
  }

  return std::wstring(first, last);
}

std::wstring Uppercase(const std::wstring& value) {
  std::wstring result = value;
  std::transform(result.begin(), result.end(), result.begin(), [](wchar_t ch) {
    return static_cast<wchar_t>(towupper(ch));
  });
  return result;
}

std::vector<std::wstring> SplitHotkey(const std::wstring& hotkeyText) {
  std::vector<std::wstring> parts;
  std::wstring current;

  for (const wchar_t ch : hotkeyText) {
    if (ch == L'+') {
      parts.push_back(Trim(current));
      current.clear();
      continue;
    }

    current.push_back(ch);
  }

  parts.push_back(Trim(current));
  return parts;
}

bool TryParseVirtualKeyToken(const std::wstring& token, UINT* virtualKey) {
  if (token.size() == 1) {
    const wchar_t value = token.front();
    if ((value >= L'A' && value <= L'Z') || (value >= L'0' && value <= L'9')) {
      *virtualKey = static_cast<UINT>(value);
      return true;
    }
  }

  if (token.size() >= 2 && token.front() == L'F') {
    int functionIndex = 0;
    for (size_t index = 1; index < token.size(); ++index) {
      const wchar_t ch = token[index];
      if (ch < L'0' || ch > L'9') {
        return false;
      }

      functionIndex = (functionIndex * 10) + static_cast<int>(ch - L'0');
    }

    if (functionIndex >= 1 && functionIndex <= 24) {
      *virtualKey = static_cast<UINT>(VK_F1 + functionIndex - 1);
      return true;
    }
  }

  if (token == L"SPACE") {
    *virtualKey = VK_SPACE;
    return true;
  }

  if (token == L"TAB") {
    *virtualKey = VK_TAB;
    return true;
  }

  if (token == L"ENTER") {
    *virtualKey = VK_RETURN;
    return true;
  }

  return false;
}

}  // namespace

HotkeyRegistrationResult HotkeyManager::Register(
  HWND windowHandle,
  const std::wstring& hotkeyText) {
  if (windowHandle == nullptr) {
    return {
      .status = HotkeyRegistrationStatus::kRegistrationFailed,
      .errorCode = ERROR_INVALID_WINDOW_HANDLE,
    };
  }

  HotkeyBinding parsedBinding{};
  if (!TryParseHotkey(hotkeyText, &parsedBinding)) {
    return {
      .status = HotkeyRegistrationStatus::kInvalidShortcut,
      .errorCode = ERROR_INVALID_DATA,
    };
  }

  if (!registered_) {
    DWORD errorCode = ERROR_SUCCESS;
    if (!TryRegisterBinding(windowHandle, kHotkeyId, parsedBinding, &errorCode)) {
      return MakeRegistrationFailure(errorCode);
    }

    binding_ = parsedBinding;
    registered_ = true;
    return {.status = HotkeyRegistrationStatus::kSuccess, .errorCode = ERROR_SUCCESS};
  }

  if (SameShortcut(binding_, parsedBinding)) {
    binding_.displayText = hotkeyText;
    return {.status = HotkeyRegistrationStatus::kSuccess, .errorCode = ERROR_SUCCESS};
  }

  UnregisterHotKey(windowHandle, kReplacementHotkeyId);

  DWORD errorCode = ERROR_SUCCESS;
  if (!TryRegisterBinding(windowHandle, kReplacementHotkeyId, parsedBinding, &errorCode)) {
    return MakeRegistrationFailure(errorCode);
  }

  if (UnregisterHotKey(windowHandle, kHotkeyId) != TRUE) {
    errorCode = GetLastError();
    UnregisterHotKey(windowHandle, kReplacementHotkeyId);
    return MakeRegistrationFailure(errorCode);
  }

  UnregisterHotKey(windowHandle, kReplacementHotkeyId);

  if (TryRegisterBinding(windowHandle, kHotkeyId, parsedBinding, &errorCode)) {
    binding_ = parsedBinding;
    registered_ = true;
    return {.status = HotkeyRegistrationStatus::kSuccess, .errorCode = ERROR_SUCCESS};
  }

  const DWORD primaryErrorCode = errorCode;
  DWORD restoreErrorCode = ERROR_SUCCESS;
  const bool restored = TryRegisterBinding(windowHandle, kHotkeyId, binding_, &restoreErrorCode);
  registered_ = restored;
  return MakeRegistrationFailure(primaryErrorCode);
}

void HotkeyManager::Unregister(HWND windowHandle) {
  if (!registered_ || windowHandle == nullptr) {
    return;
  }

  UnregisterHotKey(windowHandle, kHotkeyId);
  registered_ = false;
}

bool HotkeyManager::IsRegistered() const {
  return registered_;
}

bool HotkeyManager::IsHotkeyMessage(WPARAM wparam) const {
  return registered_ && static_cast<int>(wparam) == kHotkeyId;
}

const HotkeyBinding& HotkeyManager::binding() const {
  return binding_;
}

const std::wstring& HotkeyManager::DefaultHotkeyText() {
  return kDefaultHotkeyText;
}

bool HotkeyManager::TryParseHotkey(
  const std::wstring& hotkeyText,
  HotkeyBinding* binding) {
  if (binding == nullptr) {
    return false;
  }

  const std::vector<std::wstring> parts = SplitHotkey(hotkeyText);
  if (parts.size() < 2) {
    return false;
  }

  UINT modifiers = 0;
  UINT virtualKey = 0;

  for (const std::wstring& part : parts) {
    if (part.empty()) {
      return false;
    }

    const std::wstring token = Uppercase(part);
    if (token == L"CTRL" || token == L"CONTROL") {
      if ((modifiers & MOD_CONTROL) != 0) {
        return false;
      }
      modifiers |= MOD_CONTROL;
      continue;
    }

    if (token == L"ALT") {
      if ((modifiers & MOD_ALT) != 0) {
        return false;
      }
      modifiers |= MOD_ALT;
      continue;
    }

    if (token == L"SHIFT") {
      if ((modifiers & MOD_SHIFT) != 0) {
        return false;
      }
      modifiers |= MOD_SHIFT;
      continue;
    }

    if (virtualKey != 0 || !TryParseVirtualKeyToken(token, &virtualKey)) {
      return false;
    }
  }

  if (modifiers == 0 || virtualKey == 0) {
    return false;
  }

  *binding = {
    .modifiers = modifiers,
    .virtualKey = virtualKey,
    .displayText = hotkeyText,
  };
  return true;
}
