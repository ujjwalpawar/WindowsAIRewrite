#include "UserMessages.h"

const wchar_t* UserMessage(UserMessageId messageId) {
  switch (messageId) {
    case UserMessageId::kEmptySelection:
      return L"Select some text first.";
    case UserMessageId::kUnsupportedSelection:
      return L"This app can only rewrite text from supported apps and controls.";
    case UserMessageId::kElevatedTargetUnsupported:
      return L"The target app is running elevated, so Windows blocked the selection request.";
    case UserMessageId::kSecureDesktopUnsupported:
      return L"This selection is not available from the secure desktop or a password field.";
    case UserMessageId::kPasswordFieldUnsupported:
      return L"Password fields are not supported for rewriting.";
    case UserMessageId::kClipboardBusy:
      return L"The clipboard is busy. Try again in a moment.";
    case UserMessageId::kMissingCredential:
      return L"Add your OpenAI API key in Credential Manager before rewriting.";
    case UserMessageId::kAuthFailed:
      return L"The saved API key is invalid. Replace it with a valid key.";
    case UserMessageId::kNetworkTimeout:
      return L"The request timed out. Check your connection and try again.";
    case UserMessageId::kOpenAIError:
      return L"OpenAI returned an error while generating the rewrite.";
    case UserMessageId::kHotkeyConflict:
      return L"Another app is already using that hotkey. Choose a different combination in Settings.";
    case UserMessageId::kAlreadyRunning:
      return L"Windows AI Rewrite is already running.";
    case UserMessageId::kUnexpectedInternalError:
    default:
      return L"An unexpected error occurred.";
  }
}

const wchar_t* FirstRunDisclosure() {
  return L"Selected text is sent to OpenAI to generate rewrites. The app does not log selected text, API keys, full prompts, or full responses.";
}
