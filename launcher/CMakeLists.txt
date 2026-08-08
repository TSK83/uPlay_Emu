# ── ACU-Launcher (Bootstrapper EXE) ────────────────────────────────────
add_executable(ACU-Launcher
    src/main.cpp
    src/injector.cpp
)

target_include_directories(ACU-Launcher PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/shared
)

target_link_libraries(ACU-Launcher PRIVATE
    shlwapi.lib
)

set_target_properties(ACU-Launcher PROPERTIES
    OUTPUT_NAME "ACU-Launcher"
    VS_DEBUGGER_COMMAND_ARGUMENTS "--game-dir \"C:\\Games\\ACU\""
)
