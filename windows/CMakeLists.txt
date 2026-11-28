cmake_minimum_required(VERSION 3.21)
project(Dropocalypse LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static C runtime: the .exe runs without installing the Visual C++ Redistributable
# (which would need admin rights).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(APP_VERSION "0.0.0-dev" CACHE STRING "Version shown in the .exe properties")

include(FetchContent)
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.6.tar.gz
    URL_HASH SHA256=c5fbc5dcab1d46064001c3b84d7a88812985cde7e0e9ced03f5677bec1ba502a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(imgui)

add_executable(Dropocalypse WIN32
    src/main.cpp
    src/audio.cpp
    src/renderer.cpp
    src/logo.cpp
    src/recorder.cpp
    src/milkdrop_host.cpp
    ../shared/remote/remote_server.cpp
    ../shared/remote/qrcodegen.c
    src/remote_json.h
    src/recorder.h
    src/milkdrop.h
    src/milkdrop_host.h
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

target_include_directories(Dropocalypse PRIVATE src ../shared/remote ../shared/remote/include ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(Dropocalypse PRIVATE UNICODE _UNICODE APP_VERSION_STR="${APP_VERSION}")
option(DROPOCALYPSE_STORE "Build the Microsoft Store edition (no built-in updater)" OFF)
if(DROPOCALYPSE_STORE)
    target_compile_definitions(Dropocalypse PRIVATE STORE_EDITION)
endif()
target_compile_options(Dropocalypse PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W3 /permissive- /utf-8 /MP>)
target_link_libraries(Dropocalypse PRIVATE
    d3d11 dxgi d3dcompiler opengl32 ole32 oleaut32 avrt winmm windowscodecs shell32 uuid propsys winhttp bcrypt crypt32 wintrust mfplat mfreadwrite mfuuid ws2_32)

include(CTest)
if(BUILD_TESTING)
    add_executable(dropocalypse_tests tests/core_logic_test.cpp)
    target_include_directories(dropocalypse_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_features(dropocalypse_tests PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(dropocalypse_tests PRIVATE /W3 /permissive- /utf-8)
    else()
        target_compile_options(dropocalypse_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME core_logic COMMAND dropocalypse_tests)
    add_executable(remote_server_tests ../shared/remote/tests/remote_server_test.cpp
        ../shared/remote/remote_server.cpp ../shared/remote/qrcodegen.c)
    target_include_directories(remote_server_tests PRIVATE ../shared/remote ../shared/remote/include)
    target_link_libraries(remote_server_tests PRIVATE ws2_32)
    add_test(NAME remote_server COMMAND remote_server_tests)
endif()
