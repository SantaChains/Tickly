# =============================================================================
#  Tickly —— 极简 Win32 待办工具
#
#  构建（任选其一）：
#    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#    cmake --build build
#
#  产物：build/Tickly.exe（单文件，静态链接运行库）。
#
#  资源（图标 ID=1、DPI 感知清单、版本信息）来自 assets/resource.rc，
#  必须参与链接：缺图标 -> 任务栏白板；缺清单 -> 非 100% 缩放下文字发虚。
# =============================================================================
cmake_minimum_required(VERSION 3.20)
project(Tickly LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "Tickly 是 Win32 程序，只能在 Windows 上构建")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# 版本号唯一来源：src/app/version.h 的 TICKLY_VERSION
# 只在此处解析并校验，避免「界面 / 资源 / 清单」三处版本号各写一份
# ---------------------------------------------------------------------------
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/app/version.h" _ver_line
     REGEX "#define[ \t]+TICKLY_VERSION")
string(REGEX MATCH "L\"([0-9.]+)\"" _ver_match "${_ver_line}")
if(NOT _ver_match)
  message(FATAL_ERROR "src/app/version.h 里找不到 TICKLY_VERSION")
endif()
set(TICKLY_VERSION "${CMAKE_MATCH_1}")

# 资源脚本里的 FILEVERSION 与清单里的 version 必须与源码一致
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/resource.rc" _rc)
string(REGEX MATCH "FILEVERSION[ \t]+([0-9]+),([0-9]+),([0-9]+),([0-9]+)" _m "${_rc}")
if(NOT _m)
  message(FATAL_ERROR "assets/resource.rc 里找不到 FILEVERSION")
endif()
set(_rc_ver "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}.${CMAKE_MATCH_4}")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.manifest" _mf)
string(REGEX MATCH "assemblyIdentity[^>]*version=\"([0-9.]+)\"" _m2 "${_mf}")
set(_mf_ver "${CMAKE_MATCH_1}")
set(_want_ver "${TICKLY_VERSION}.0")
if(NOT _rc_ver STREQUAL _want_ver OR NOT _mf_ver STREQUAL _want_ver)
  message(FATAL_ERROR
    "版本号不一致：version.h=${_want_ver} resource.rc=${_rc_ver} app.manifest=${_mf_ver}")
endif()
message(STATUS "Tickly ${TICKLY_VERSION}（version.h / resource.rc / app.manifest 一致）")

# ---------------------------------------------------------------------------
# 源文件（按层次分组，便于对照模块职责）
# ---------------------------------------------------------------------------
set(TICKLY_APP_SOURCES
  src/main.cpp
  src/app/actions.cpp
  src/app/rows.cpp
  src/app/store.cpp
)

set(TICKLY_CORE_SOURCES
  src/core/text_util.cpp
  src/core/time_util.cpp
  src/core/date_parse.cpp
)

set(TICKLY_UI_SOURCES
  src/ui/context.cpp
  src/ui/theme.cpp
  src/ui/dark_mode.cpp
  src/ui/gdi_util.cpp
  src/ui/dwm_shell.cpp
  src/ui/main_window.cpp
  src/ui/list_view.cpp
  src/ui/list_text.cpp
  src/ui/list_paint.cpp
  src/ui/row_editor.cpp
  src/ui/input_box.cpp
  src/ui/tray.cpp
  src/ui/toast.cpp
  src/ui/capture.cpp
  src/ui/autostart.cpp
  src/ui/hotkeys.cpp
  src/ui/reminder.cpp
  src/ui/dialogs_common.cpp
  src/ui/dialogs_settings.cpp
  src/ui/dialogs_date.cpp
)

set(TICKLY_PLATFORM_SOURCES
  src/platform/dpi.cpp
)

# DPI 感知清单的注入方式按工具链二选一：
#   MSVC  -> app.manifest 作为 manifest 输入交给 link.exe（/MANIFESTINPUT）
#   mingw -> manifest.rc 把清单包成 RT_MANIFEST 资源（windres）
if(MSVC)
  set(TICKLY_RES_SOURCES assets/resource.rc assets/app.manifest)
else()
  set(TICKLY_RES_SOURCES assets/resource.rc assets/manifest.rc)
endif()

# 资源脚本里 include 的图标/清单不是 C/C++ 头文件，windres/rc 的依赖扫描不跟踪它们：
# 只改 app.ico 时增量构建会“看起来成功”但 exe 还是旧图标（实测踩到）。
# 这里显式声明对象级依赖，改图标/清单一定会重新编译资源并重链。
set_source_files_properties(assets/resource.rc PROPERTIES OBJECT_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.ico;${CMAKE_CURRENT_SOURCE_DIR}/assets/app.manifest")
set_source_files_properties(assets/manifest.rc PROPERTIES OBJECT_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.manifest")

add_executable(Tickly WIN32
  ${TICKLY_APP_SOURCES}
  ${TICKLY_CORE_SOURCES}
  ${TICKLY_UI_SOURCES}
  ${TICKLY_PLATFORM_SOURCES}
  ${TICKLY_RES_SOURCES}
)

target_include_directories(Tickly PRIVATE src assets)
target_compile_definitions(Tickly PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(Tickly PRIVATE
  user32 gdi32 advapi32 shell32 comctl32 comdlg32 ole32 uxtheme)

if(MSVC)
  # /utf-8：源码与执行字符集都用 UTF-8（否则中文串在英文 runner 上会乱）
  # /MT ：静态链接 C 运行库 -> 单 exe，不依赖 VC++ 运行库
  target_compile_options(Tickly PRIVATE /utf-8 /W3 /EHsc)
  set_property(TARGET Tickly PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
  # 资源脚本自己用 #pragma code_page(65001) 声明 UTF-8（见 assets/resource.rc）；
  # 不要再传 /c 65001：cmcldeps 会把 65001 当成源文件。
  # 清单走 /MANIFESTINPUT，避免与 .rc 里的 RT_MANIFEST 重复（CVT1100）。
else()
  # mingw-w64：告警全开，把「看似能用其实有坑」的代码挡在构建期
  target_compile_options(Tickly PRIVATE -Wall -Wextra -Wno-unused-parameter
    -Wno-cast-function-type -Wno-missing-field-initializers)
  # -municode：wWinMain 入口；-mwindows：GUI 子系统；-static：单文件无需运行时 DLL
  # -specs：去掉 mingw 默认的 default-manifest.o，否则与 manifest.rc 里的
  #         DPI 清单冲突（multiple non-default manifests）
  target_link_options(Tickly PRIVATE -municode -mwindows -static
    "-specs=${CMAKE_CURRENT_SOURCE_DIR}/cmake/mingw64.specs")
endif()

# llvm-rc（clang-cl 工具链的默认 RC）既不认 .rc 里的 #pragma code_page，
# 也不认 --codepage，只能用 /C 指定输入代码页；否则 VERSIONINFO 里的中文
# 会报 "Non-ASCII 8-bit codepoint can't be interpreted in the current codepage"。
# 这段必须放在 MSVC/非 MSVC 分支之外：clang-cl 也走 MSVC 分支。
if(CMAKE_RC_COMPILER MATCHES "llvm-rc")
  string(APPEND CMAKE_RC_FLAGS " /C65001")
endif()
