#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — ImGui Overlay (Server Browser + Host/Join + Console)
// ═══════════════════════════════════════════════════════════════════════

#ifndef ACU_IMGUI_OVERLAY_HPP
#define ACU_IMGUI_OVERLAY_HPP

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>

namespace acu {

/// Represents a server entry from the master server
struct ServerEntry {
    std::string id;
    std::string host_name;
    std::string game_id;
    std::string join_code;
    std::string host_ip;
    int         current_players = 0;
    int         max_players     = 0;
    int         ping_ms         = 0;
    std::string map_name;
};

class ImGuiOverlay {
public:
    static ImGuiOverlay& Instance();

    /// Apply the dark cyberpunk theme to ImGui.
    void SetupTheme();

    /// Render the full overlay (called from hkPresent on the render thread).
    void Render();

    /// Set the master server endpoint for server browser queries.
    void SetMasterServer(const std::string& ip, uint16_t port);

private:
    ImGuiOverlay() = default;
    ImGuiOverlay(const ImGuiOverlay&) = delete;
    ImGuiOverlay& operator=(const ImGuiOverlay&) = delete;

    // Tab renderers
    void RenderServerBrowser();
    void RenderHostJoin();
    void RenderConsole();

    // Network operations (run on background thread, results fed to UI)
    void FetchServerList();
    void HostGame();
    void JoinByCode(const std::string& code);
    void JoinByIP(const std::string& ip, uint16_t port);

    // ── State ─────────────────────────────────────────────────────────
    std::string              m_masterIp   = "127.0.0.1";
    uint16_t                 m_masterPort = 3000;

    // Server browser state
    std::vector<ServerEntry> m_servers;
    std::mutex               m_serversMutex;
    bool                     m_fetchInProgress = false;
    float                    m_lastFetchTime   = 0.0f;
    std::string              m_fetchError;

    // Host/Join state
    char  m_directIpBuf[128]   = "127.0.0.1:3074";
    char  m_joinCodeBuf[16]    = "";
    char  m_hostNameBuf[64]    = "My ACU Server";
    int   m_maxPlayers         = 4;
    bool  m_isHosting          = false;
    std::string m_hostStatus;

    // Console state
    uint32_t m_consoleCategoryMask = 0xFFFFFFFF; // All categories enabled
    bool     m_consoleAutoScroll   = true;
    char     m_consoleFilterBuf[128] = "";
};

} // namespace acu

#endif // ACU_IMGUI_OVERLAY_HPP
