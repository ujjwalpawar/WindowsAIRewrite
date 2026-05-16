#pragma once

#include <windows.h>

#include <string>

inline constexpr wchar_t kOpenAIApiCredentialTarget[] =
  L"WindowsAIRewrite/OpenAIApiKey";

enum class CredentialStoreStatus {
  kSuccess,
  kNotFound,
  kInvalidArgument,
  kInvalidData,
  kPlatformError,
};

struct CredentialOperationResult {
  CredentialStoreStatus status = CredentialStoreStatus::kSuccess;
  DWORD errorCode = ERROR_SUCCESS;
};

struct CredentialReadResult {
  CredentialStoreStatus status = CredentialStoreStatus::kSuccess;
  std::wstring value{};
  DWORD errorCode = ERROR_SUCCESS;
};

class CredentialStore {
 public:
  CredentialStore();
  explicit CredentialStore(std::wstring targetName);

  [[nodiscard]] CredentialOperationResult Write(const std::wstring& value) const;
  [[nodiscard]] CredentialOperationResult Update(const std::wstring& value) const;
  [[nodiscard]] CredentialReadResult Read() const;
  [[nodiscard]] CredentialOperationResult Delete() const;
  [[nodiscard]] bool Exists() const;

  [[nodiscard]] const std::wstring& target_name() const;

 private:
  std::wstring targetName_;
};
