cmake_minimum_required(VERSION 3.21)
project(taskbar-mediaplayer VERSION 2.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the exe runs without the Visual C++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(taskbar-mediaplayer WIN32
    src/icons.cpp
    src/main.cpp
    src/media.cpp
    src/settings.cpp
    src/settings_dialog.cpp
    src/widget.cpp
    res/app.rc
    res/app.manifest
)

target_compile_definitions(taskbar-mediaplayer PRIVATE
    UNICODE _UNICODE NOMINMAX
    _WIN32_WINNT=0x0A00 WINVER=0x0A00
    APP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    APP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    APP_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
target_compile_options(taskbar-mediaplayer PRIVATE /W4 /permissive- /utf-8 /EHsc /guard:cf)
# Hardening: Control Flow Guard, CET shadow stack, and imported DLLs loaded from System32 only, so a
# DLL planted next to a portable copy (in Downloads, say) is never picked up.
target_link_options(taskbar-mediaplayer PRIVATE /guard:cf /CETCOMPAT /DEPENDENTLOADFLAG:0x800)
target_link_libraries(taskbar-mediaplayer PRIVATE
    windowsapp gdiplus comctl32 dwmapi uxtheme shcore shell32 advapi32
)
