# WindowsAIRewrite

`WindowsAIRewrite` is a native C++20 Windows utility for hotkey-first text rewriting with a tray app, preview flow, and per-user credential storage.

## Demo

![WindowsAIRewrite demo](media/windows-ai-rewrite-demo.mp4)

## MVP scope

- Native Windows desktop executable built with CMake, MSVC v143+, and vcpkg manifest mode.
- Tray/background utility with a configurable global hotkey and preview-before-replace flow. The default hotkey is `Ctrl+Alt+Shift+R`.
- Per-user OpenAI API key storage in Windows Credential Manager.
- Simple per-user Inno Setup installer with a Start Menu shortcut and optional startup registration.

## Unsupported contexts in MVP

- Universal arbitrary-app right-click integration is out of scope for MVP.
- The chosen MVP interaction model is hotkey-first, not shell-extension-first.
- No auto-updater, telemetry, browser extension, Office add-in, or admin-by-default deployment is included.

## Project layout

- `src/`: application source files.
- `include/`: application headers.
- `resources/`: manifest, version resource, and icon assets.
- `installer/`: Inno Setup packaging assets, including `windows-ai-rewrite.iss`.
- `docs/qa/`: QA notes and execution evidence references.

## Build prerequisites

- Visual Studio 2022 with the MSVC v143 toolset.
- CMake 3.26 or newer.
- vcpkg configured for manifest mode with access to `nlohmann-json`.

## Build commands

```powershell
cmake --preset windows-msvc-x64
cmake --build --preset windows-msvc-x64-release
```

The expected release executable for packaging is `build\windows-msvc-x64\Release\WindowsAIRewrite.exe`.

## Installer prerequisites

- Inno Setup 6 with `ISCC.exe` available.
- A successful release build at `build\windows-msvc-x64\Release\WindowsAIRewrite.exe`.

Compile the installer from the project root with:

```powershell
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" "installer\windows-ai-rewrite.iss"
```

If `cmake`, the MSVC toolchain, or `ISCC.exe` is unavailable, the installer script can be reviewed statically but cannot be compiled until those prerequisites are installed and on PATH or invoked by full path.

## Install and uninstall notes

- The installer is per-user and defaults to `%LOCALAPPDATA%\Programs\Windows AI Rewrite`.
- The optional startup task writes `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\WindowsAIRewrite` to the installed executable path and removes that value on uninstall.
- The installer never asks for or stores the OpenAI API key. First launch keeps the app's normal first-run setup flow.
- Before uninstalling, use the app Settings UI if you want to delete the saved Credential Manager entry.
