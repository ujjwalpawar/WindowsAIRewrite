#include "PreviewWindow.h"

#include <utility>

#include "UiControlIds.h"
#include "Win32WindowActivation.h"
#include "Win32UiTheme.h"

namespace {

constexpr wchar_t kWindowClassName[] = L"WindowsAIRewritePreviewWindow";
constexpr wchar_t kWindowTitle[] = L"Preview rewrite";
constexpr int kWindowWidth = 760;
constexpr int kWindowHeight = 560;
constexpr int kContentLeft = Win32UiTheme::Space::kXl;
constexpr int kContentTop = Win32UiTheme::Space::kLg;
constexpr int kContentWidth = kWindowWidth - (Win32UiTheme::Space::kXl * 2);
constexpr int kHeadingHeight = 28;
constexpr int kColumnGap = Win32UiTheme::Space::kLg;
constexpr int kColumnWidth = (kContentWidth - kColumnGap) / 2;
constexpr int kLabelTop = kContentTop + kHeadingHeight + Win32UiTheme::Space::kLg;
constexpr int kEditorTop = kLabelTop + Win32UiTheme::Metric::kLabelHeight + Win32UiTheme::Space::kSm;
constexpr int kEditorHeight = 372;
constexpr int kButtonTop = kEditorTop + kEditorHeight + Win32UiTheme::Space::kLg;
constexpr int kButtonGap = Win32UiTheme::Space::kSm;

int ClampToRange(int value, int lowerBound, int upperBound) {
  if (upperBound < lowerBound) {
    return lowerBound;
  }

  if (value < lowerBound) {
    return lowerBound;
  }

  if (value > upperBound) {
    return upperBound;
  }

  return value;
}

bool IsUsableOwnerWindow(HWND ownerWindow) {
  if (ownerWindow == nullptr || !IsWindowVisible(ownerWindow) || IsIconic(ownerWindow)) {
    return false;
  }

  RECT ownerRect{};
  if (GetWindowRect(ownerWindow, &ownerRect) != TRUE) {
    return false;
  }

  return (ownerRect.right - ownerRect.left) > 1 && (ownerRect.bottom - ownerRect.top) > 1;
}

bool TryGetWorkAreaForWindow(HWND referenceWindow, RECT* workArea) {
  if (referenceWindow == nullptr || workArea == nullptr) {
    return false;
  }

  const HMONITOR monitor = MonitorFromWindow(referenceWindow, MONITOR_DEFAULTTONEAREST);
  if (monitor != nullptr) {
    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (GetMonitorInfoW(monitor, &monitorInfo) == TRUE) {
      *workArea = monitorInfo.rcWork;
      return true;
    }
  }

  return SystemParametersInfoW(SPI_GETWORKAREA, 0, workArea, 0) == TRUE;
}

void CenterWindowWithinWorkArea(HWND windowHandle, const RECT& workArea) {
  RECT windowRect{};
  GetWindowRect(windowHandle, &windowRect);

  const int width = windowRect.right - windowRect.left;
  const int height = windowRect.bottom - windowRect.top;
  const int workWidth = static_cast<int>(workArea.right - workArea.left);
  const int workHeight = static_cast<int>(workArea.bottom - workArea.top);

  int x = workArea.left + ((workWidth - width) / 2);
  int y = workArea.top + ((workHeight - height) / 2);

  x = ClampToRange(x, static_cast<int>(workArea.left), static_cast<int>(workArea.right - width));
  y = ClampToRange(y, static_cast<int>(workArea.top), static_cast<int>(workArea.bottom - height));

  SetWindowPos(windowHandle, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}

HFONT CreatePointFont(int pointSize, int weight, const wchar_t* faceName) {
  HDC screen = GetDC(nullptr);
  const int pixels = screen == nullptr
                       ? pointSize
                       : MulDiv(pointSize, GetDeviceCaps(screen, LOGPIXELSY), 72);
  if (screen != nullptr) {
    ReleaseDC(nullptr, screen);
  }

  return CreateFontW(
    -pixels,
    0,
    0,
    0,
    weight,
    FALSE,
    FALSE,
    FALSE,
    DEFAULT_CHARSET,
    OUT_DEFAULT_PRECIS,
    CLIP_DEFAULT_PRECIS,
    CLEARTYPE_QUALITY,
    DEFAULT_PITCH | FF_DONTCARE,
    faceName);
}

HMENU ControlHandle(int controlId) {
  return reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId));
}

void SetControlFont(HWND control, HFONT font) {
  if (control != nullptr && font != nullptr) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
  }
}

