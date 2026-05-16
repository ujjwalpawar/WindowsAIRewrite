#include "CredentialStore.h"

#include <wincred.h>

#include <cstring>
#include <utility>
#include <vector>

namespace {

CredentialOperationResult ResultFromLastError(DWORD errorCode) {
  return {
    .status = CredentialStoreStatus::kPlatformError,
    .errorCode = errorCode,
  };
}

std::vector<unsigned char> EncodeSecret(const std::wstring& value) {
  std::vector<unsigned char> blob(value.size() * sizeof(wchar_t));
  if (!blob.empty()) {
    std::memcpy(blob.data(), value.data(), blob.size());
  }
  return blob;
}

}  // namespace

CredentialStore::CredentialStore() : targetName_(kOpenAIApiCredentialTarget) {}

CredentialStore::CredentialStore(std::wstring targetName)
  : targetName_(std::move(targetName)) {}

CredentialOperationResult CredentialStore::Write(const std::wstring& value) const {
  if (value.empty()) {
    return {
      .status = CredentialStoreStatus::kInvalidArgument,
      .errorCode = ERROR_INVALID_PARAMETER,
    };
  }

  std::vector<unsigned char> blob = EncodeSecret(value);

  CREDENTIALW credential{};
  credential.Type = CRED_TYPE_GENERIC;
  credential.TargetName = const_cast<LPWSTR>(targetName_.c_str());
  credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
  credential.CredentialBlob = blob.empty() ? nullptr : blob.data();
  credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
  credential.UserName = const_cast<LPWSTR>(L"OpenAI");

  if (CredWriteW(&credential, 0) != TRUE) {
    return ResultFromLastError(GetLastError());
  }

  return {.status = CredentialStoreStatus::kSuccess, .errorCode = ERROR_SUCCESS};
}

CredentialOperationResult CredentialStore::Update(const std::wstring& value) const {
  return Write(value);
}

CredentialReadResult CredentialStore::Read() const {
  PCREDENTIALW credential = nullptr;
  if (CredReadW(targetName_.c_str(), CRED_TYPE_GENERIC, 0, &credential) != TRUE) {
    const DWORD errorCode = GetLastError();
    if (errorCode == ERROR_NOT_FOUND) {
      return {
        .status = CredentialStoreStatus::kNotFound,
        .value = {},
        .errorCode = errorCode,
      };
    }

    return {
      .status = CredentialStoreStatus::kPlatformError,
      .value = {},
      .errorCode = errorCode,
    };
  }

  CredentialReadResult result{};
  // Store the credential blob as UTF-16LE bytes so the Win32 boundary keeps the
  // secret lossless without any UTF-8 transcoding step.
  if (credential->CredentialBlobSize % sizeof(wchar_t) != 0) {
    result = {
      .status = CredentialStoreStatus::kInvalidData,
      .value = {},
      .errorCode = ERROR_INVALID_DATA,
    };
  } else {
    const wchar_t* value = reinterpret_cast<const wchar_t*>(credential->CredentialBlob);
    const size_t length = credential->CredentialBlobSize / sizeof(wchar_t);
    result = {
      .status = CredentialStoreStatus::kSuccess,
      .value = std::wstring(value, value + length),
      .errorCode = ERROR_SUCCESS,
    };
  }

  CredFree(credential);
  return result;
}

CredentialOperationResult CredentialStore::Delete() const {
  if (CredDeleteW(targetName_.c_str(), CRED_TYPE_GENERIC, 0) != TRUE) {
    const DWORD errorCode = GetLastError();
    if (errorCode == ERROR_NOT_FOUND) {
      return {
        .status = CredentialStoreStatus::kNotFound,
        .errorCode = errorCode,
      };
    }

    return ResultFromLastError(errorCode);
  }

  return {.status = CredentialStoreStatus::kSuccess, .errorCode = ERROR_SUCCESS};
}

bool CredentialStore::Exists() const {
  return Read().status == CredentialStoreStatus::kSuccess;
}

const std::wstring& CredentialStore::target_name() const {
  return targetName_;
}
