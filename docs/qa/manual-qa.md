# Windows AI Rewrite Manual QA

## Current Status In This Environment

- Static implementation and task evidence for Tasks 1-9 exist under `C:\Users\ujjwa\.sisyphus\evidence\task-*`.
- Native build and runtime QA are currently blocked because `cmake`, MSVC `cl`, `vcpkg`, `ISCC.exe`, the release executable, and `clangd` are unavailable in this environment.
- Live OpenAI QA is currently blocked because no safe local API key is available for agent-run verification.
- Do not record real selected text, real API keys, Authorization headers, full prompts, or full responses in any evidence.
- Use only the fixture texts `hello world` and `this are test` in manual QA artifacts.

## Future Environment Prerequisites

1. Windows 10 22H2+ or Windows 11 x64.
2. `cmake` on `PATH`.
3. MSVC v143+ with `cl` on `PATH`.
4. `vcpkg` available for manifest restore.
5. Inno Setup 6 with `ISCC.exe` on `PATH`.
6. A safe runtime OpenAI API key entered only through the app first-run/settings UI.
7. Optional: `clangd` available for C++ language-server diagnostics.

## Build And Packaging Checklist

1. Run `cmake --preset windows-msvc-x64` from `C:\Users\ujjwa\WindowsAIRewrite`.
2. Run `cmake --build --preset windows-msvc-x64-release`.
3. Verify `build\windows-msvc-x64\Release\WindowsAIRewrite.exe` exists.
4. Run `ISCC.exe installer\windows-ai-rewrite.iss`.
5. Verify the installer output exists and references the release executable.

## First-Run Key Setup

1. Ensure no stored credential exists for `WindowsAIRewrite/OpenAIApiKey`.
2. Launch the app.
3. Verify first-run setup appears before any rewrite attempt.
4. Verify the disclosure says selected text is sent to OpenAI and that the app does not log selected text, API keys, full prompts, or full responses.
5. Enter a safe runtime key.
6. Save.
7. Re-open Settings.
8. Verify the key is shown only as configured/masked state, not plaintext.
9. Verify `%LOCALAPPDATA%\WindowsAIRewrite\settings.json` contains no secret value.

## Notepad Happy Path

1. Launch the built app and confirm the tray icon is visible.
2. Open Notepad.
3. Type `this are test`.
4. Select all text.
5. Press `Ctrl+Alt+Shift+R`.
6. Wait for the preview window.
7. Verify the preview shows original and rewritten text.
8. Click `Replace`.
9. Verify Notepad text changes only after `Replace`.
10. Verify the final Notepad text exactly matches the rewritten text shown in the preview.
11. Verify the app remains running and the tray icon remains available.

## Preview Cancel

1. Open Notepad.
2. Type `hello world`.
3. Select all text.
4. Trigger rewrite.
5. When preview appears, click `Cancel`.
6. Verify Notepad still contains exactly `hello world`.
7. Verify no replacement occurs and the app keeps running.

## Preview Copy

1. Open Notepad.
2. Type `hello world`.
3. Select all text.
4. Trigger rewrite.
5. When preview appears, click `Copy`.
6. Verify the selected text in Notepad is unchanged.
7. Paste into a safe scratch target and verify the pasted value equals the rewritten preview text.

## Missing Key

1. Delete the stored credential for `WindowsAIRewrite/OpenAIApiKey`.
2. Trigger rewrite from the tray or hotkey.
3. Verify the app routes back to setup or shows the `MissingCredential` message.
4. Verify the process remains running.

## Invalid Key

1. Store an intentionally invalid placeholder key through the UI without recording it in evidence.
2. Trigger rewrite on `this are test`.
3. Verify the app shows the `AuthFailed` message.
4. Verify no Authorization value appears in logs or evidence.
5. Delete the invalid key after the check.

## Empty Selection

1. Open Notepad.
2. Leave only a caret with no selection.
3. Trigger rewrite.
4. Verify the app shows the `EmptySelection` message.
5. Verify the process remains running.

## Unsupported Or Elevated Target

1. Try a target known to block supported selection capture, or an elevated Notepad instance if safely available.
2. Trigger rewrite.
3. Verify the app shows `UnsupportedSelection` or `ElevatedTargetUnsupported`, depending on the context.
4. Verify the process remains running and no crash occurs.

## Network Blocked

1. Block outbound network access for the app, or disconnect the network safely.
2. Trigger rewrite on `this are test`.
3. Verify the app shows `NetworkTimeout` or the configured OpenAI/network failure message.
4. Verify the process remains running.

## Clipboard Preservation And Restoration

1. Copy a safe scratch value to the clipboard before triggering rewrite.
2. Run the happy-path rewrite and click `Replace`.
3. Verify the rewrite paste succeeds.
4. Verify the original clipboard value is restored when `restoreClipboard=true`.
5. Repeat with a simulated clipboard lock if possible.
6. Verify the app reports `ClipboardBusy` or documents restoration failure without crashing.

## Tray Exit

1. Launch the app.
2. Open the tray menu.
3. Click `Exit`.
4. Verify the tray icon disappears.
5. Verify `WindowsAIRewrite.exe` is no longer running.

## Installer Startup And Uninstall

1. Build the installer.
2. Run the installer with the startup option enabled.
3. Verify the app installs under the configured per-user path.
4. Verify the app launches after install through the app executable, not through installer-side key collection.
5. Verify the HKCU Run value exists when startup is enabled.
6. Uninstall the app.
7. Verify installed files are removed.
8. Verify the HKCU Run value is removed.
9. Verify the user can delete the stored credential from app settings before uninstall, and document if any credential cleanup remains manual.

## Evidence Hygiene Checklist

1. Search project and evidence artifacts for secret-prefix markers.
2. Search project and evidence artifacts for auth-header markers.
3. Confirm evidence includes only fixture text: `hello world` and `this are test`.
4. Confirm evidence does not include real API keys, private selected text, full prompts, or full sensitive responses.
5. If a blocker prevents runtime QA, record the blocker explicitly instead of claiming pass status.
