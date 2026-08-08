# ── ACU-Core (Engine Hooks DLL) ────────────────────────────────────────
add_library(ACU-Core SHARED
    src/dllmain.cpp
    src/logger.cpp
    src/integrity_bypass.cpp
    src/dx11_hook.cpp
    src/input_hook.cpp
    src/imgui_overlay.cpp
    src/network_hook.cpp
    src/prudp_handler.cpp
    src/pia_handler.cpp
    src/dedicated_mode.cpp
)

target_include_directories(ACU-Core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/shared
)

target_link_libraries(ACU-Core PRIVATE
    minhook
    imgui_dx11
    nlohmann_json::nlohmann_json
    httplib::httplib
    d3d11.lib
    dxgi.lib
    ws2_32.lib
    crypt32.lib
)

set_target_properties(ACU-Core PROPERTIES
    OUTPUT_NAME "ACU-Core"
)
