#pragma once

#include <windows.h>

#include <string>

#include "CredentialStore.h"
#include "SettingsStore.h"

enum class SettingsWindowAction {
  kCancel,
  kSaved,
};

struct SettingsWindowResult {
  SettingsWindowAction action = SettingsWindowAction::kCancel;
  AppSettings settings = SettingsStore::DefaultSettings();
  bool apiKeyConfigured = false;
};

class SettingsWindow {
 public:
  explicit SettingsWindow(HINSTANCE instanceHandle);
  SettingsWindow(
    HINSTANCE instanceHandle,
    SettingsStore settingsStore,
    CredentialStore credentialStore);
  SettingsWindow(const SettingsWindow&) = delete;
  SettingsWindow& operator=(const SettingsWindow&) = delete;
  ~SettingsWindow();

  [[nodiscard]] bool ShowFirstRunSetup(HWND ownerWindow);
  [[nodiscard]] SettingsWindowResult ShowSettings(HWND ownerWindow);

 private:
  enum class Mode {
    kFirstRun,
    kSettings,
  };

  [[nodiscard]] bool ShowModal(HWND ownerWindow, Mode mode);
  [[nodiscard]] bool EnsureWindowClass();
  [[nodiscard]] bool CreateThemeObjects();
  void DestroyThemeObjects();
  void CreateFirstRunControls();
  void CreateSettingsControls();
  void SaveFirstRunApiKey();
  void SaveSettings();
  void DeleteApiKey();
  void RefreshApiKeyStatus();
  void UpdateModels();
  void SetModelStatus(const std::wstring& text, bool isError);
  void StartHotkeyCapture();
  [[nodiscard]] bool HandleHotkeyCapture(WPARAM wparam, LPARAM lparam);
  void CloseWithAction(SettingsWindowAction action);
  void CenterOverOwner() const;

  [[nodiscard]] std::wstring ReadControlText(HWND control) const;
  [[nodiscard]] bool TryReadTimeoutSeconds(int* timeoutSeconds) const;
  [[nodiscard]] std::wstring ReadSelectedModel() const;

  LRESULT HandleMessage(HWND windowHandle, UINT message, WPARAM wparam, LPARAM lparam);
  static LRESULT CALLBACK WindowProc(
    HWND windowHandle,
    UINT message,
    WPARAM wparam,
    LPARAM lparam);

  HINSTANCE instanceHandle_ = nullptr;
  HWND windowHandle_ = nullptr;
  HWND ownerWindow_ = nullptr;
  HWND apiKeyEdit_ = nullptr;
  HWND apiKeyStatus_ = nullptr;
  HWND hotkeyEdit_ = nullptr;
  HWND hotkeyStatus_ = nullptr;
  HWND modelCombo_ = nullptr;
  HWND updateModelsButton_ = nullptr;
  HWND modelStatus_ = nullptr;
  HWND timeoutEdit_ = nullptr;
  HWND restoreClipboardCheck_ = nullptr;
  HWND launchAtStartupCheck_ = nullptr;
  HFONT headingFont_ = nullptr;
  HFONT bodyFont_ = nullptr;
  HBRUSH canvasBrush_ = nullptr;
  HBRUSH fieldBrush_ = nullptr;
  SettingsStore settingsStore_{};
  CredentialStore credentialStore_{};
  AppSettings settings_ = SettingsStore::DefaultSettings();
  SettingsWindowResult result_{};
  Mode mode_ = Mode::kSettings;
  bool apiKeyConfigured_ = false;
  bool capturingHotkey_ = false;
  bool modelStatusIsError_ = false;
};
