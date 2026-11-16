cmake_minimum_required(VERSION 3.21)
project(Dropocalypse LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static C runtime: the .exe runs without installing the Visual C++ Redistributable
# (which would need admin rights).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(APP_VERSION "0.0.0-dev" CACHE STRING "Version shown in the .exe properties")

include(FetchContent)
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.6.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(imgui)

add_executable(Dropocalypse WIN32
    src/main.cpp
    src/audio.cpp
    src/renderer.cpp
    src/logo.cpp
    src/audio.h
    src/renderer.h
    src/scenes.h
    src/shaders.h
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    app.manifest
    dropocalypse.rc
)

target_include_directories(Dropocalypse PRIVATE src ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(Dropocalypse PRIVATE UNICODE _UNICODE APP_VERSION_STR="${APP_VERSION}")
target_compile_options(Dropocalypse PRIVATE /W3 /permissive- /utf-8 /MP)
target_link_libraries(Dropocalypse PRIVATE
    d3d11 dxgi d3dcompiler ole32 oleaut32 avrt winmm windowscodecs shell32 uuid propsys winhttp bcrypt)
