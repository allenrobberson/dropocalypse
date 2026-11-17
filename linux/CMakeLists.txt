cmake_minimum_required(VERSION 3.21)
project(Dropocalypse LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(APP_VERSION "0.0.0-dev" CACHE STRING "Version shown in the app")

include(FetchContent)
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.91.6.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(stb
    URL https://github.com/nothings/stb/archive/9600522de53973336fa033a289858a4354cbb04c.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# GLFW is built in (static) so the AppImage works on X11 and Wayland without a system GLFW.
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND ON CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    URL https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(imgui stb glfw)

find_package(OpenGL REQUIRED)
find_package(CURL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse libpulse-simple)
find_package(Threads REQUIRED)

add_executable(dropocalypse
    src/main.cpp
    src/audio.cpp
    src/renderer.cpp
    src/logo.cpp
    src/recorder.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
# windows/src supplies the shared, platform-free headers (scenes.h, core_logic.h).
target_include_directories(dropocalypse PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../windows/src ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends ${stb_SOURCE_DIR})
target_compile_definitions(dropocalypse PRIVATE APP_VERSION_STR="${APP_VERSION}")
target_compile_options(dropocalypse PRIVATE -Wall -Wno-unused-function -Wno-missing-field-initializers)
target_link_libraries(dropocalypse PRIVATE glfw OpenGL::GL CURL::libcurl PkgConfig::PULSE Threads::Threads ${CMAKE_DL_LIBS})

# Unit tests for the shared update / auto-cycle / layout rules (same file the Windows build runs).
add_executable(dropocalypse_tests ../windows/tests/core_logic_test.cpp)
target_include_directories(dropocalypse_tests PRIVATE ../windows/src)

install(TARGETS dropocalypse RUNTIME DESTINATION bin)
install(FILES dropocalypse.desktop DESTINATION share/applications)
install(FILES dropocalypse.png DESTINATION share/icons/hicolor/256x256/apps)