HWND CreateStaticText(
  HWND parent,
  HINSTANCE instanceHandle,
  const wchar_t* text,
  int x,
  int y,
  int width,
  int height,
  HFONT font) {
  HWND control = CreateWindowExW(
    0,
    L"STATIC",
    text,
    WS_CHILD | WS_VISIBLE | SS_LEFT,
    x,
    y,
    width,
    height,
    parent,
    nullptr,
    instanceHandle,
    nullptr);
  SetControlFont(control, font);
  return control;
}

HWND CreateButton(
  HWND parent,
  HINSTANCE instanceHandle,
  const wchar_t* text,
  int controlId,
  int x,
  int y,
  HFONT font) {
  HWND control = CreateWindowExW(
    0,
    L"BUTTON",
    text,
    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
    x,
    y,
    Win32UiTheme::Metric::kButtonWidth,
    Win32UiTheme::Metric::kButtonHeight,
    parent,
    ControlHandle(controlId),
    instanceHandle,
    nullptr);
  SetControlFont(control, font);
  return control;
}

}  // namespace

PreviewWindow::PreviewWindow(HINSTANCE instanceHandle) : instanceHandle_(instanceHandle) {}

PreviewWindow::~PreviewWindow() {
  DestroyThemeObjects();
}

PreviewWindowResult PreviewWindow::Show(
  HWND ownerWindow,
  std::wstring originalText,
  std::wstring rewrittenText,
  bool actionsEnabled) {
  result_ = {
    .action = PreviewWindowAction::kCancel,
    .rewrittenText = actionsEnabled ? rewrittenText : L"",
  };
  ownerWindow_ = ownerWindow;
  originalText_ = std::move(originalText);
  rewrittenText_ = std::move(rewrittenText);
  actionsEnabled_ = actionsEnabled;

  if (!EnsureWindowClass() || !CreateThemeObjects()) {
    return result_;
  }

  windowHandle_ = CreateWindowExW(
    WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
    kWindowClassName,
    kWindowTitle,
    WS_CAPTION | WS_SYSMENU,
    CW_USEDEFAULT,
    CW_USEDEFAULT,
    kWindowWidth,
    kWindowHeight,
    ownerWindow_,
    nullptr,
    instanceHandle_,
    this);

  if (windowHandle_ == nullptr) {
    return result_;
  }

  if (ownerWindow_ != nullptr) {
    EnableWindow(ownerWindow_, FALSE);
  }

  CenterOverOwner();
  Win32WindowActivation::ShowAndActivateWindow(windowHandle_);
  UpdateWindow(windowHandle_);

  MSG message{};
  while (windowHandle_ != nullptr) {
    const BOOL getMessageResult = GetMessageW(&message, nullptr, 0, 0);
    if (getMessageResult <= 0) {
      break;
    }

    if (windowHandle_ == nullptr || !IsDialogMessageW(windowHandle_, &message)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
  }

  if (ownerWindow_ != nullptr) {
    EnableWindow(ownerWindow_, TRUE);
    SetActiveWindow(ownerWindow_);
  }

  return result_;
}

void PreviewWindow::SetRewriteText(std::wstring rewrittenText) {
  rewrittenText_ = std::move(rewrittenText);
  result_.rewrittenText = rewrittenText_;
  if (rewrittenEdit_ != nullptr) {
    SetWindowTextW(rewrittenEdit_, rewrittenText_.c_str());
  }
  SetActionsEnabled(true);
  if (replaceButton_ != nullptr) {
    SetFocus(replaceButton_);
  }
}

void PreviewWindow::SetRewriteError(std::wstring errorText) {
  rewrittenText_ = std::move(errorText);
  result_.rewrittenText.clear();
  if (rewrittenEdit_ != nullptr) {
    SetWindowTextW(rewrittenEdit_, rewrittenText_.c_str());
  }
  SetActionsEnabled(false);
}

bool PreviewWindow::EnsureWindowClass() {
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = &PreviewWindow::WindowProc;
  windowClass.hInstance = instanceHandle_;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.lpszClassName = kWindowClassName;

  if (RegisterClassExW(&windowClass) != 0) {
    return true;
  }

  return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool PreviewWindow::CreateThemeObjects() {
  DestroyThemeObjects();
  headingFont_ = CreatePointFont(
    Win32UiTheme::Metric::kHeadingPointSize,
    FW_SEMIBOLD,
    Win32UiTheme::kHeadingFontName);
  bodyFont_ = CreatePointFont(
    Win32UiTheme::Metric::kBodyPointSize,
    FW_NORMAL,
    Win32UiTheme::kBodyFontName);
  canvasBrush_ = CreateSolidBrush(Win32UiTheme::Color::kCanvas);
  fieldBrush_ = CreateSolidBrush(Win32UiTheme::Color::kField);

  return headingFont_ != nullptr && bodyFont_ != nullptr && canvasBrush_ != nullptr &&
         fieldBrush_ != nullptr;
}

void PreviewWindow::DestroyThemeObjects() {
  if (headingFont_ != nullptr) {
    DeleteObject(headingFont_);
    headingFont_ = nullptr;
  }
  if (bodyFont_ != nullptr) {
    DeleteObject(bodyFont_);
    bodyFont_ = nullptr;
  }
  if (canvasBrush_ != nullptr) {
    DeleteObject(canvasBrush_);
    canvasBrush_ = nullptr;
  }
  if (fieldBrush_ != nullptr) {
    DeleteObject(fieldBrush_);
    fieldBrush_ = nullptr;
  }
}

void PreviewWindow::CreateControls() {
  CreateStaticText(
    windowHandle_,
    instanceHandle_,
    L"Review before replacing anything",
    kContentLeft,
    kContentTop,
    kContentWidth,
    kHeadingHeight,
    headingFont_);

  CreateStaticText(
    windowHandle_,
    instanceHandle_,
    L"Original text",
    kContentLeft,
    kLabelTop,
    kColumnWidth,
    Win32UiTheme::Metric::kLabelHeight,
    bodyFont_);
  CreateStaticText(
    windowHandle_,
    instanceHandle_,
    L"Rewritten text",
    kContentLeft + kColumnWidth + kColumnGap,
    kLabelTop,
    kColumnWidth,
    Win32UiTheme::Metric::kLabelHeight,
    bodyFont_);

  originalEdit_ = CreateWindowExW(
    WS_EX_CLIENTEDGE,
    L"EDIT",
    originalText_.c_str(),
    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_LEFT | ES_MULTILINE |
      ES_AUTOVSCROLL | ES_READONLY,
    kContentLeft,
    kEditorTop,
    kColumnWidth,
    kEditorHeight,
    windowHandle_,
    ControlHandle(UiControlId::kPreviewOriginalEdit),
    instanceHandle_,
    nullptr);
  SetControlFont(originalEdit_, bodyFont_);

  rewrittenEdit_ = CreateWindowExW(
    WS_EX_CLIENTEDGE,
    L"EDIT",
    rewrittenText_.c_str(),
    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_LEFT | ES_MULTILINE |
      ES_AUTOVSCROLL | ES_READONLY,
    kContentLeft + kColumnWidth + kColumnGap,
    kEditorTop,
    kColumnWidth,
    kEditorHeight,
    windowHandle_,
    ControlHandle(UiControlId::kPreviewRewrittenEdit),
    instanceHandle_,
    nullptr);
  SetControlFont(rewrittenEdit_, bodyFont_);

  const int cancelLeft = kWindowWidth - Win32UiTheme::Space::kXl -
                         Win32UiTheme::Metric::kButtonWidth;
  const int copyLeft = cancelLeft - kButtonGap - Win32UiTheme::Metric::kButtonWidth;
  const int replaceLeft = copyLeft - kButtonGap - Win32UiTheme::Metric::kButtonWidth;

  replaceButton_ = CreateButton(
    windowHandle_,
    instanceHandle_,
    L"Replace",
    UiControlId::kPreviewReplaceButton,
    replaceLeft,
    kButtonTop,
    bodyFont_);
  copyButton_ = CreateButton(
    windowHandle_,
    instanceHandle_,
    L"Copy",
    UiControlId::kPreviewCopyButton,
    copyLeft,
    kButtonTop,
    bodyFont_);
  HWND cancelButton = CreateButton(
    windowHandle_,
    instanceHandle_,
    L"Cancel",
    UiControlId::kPreviewCancelButton,
    cancelLeft,
    kButtonTop,
    bodyFont_);
  SetActionsEnabled(actionsEnabled_);
  SetFocus(actionsEnabled_ ? replaceButton_ : cancelButton);
}

void PreviewWindow::SetActionsEnabled(bool enabled) {
  actionsEnabled_ = enabled;
  if (replaceButton_ != nullptr) {
    EnableWindow(replaceButton_, enabled ? TRUE : FALSE);
  }
  if (copyButton_ != nullptr) {
    EnableWindow(copyButton_, enabled ? TRUE : FALSE);
  }
}

void PreviewWindow::CloseWithAction(PreviewWindowAction action) {
  if ((action == PreviewWindowAction::kReplace || action == PreviewWindowAction::kCopy) &&
      !actionsEnabled_) {
    return;
  }
  result_.action = action;
  result_.rewrittenText = actionsEnabled_ ? rewrittenText_ : L"";
  if (windowHandle_ != nullptr) {
    DestroyWindow(windowHandle_);
  }
}

void PreviewWindow::CenterOverOwner() const {
  if (windowHandle_ == nullptr) {
    return;
  }

  RECT windowRect{};
  GetWindowRect(windowHandle_, &windowRect);

  if (IsUsableOwnerWindow(ownerWindow_)) {
    RECT anchorRect{};
    GetWindowRect(ownerWindow_, &anchorRect);

    RECT workArea{};
    if (!TryGetWorkAreaForWindow(ownerWindow_, &workArea) &&
        !TryGetWorkAreaForWindow(windowHandle_, &workArea)) {
      return;
    }

    const int width = anchorRect.right - anchorRect.left;
    const int height = anchorRect.bottom - anchorRect.top;
    const int windowWidth = windowRect.right - windowRect.left;
    const int windowHeight = windowRect.bottom - windowRect.top;
    int x = anchorRect.left + ((width - windowWidth) / 2);
    int y = anchorRect.top + ((height - windowHeight) / 2);
    x = ClampToRange(x, static_cast<int>(workArea.left), static_cast<int>(workArea.right - windowWidth));
    y = ClampToRange(y, static_cast<int>(workArea.top), static_cast<int>(workArea.bottom - windowHeight));
    SetWindowPos(windowHandle_, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
    return;
  }

  RECT workArea{};
  if (!TryGetWorkAreaForWindow(windowHandle_, &workArea)) {
    return;
  }

  CenterWindowWithinWorkArea(windowHandle_, workArea);
}

LRESULT PreviewWindow::HandleMessage(HWND windowHandle, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_CREATE:
      CreateControls();
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case UiControlId::kPreviewReplaceButton:
          CloseWithAction(PreviewWindowAction::kReplace);
          return 0;
        case UiControlId::kPreviewCopyButton:
          CloseWithAction(PreviewWindowAction::kCopy);
          return 0;
        case UiControlId::kPreviewCancelButton:
          CloseWithAction(PreviewWindowAction::kCancel);
          return 0;
        default:
          break;
      }
      break;
    case WM_CLOSE:
      CloseWithAction(PreviewWindowAction::kCancel);
      return 0;
    case WM_NCDESTROY:
      windowHandle_ = nullptr;
      break;
    case WM_ERASEBKGND: {
      RECT clientRect{};
      GetClientRect(windowHandle, &clientRect);
      FillRect(reinterpret_cast<HDC>(wparam), &clientRect, canvasBrush_);
      return 1;
    }
    case WM_CTLCOLORSTATIC: {
      HDC deviceContext = reinterpret_cast<HDC>(wparam);
      SetTextColor(deviceContext, Win32UiTheme::Color::kInk);
      SetBkColor(deviceContext, Win32UiTheme::Color::kCanvas);
      return reinterpret_cast<LRESULT>(canvasBrush_);
    }
    case WM_CTLCOLOREDIT: {
      HDC deviceContext = reinterpret_cast<HDC>(wparam);
      SetTextColor(deviceContext, Win32UiTheme::Color::kInk);
      SetBkColor(deviceContext, Win32UiTheme::Color::kField);
      return reinterpret_cast<LRESULT>(fieldBrush_);
    }
    default:
      break;
  }

  return DefWindowProcW(windowHandle, message, wparam, lparam);
}

LRESULT CALLBACK PreviewWindow::WindowProc(
  HWND windowHandle,
  UINT message,
  WPARAM wparam,
  LPARAM lparam) {
  PreviewWindow* previewWindow = nullptr;

  if (message == WM_NCCREATE) {
    const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lparam);
    previewWindow = static_cast<PreviewWindow*>(createStruct->lpCreateParams);
    SetWindowLongPtrW(windowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(previewWindow));
    if (previewWindow != nullptr) {
      previewWindow->windowHandle_ = windowHandle;
    }
  } else {
    previewWindow = reinterpret_cast<PreviewWindow*>(GetWindowLongPtrW(windowHandle, GWLP_USERDATA));
  }

  if (previewWindow == nullptr) {
    return DefWindowProcW(windowHandle, message, wparam, lparam);
  }

  const LRESULT result = previewWindow->HandleMessage(windowHandle, message, wparam, lparam);
  if (message == WM_NCDESTROY) {
    SetWindowLongPtrW(windowHandle, GWLP_USERDATA, 0);
    previewWindow->windowHandle_ = nullptr;
  }

  return result;
}
