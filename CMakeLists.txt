cmake_minimum_required(VERSION 3.25)
project(ACU-CustomClient VERSION 1.0.0 LANGUAGES CXX C)

# ── Global Configuration ───────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Force x64
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This project requires a 64-bit build. Configure with -A x64.")
endif()

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# MSVC-specific flags
if(MSVC)
    add_compile_options(/W3 /MP /utf-8)
    add_compile_definitions(
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        UNICODE
        _UNICODE
    )
    # Release optimizations
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /GL /DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG /OPT:REF /OPT:ICF")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG /OPT:REF /OPT:ICF")
endif()

# ── FetchContent Dependencies ──────────────────────────────────────────
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# MinHook — lightweight x86/x64 API hooking library
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)

# Dear ImGui — immediate mode GUI
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.4-docking
    GIT_SHALLOW    TRUE
)

# nlohmann/json — JSON for Modern C++
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

# cpp-httplib — header-only HTTP/HTTPS client+server
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.3
    GIT_SHALLOW    TRUE
)

# Fetch all dependencies
FetchContent_MakeAvailable(minhook nlohmann_json httplib)

# ImGui needs manual target setup (it's source-embedded, not a CMake project)
FetchContent_MakeAvailable(imgui)
FetchContent_GetProperties(imgui SOURCE_DIR IMGUI_SOURCE_DIR)

# Create ImGui static library with DX11 + Win32 backends
add_library(imgui_dx11 STATIC
    ${IMGUI_SOURCE_DIR}/imgui.cpp
    ${IMGUI_SOURCE_DIR}/imgui_draw.cpp
    ${IMGUI_SOURCE_DIR}/imgui_widgets.cpp
    ${IMGUI_SOURCE_DIR}/imgui_tables.cpp
    ${IMGUI_SOURCE_DIR}/imgui_demo.cpp
    ${IMGUI_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(imgui_dx11 PUBLIC
    ${IMGUI_SOURCE_DIR}
    ${IMGUI_SOURCE_DIR}/backends
)

target_link_libraries(imgui_dx11 PUBLIC
    d3d11.lib
    dxgi.lib
    d3dcompiler.lib
)

# ── Sub-Projects ───────────────────────────────────────────────────────
add_subdirectory(launcher)
add_subdirectory(core)
add_subdirectory(uplay_emu)

# ── Post-Build: Copy outputs to bin/ ───────────────────────────────────
add_custom_target(deploy ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/bin
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:ACU-Launcher> ${CMAKE_SOURCE_DIR}/bin/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:ACU-Core> ${CMAKE_SOURCE_DIR}/bin/
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:uplay_r1_loader64> ${CMAKE_SOURCE_DIR}/bin/
    DEPENDS ACU-Launcher ACU-Core uplay_r1_loader64
    COMMENT "Deploying binaries to bin/"
)
