# ── uplay_r1_loader64 (Uplay API Emulator DLL) ────────────────────────
add_library(uplay_r1_loader64 SHARED
    src/dllmain.cpp
    src/uplay_api.cpp
)

target_include_directories(uplay_r1_loader64 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/shared
)

# Module definition file for explicit exports
target_sources(uplay_r1_loader64 PRIVATE
    src/uplay_exports.def
)

set_target_properties(uplay_r1_loader64 PROPERTIES
    OUTPUT_NAME "uplay_r1_loader64"
)
