cmake_minimum_required(VERSION 3.16)
project(SnippingTool VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows-specific settings
if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE)
endif()

# Main executable (WIN32 = GUI app, no console)
add_executable(snip WIN32
    src/main.cpp
    src/settings.cpp
    src/hotkeys.cpp
    src/capture.cpp
    src/drawing.cpp
    src/tooltip.cpp
    src/notification.cpp
    src/overlay.cpp
    src/mode_picker.cpp
    src/settings_dlg.cpp
    src/main_window.cpp
)

# Link Windows libraries
target_link_libraries(snip PRIVATE
    d3d11
    dxgi
    windowscodecs
    comdlg32
    msimg32
    dwmapi
    uxtheme
)

# Installation
install(TARGETS snip RUNTIME DESTINATION bin)
