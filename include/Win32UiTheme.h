#pragma once

#include <windows.h>

namespace Win32UiTheme {

namespace Color {
inline constexpr COLORREF kCanvas = RGB(245, 242, 235);
inline constexpr COLORREF kPanel = RGB(255, 252, 246);
inline constexpr COLORREF kField = RGB(255, 255, 255);
inline constexpr COLORREF kInk = RGB(35, 38, 42);
inline constexpr COLORREF kMutedInk = RGB(91, 96, 101);
inline constexpr COLORREF kAccent = RGB(0, 108, 103);
inline constexpr COLORREF kDanger = RGB(156, 52, 56);
}  // namespace Color

namespace Space {
inline constexpr int kXs = 4;
inline constexpr int kSm = 8;
inline constexpr int kMd = 12;
inline constexpr int kLg = 16;
inline constexpr int kXl = 24;
inline constexpr int kXxl = 32;
}  // namespace Space

namespace Radius {
inline constexpr int kPanel = 14;
}  // namespace Radius

namespace Metric {
inline constexpr int kButtonWidth = 104;
inline constexpr int kButtonHeight = 32;
inline constexpr int kInputHeight = 26;
inline constexpr int kLabelHeight = 20;
inline constexpr int kBodyPointSize = 10;
inline constexpr int kHeadingPointSize = 16;
}

inline constexpr wchar_t kHeadingFontName[] = L"Bahnschrift SemiBold";
inline constexpr wchar_t kBodyFontName[] = L"Segoe UI";

}  // namespace Win32UiTheme
