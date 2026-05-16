cmake_minimum_required(VERSION 3.26)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT})
  file(TO_CMAKE_PATH "$ENV{VCPKG_ROOT}" VCPKG_ROOT)
  set(
    CMAKE_TOOLCHAIN_FILE
    "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
    CACHE FILEPATH "Path to the vcpkg toolchain file"
  )
endif()

project(WindowsAIRewrite VERSION 0.3.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(WindowsAIRewrite WIN32
  src/AppError.cpp
  src/App.cpp
  src/ClipboardManager.cpp
  src/SafeLog.cpp
  src/HotkeyManager.cpp
  src/OpenAIClient.cpp
  src/PreviewWindow.cpp
  src/RewriteController.cpp
  src/SettingsWindow.cpp
  src/TrayIcon.cpp
  src/main.cpp
  src/SettingsStore.cpp
  src/TextCaptureService.cpp
  src/CredentialStore.cpp
  src/UserMessages.cpp
  resources/WindowsAIRewrite.rc
)

target_include_directories(WindowsAIRewrite PRIVATE include)

target_compile_definitions(WindowsAIRewrite PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  UNICODE
  _UNICODE
)

if(MSVC)
  target_compile_options(WindowsAIRewrite PRIVATE /W4 /permissive- /EHsc /utf-8)
  target_link_options(WindowsAIRewrite PRIVATE /MANIFEST:NO)
endif()

find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(WindowsAIRewrite PRIVATE
  nlohmann_json::nlohmann_json
  Advapi32
  Ole32
  OleAut32
  Shell32
  Uiautomationcore
  Winhttp
  Gdi32
  User32
)

set_target_properties(WindowsAIRewrite PROPERTIES
  OUTPUT_NAME "WindowsAIRewrite"
  VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
