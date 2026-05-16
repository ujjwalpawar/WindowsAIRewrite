#pragma once

#include <windows.h>

#include <string>

enum class PreviewWindowAction {
  kCancel,
  kReplace,
  kCopy,
};

struct PreviewWindowResult {
  PreviewWindowAction action = PreviewWindowAction::kCancel;
  std::wstring rewrittenText{};
};

class PreviewWindow {
 public:
  explicit PreviewWindow(HINSTANCE instanceHandle);
  PreviewWindow(const PreviewWindow&) = delete;
  PreviewWindow& operator=(const PreviewWindow&) = delete;
  ~PreviewWindow();

  [[nodiscard]] PreviewWindowResult Show(
    HWND ownerWindow,
    std::wstring originalText,
    std::wstring rewrittenText,
    bool actionsEnabled);

  void SetRewriteText(std::wstring rewrittenText);
  void SetRewriteError(std::wstring errorText);

 private:
  [[nodiscard]] bool EnsureWindowClass();
  [[nodiscard]] bool CreateThemeObjects();
  void DestroyThemeObjects();
  void CreateControls();
  void SetActionsEnabled(bool enabled);
  void CloseWithAction(PreviewWindowAction action);
  void CenterOverOwner() const;

  LRESULT HandleMessage(HWND windowHandle, UINT message, WPARAM wparam, LPARAM lparam);
  static LRESULT CALLBACK WindowProc(
    HWND windowHandle,
    UINT message,
    WPARAM wparam,
    LPARAM lparam);

  HINSTANCE instanceHandle_ = nullptr;
  HWND windowHandle_ = nullptr;
  HWND ownerWindow_ = nullptr;
  HWND originalEdit_ = nullptr;
  HWND rewrittenEdit_ = nullptr;
  HWND replaceButton_ = nullptr;
  HWND copyButton_ = nullptr;
  HFONT headingFont_ = nullptr;
  HFONT bodyFont_ = nullptr;
  HBRUSH canvasBrush_ = nullptr;
  HBRUSH fieldBrush_ = nullptr;
  std::wstring originalText_{};
  std::wstring rewrittenText_{};
  bool actionsEnabled_ = true;
  PreviewWindowResult result_{};
};
