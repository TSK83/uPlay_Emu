// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — ImGui Overlay Implementation
// ═══════════════════════════════════════════════════════════════════════
//  Renders the togglable multiplayer dashboard with three tabs:
//    1. Server Browser — fetches and displays active lobbies
//    2. Host/Join — create games, direct IP connect, join codes
//    3. Developer Console — real-time filtered log viewer
// ═══════════════════════════════════════════════════════════════════════

#include "imgui_overlay.hpp"
#include "input_hook.hpp"
#include "logger.hpp"

#include <imgui.h>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>

using json = nlohmann::json;

namespace acu {

ImGuiOverlay& ImGuiOverlay::Instance() {
    static ImGuiOverlay s_instance;
    return s_instance;
}

void ImGuiOverlay::SetMasterServer(const std::string& ip, uint16_t port) {
    m_masterIp   = ip;
    m_masterPort = port;
}

// ═══════════════════════════════════════════════════════════════════════
//  Theme Setup — Dark Cyberpunk Aesthetic
// ═══════════════════════════════════════════════════════════════════════

void ImGuiOverlay::SetupTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors    = style.Colors;

    // ── Window / Frame ────────────────────────────────────────────────
    style.WindowRounding    = 8.0f;
    style.FrameRounding     = 4.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.WindowPadding     = ImVec2(12, 12);
    style.FramePadding      = ImVec2(8, 4);
    style.ItemSpacing       = ImVec2(8, 6);
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.WindowTitleAlign  = ImVec2(0.5f, 0.5f);

    // ── Color Palette (Dark Purple / Cyan) ────────────────────────────
    ImVec4 bg_dark     = ImVec4(0.06f, 0.06f, 0.10f, 0.95f);
    ImVec4 bg_mid      = ImVec4(0.10f, 0.10f, 0.16f, 1.00f);
    ImVec4 bg_light    = ImVec4(0.14f, 0.14f, 0.22f, 1.00f);
    ImVec4 accent_cyan = ImVec4(0.00f, 0.85f, 0.90f, 1.00f);
    ImVec4 accent_purple = ImVec4(0.55f, 0.25f, 0.85f, 1.00f);
    ImVec4 accent_pink = ImVec4(0.90f, 0.25f, 0.60f, 1.00f);
    ImVec4 text_bright = ImVec4(0.92f, 0.93f, 0.96f, 1.00f);
    ImVec4 text_dim    = ImVec4(0.55f, 0.56f, 0.62f, 1.00f);
    ImVec4 border      = ImVec4(0.20f, 0.20f, 0.30f, 0.60f);

    colors[ImGuiCol_WindowBg]             = bg_dark;
    colors[ImGuiCol_ChildBg]              = ImVec4(0.08f, 0.08f, 0.13f, 1.00f);
    colors[ImGuiCol_PopupBg]              = ImVec4(0.08f, 0.08f, 0.13f, 0.98f);
    colors[ImGuiCol_Border]               = border;
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    colors[ImGuiCol_Text]                 = text_bright;
    colors[ImGuiCol_TextDisabled]         = text_dim;

    colors[ImGuiCol_FrameBg]              = bg_mid;
    colors[ImGuiCol_FrameBgHovered]       = bg_light;
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.18f, 0.18f, 0.28f, 1.00f);

    colors[ImGuiCol_TitleBg]              = ImVec4(0.06f, 0.06f, 0.10f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.10f, 0.08f, 0.18f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.06f, 0.06f, 0.10f, 0.80f);

    colors[ImGuiCol_Tab]                  = bg_mid;
    colors[ImGuiCol_TabHovered]           = ImVec4(accent_cyan.x, accent_cyan.y, accent_cyan.z, 0.40f);
    colors[ImGuiCol_TabSelected]          = ImVec4(accent_cyan.x, accent_cyan.y, accent_cyan.z, 0.25f);

    colors[ImGuiCol_Button]              = ImVec4(accent_purple.x, accent_purple.y, accent_purple.z, 0.60f);
    colors[ImGuiCol_ButtonHovered]       = ImVec4(accent_purple.x, accent_purple.y, accent_purple.z, 0.80f);
    colors[ImGuiCol_ButtonActive]        = ImVec4(accent_cyan.x, accent_cyan.y, accent_cyan.z, 0.80f);

    colors[ImGuiCol_Header]              = ImVec4(accent_purple.x, accent_purple.y, accent_purple.z, 0.30f);
    colors[ImGuiCol_HeaderHovered]       = ImVec4(accent_purple.x, accent_purple.y, accent_purple.z, 0.50f);
    colors[ImGuiCol_HeaderActive]        = ImVec4(accent_cyan.x, accent_cyan.y, accent_cyan.z, 0.50f);

    colors[ImGuiCol_Separator]           = border;
    colors[ImGuiCol_SeparatorHovered]    = accent_cyan;
    colors[ImGuiCol_SeparatorActive]     = accent_cyan;

    colors[ImGuiCol_ResizeGrip]          = ImVec4(accent_cyan.x, accent_cyan.y, accent_cyan.z, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]   = ImVec4(accent_cyan.x, accent_cyan.y, accent_cyan.z, 0.50f);
    colors[ImGuiCol_ResizeGripActive]    = ImVec4(accent_cyan.x, accent_cyan.y, accent_cyan.z, 0.80f);

    colors[ImGuiCol_ScrollbarBg]         = bg_dark;
    colors[ImGuiCol_ScrollbarGrab]       = bg_light;
    colors[ImGuiCol_ScrollbarGrabHovered]= ImVec4(accent_cyan.x, accent_cyan.y, accent_cyan.z, 0.40f);
    colors[ImGuiCol_ScrollbarGrabActive] = accent_cyan;

    colors[ImGuiCol_CheckMark]           = accent_cyan;
    colors[ImGuiCol_SliderGrab]          = accent_purple;
    colors[ImGuiCol_SliderGrabActive]    = accent_cyan;

    colors[ImGuiCol_TableHeaderBg]       = ImVec4(0.12f, 0.12f, 0.20f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]   = border;
    colors[ImGuiCol_TableBorderLight]    = ImVec4(0.15f, 0.15f, 0.22f, 1.00f);
    colors[ImGuiCol_TableRowBg]          = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    colors[ImGuiCol_TableRowBgAlt]       = ImVec4(0.08f, 0.08f, 0.12f, 0.50f);

    LOG_UI("ImGui theme applied: Dark Cyberpunk");
}

// ═══════════════════════════════════════════════════════════════════════
//  Main Render Entry Point
// ═══════════════════════════════════════════════════════════════════════

void ImGuiOverlay::Render() {
    if (!InputHook::Instance().IsOverlayVisible()) {
        return;
    }

    // Set initial window size and position (centered)
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 windowSize(720, 520);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f)
    );

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("ACU Custom Client  |  Multiplayer Dashboard", nullptr, flags)) {
        // Version info
        ImGui::TextColored(ImVec4(0.55f, 0.56f, 0.62f, 1.0f), "v1.0.0 | Press F2/Insert to toggle");
        ImGui::Separator();
        ImGui::Spacing();

        // Tab bar
        if (ImGui::BeginTabBar("MainTabs", ImGuiTabBarFlags_Reorderable)) {
            if (ImGui::BeginTabItem("Server Browser")) {
                RenderServerBrowser();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Host / Join")) {
                RenderHostJoin();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Developer Console")) {
                RenderConsole();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

// ═══════════════════════════════════════════════════════════════════════
//  Tab 1: Server Browser
// ═══════════════════════════════════════════════════════════════════════

void ImGuiOverlay::RenderServerBrowser() {
    ImGui::Spacing();

    // Refresh button
    if (ImGui::Button("Refresh", ImVec2(100, 0))) {
        FetchServerList();
    }
    ImGui::SameLine();

    if (m_fetchInProgress) {
        ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.90f, 1.0f), "Fetching...");
    } else if (!m_fetchError.empty()) {
        ImGui::TextColored(ImVec4(0.90f, 0.25f, 0.25f, 1.0f), "Error: %s", m_fetchError.c_str());
    } else {
        std::lock_guard lock(m_serversMutex);
        ImGui::Text("%zu server(s) found", m_servers.size());
    }

    ImGui::Spacing();

    // Server list table
    ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY |
                                  ImGuiTableFlags_SizingStretchProp;

    if (ImGui::BeginTable("ServerList", 6, tableFlags, ImVec2(0, -30))) {
        ImGui::TableSetupColumn("Server Name",  ImGuiTableColumnFlags_None, 3.0f);
        ImGui::TableSetupColumn("Map",          ImGuiTableColumnFlags_None, 2.0f);
        ImGui::TableSetupColumn("Players",      ImGuiTableColumnFlags_None, 1.2f);
        ImGui::TableSetupColumn("Join Code",    ImGuiTableColumnFlags_None, 1.5f);
        ImGui::TableSetupColumn("Ping",         ImGuiTableColumnFlags_None, 0.8f);
        ImGui::TableSetupColumn("Action",       ImGuiTableColumnFlags_None, 1.0f);
        ImGui::TableHeadersRow();

        std::lock_guard lock(m_serversMutex);
        for (size_t i = 0; i < m_servers.size(); ++i) {
            const auto& srv = m_servers[i];
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::Text("%s", srv.host_name.c_str());

            ImGui::TableNextColumn();
            ImGui::Text("%s", srv.map_name.c_str());

            ImGui::TableNextColumn();
            // Color-code player count
            float ratio = srv.max_players > 0 ? static_cast<float>(srv.current_players) / srv.max_players : 0;
            ImVec4 playerColor = ratio >= 1.0f ? ImVec4(0.9f, 0.25f, 0.25f, 1.0f) :
                                 ratio >= 0.75f ? ImVec4(0.9f, 0.7f, 0.2f, 1.0f) :
                                                  ImVec4(0.3f, 0.9f, 0.4f, 1.0f);
            ImGui::TextColored(playerColor, "%d / %d", srv.current_players, srv.max_players);

            ImGui::TableNextColumn();
            ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.9f, 1.0f), "%s", srv.join_code.c_str());

            ImGui::TableNextColumn();
            if (srv.ping_ms < 50)
                ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "%dms", srv.ping_ms);
            else if (srv.ping_ms < 100)
                ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "%dms", srv.ping_ms);
            else
                ImGui::TextColored(ImVec4(0.9f, 0.25f, 0.25f, 1.0f), "%dms", srv.ping_ms);

            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(i));
            if (srv.current_players < srv.max_players) {
                if (ImGui::SmallButton("Join")) {
                    LOG_NET("Joining server: %s (code: %s)", srv.host_name.c_str(), srv.join_code.c_str());
                    JoinByCode(srv.join_code);
                }
            } else {
                ImGui::TextDisabled("Full");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Auto-refresh every 15 seconds
    float currentTime = static_cast<float>(ImGui::GetTime());
    if (currentTime - m_lastFetchTime > 15.0f && !m_fetchInProgress) {
        FetchServerList();
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  Tab 2: Host / Join
// ═══════════════════════════════════════════════════════════════════════

void ImGuiOverlay::RenderHostJoin() {
    ImGui::Spacing();

    // ── Host Section ──────────────────────────────────────────────────
    ImGui::TextColored(ImVec4(0.55f, 0.25f, 0.85f, 1.0f), "HOST A GAME");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::InputText("Server Name", m_hostNameBuf, sizeof(m_hostNameBuf));
    ImGui::SliderInt("Max Players", &m_maxPlayers, 2, 8);

    ImGui::Spacing();
    if (!m_isHosting) {
        if (ImGui::Button("Host Game", ImVec2(200, 36))) {
            HostGame();
        }
    } else {
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.4f, 1.0f), "Hosting active!");
        if (ImGui::Button("Stop Hosting", ImVec2(200, 36))) {
            m_isHosting = false;
            m_hostStatus = "Stopped";
            LOG_NET("Hosting stopped");
        }
    }

    if (!m_hostStatus.empty()) {
        ImGui::SameLine();
        ImGui::Text("%s", m_hostStatus.c_str());
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // ── Join Section ──────────────────────────────────────────────────
    ImGui::TextColored(ImVec4(0.0f, 0.85f, 0.90f, 1.0f), "JOIN A GAME");
    ImGui::Separator();
    ImGui::Spacing();

    // Join by code
    ImGui::InputText("Join Code", m_joinCodeBuf, sizeof(m_joinCodeBuf));
    ImGui::SameLine();
    if (ImGui::Button("Join##Code", ImVec2(80, 0))) {
        if (strlen(m_joinCodeBuf) > 0) {
            LOG_NET("Joining by code: %s", m_joinCodeBuf);
            JoinByCode(m_joinCodeBuf);
        }
    }

    ImGui::Spacing();

    // Join by direct IP
    ImGui::InputText("Direct IP", m_directIpBuf, sizeof(m_directIpBuf));
    ImGui::SameLine();
    if (ImGui::Button("Connect##IP", ImVec2(80, 0))) {
        if (strlen(m_directIpBuf) > 0) {
            // Parse ip:port
            std::string addr(m_directIpBuf);
            std::string ip;
            uint16_t port = 3074;
            auto colon = addr.rfind(':');
            if (colon != std::string::npos) {
                ip   = addr.substr(0, colon);
                port = static_cast<uint16_t>(std::stoi(addr.substr(colon + 1)));
            } else {
                ip = addr;
            }
            LOG_NET("Direct connect: %s:%u", ip.c_str(), port);
            JoinByIP(ip, port);
        }
    }

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.55f, 0.56f, 0.62f, 1.0f),
        "For VLAN: Use your Radmin VPN or ZeroTier IP address");
}

// ═══════════════════════════════════════════════════════════════════════
//  Tab 3: Developer Console
// ═══════════════════════════════════════════════════════════════════════

void ImGuiOverlay::RenderConsole() {
    ImGui::Spacing();

    // Category filter checkboxes
    ImGui::Text("Filter:");
    ImGui::SameLine();

    const char* catNames[] = { "ALL", "NET", "PRUDP", "PIA", "ENGINE", "UI", "AUTH", "HOOK" };
    for (int i = 0; i < 8; ++i) {
        bool enabled = (m_consoleCategoryMask & (1u << i)) != 0;
        if (ImGui::Checkbox(catNames[i], &enabled)) {
            if (i == 0) {
                // ALL toggle
                m_consoleCategoryMask = enabled ? 0xFFFFFFFF : 0;
            } else {
                if (enabled)
                    m_consoleCategoryMask |= (1u << i);
                else
                    m_consoleCategoryMask &= ~(1u << i);
            }
        }
        if (i < 7) ImGui::SameLine();
    }

    // Text filter
    ImGui::InputText("Search", m_consoleFilterBuf, sizeof(m_consoleFilterBuf));
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_consoleAutoScroll);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        Logger::Instance().Clear();
    }

    ImGui::Separator();

    // Log display
    ImGui::BeginChild("ConsoleLog", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);

    std::string filterStr(m_consoleFilterBuf);
    bool hasFilter = !filterStr.empty();

    // Color map for categories
    ImVec4 catColors[] = {
        ImVec4(0.85f, 0.85f, 0.85f, 1.0f),  // GENERAL
        ImVec4(0.40f, 0.80f, 1.00f, 1.0f),  // NET
        ImVec4(1.00f, 0.70f, 0.30f, 1.0f),  // PRUDP
        ImVec4(0.50f, 1.00f, 0.50f, 1.0f),  // PIA
        ImVec4(0.90f, 0.90f, 0.50f, 1.0f),  // ENGINE
        ImVec4(0.70f, 0.50f, 1.00f, 1.0f),  // UI
        ImVec4(1.00f, 0.50f, 0.70f, 1.0f),  // AUTH
        ImVec4(0.60f, 0.60f, 0.60f, 1.0f),  // HOOK
    };

    Logger::Instance().ForEachFiltered(m_consoleCategoryMask, [&](const LogEntry& entry) {
        // Apply text filter
        if (hasFilter && entry.message.find(filterStr) == std::string::npos) {
            return;
        }

        int catIdx = static_cast<int>(entry.category);
        if (catIdx < 0 || catIdx >= 8) catIdx = 0;

        // Timestamp in dim color
        ImGui::TextColored(ImVec4(0.45f, 0.45f, 0.50f, 1.0f), "[%s]", entry.timestamp.c_str());
        ImGui::SameLine();

        // Category tag
        ImGui::TextColored(catColors[catIdx], "[%s]", LogCategoryToString(entry.category));
        ImGui::SameLine();

        // Message
        if (entry.color != 0) {
            ImVec4 customColor(
                ((entry.color >> 24) & 0xFF) / 255.0f,
                ((entry.color >> 16) & 0xFF) / 255.0f,
                ((entry.color >> 8) & 0xFF) / 255.0f,
                (entry.color & 0xFF) / 255.0f
            );
            ImGui::TextColored(customColor, "%s", entry.message.c_str());
        } else {
            ImGui::TextUnformatted(entry.message.c_str());
        }
    });

    if (m_consoleAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }

    ImGui::EndChild();
}

// ═══════════════════════════════════════════════════════════════════════
//  Network Operations
// ═══════════════════════════════════════════════════════════════════════

void ImGuiOverlay::FetchServerList() {
    if (m_fetchInProgress) return;
    m_fetchInProgress = true;
    m_fetchError.clear();
    m_lastFetchTime = static_cast<float>(ImGui::GetTime());

    // Run HTTP request on background thread to avoid blocking the render thread
    std::thread([this]() {
        try {
            httplib::Client cli(m_masterIp, m_masterPort);
            cli.set_connection_timeout(3, 0);
            cli.set_read_timeout(5, 0);

            auto res = cli.Get("/servers?limit=50");

            if (res && res->status == 200) {
                auto j = json::parse(res->body);
                std::vector<ServerEntry> newServers;

                if (j.contains("servers") && j["servers"].is_array()) {
                    for (const auto& s : j["servers"]) {
                        ServerEntry entry;
                        entry.id              = s.value("id", "");
                        entry.host_name       = s.value("host_name", "Unknown");
                        entry.game_id         = s.value("game_id", "");
                        entry.join_code       = s.value("join_code", "");
                        entry.host_ip         = s.value("host_ip", "");
                        entry.current_players = s.value("current_players", 0);
                        entry.max_players     = s.value("max_players", 4);
                        entry.ping_ms         = s.value("ping_ms", 0);
                        entry.map_name        = s.value("map_name", "Unknown");
                        newServers.push_back(std::move(entry));
                    }
                }

                {
                    std::lock_guard lock(m_serversMutex);
                    m_servers = std::move(newServers);
                }

                LOG_NET("Server list refreshed: %zu servers", m_servers.size());
            } else {
                m_fetchError = res ? ("HTTP " + std::to_string(res->status)) : "Connection failed";
                LOG_NET("Server list fetch failed: %s", m_fetchError.c_str());
            }
        } catch (const std::exception& e) {
            m_fetchError = e.what();
            LOG_NET("Server list fetch exception: %s", e.what());
        }

        m_fetchInProgress = false;
    }).detach();
}

void ImGuiOverlay::HostGame() {
    m_isHosting  = true;
    m_hostStatus = "Registering...";

    std::string hostName(m_hostNameBuf);
    int maxPlayers = m_maxPlayers;

    std::thread([this, hostName, maxPlayers]() {
        try {
            httplib::Client cli(m_masterIp, m_masterPort);
            cli.set_connection_timeout(3, 0);

            json payload = {
                {"host_name",          hostName},
                {"max_players",        maxPlayers},
                {"current_players",    1},
                {"game_id",            "ACU_v1.5.0"},
                {"map_name",           "Paris_FreeRoam"},
                {"host_socket_id",     "local_host_001"},
                {"heartbeat_interval", 30000}
            };

            auto res = cli.Post("/servers", payload.dump(), "application/json");

            if (res && res->status == 201) {
                auto j = json::parse(res->body);
                std::string joinCode = j.value("join_code", "??????");
                m_hostStatus = "Hosted! Code: " + joinCode;
                LOG_NET("Game hosted successfully. Join code: %s", joinCode.c_str());

                // TODO: RE-REQUIRED — Call engine's internal CreateSession function
                // This requires AOB scanning for the session creation vtable in ACU.exe
                LOG_ENGINE("TODO: Invoke engine CreateSession (requires RE of session manager)");

            } else {
                m_hostStatus = "Failed to register";
                LOG_NET("Host registration failed: %s",
                        res ? res->body.c_str() : "Connection failed");
                m_isHosting = false;
            }
        } catch (const std::exception& e) {
            m_hostStatus = std::string("Error: ") + e.what();
            LOG_NET("Host exception: %s", e.what());
            m_isHosting = false;
        }
    }).detach();
}

void ImGuiOverlay::JoinByCode(const std::string& code) {
    std::thread([this, code]() {
        try {
            httplib::Client cli(m_masterIp, m_masterPort);
            cli.set_connection_timeout(3, 0);

            auto res = cli.Get("/servers/join/" + code);

            if (res && res->status == 200) {
                auto j = json::parse(res->body);
                std::string hostIp = j.value("host_ip", "");
                int port = j.value("host_port", 3074);

                LOG_NET("Join code resolved: %s -> %s:%d", code.c_str(), hostIp.c_str(), port);

                // TODO: RE-REQUIRED — Call engine's internal JoinSession function
                // This bypasses the Scaleform (Flash) UI and directly invokes the
                // engine's multiplayer connection state machine.
                LOG_ENGINE("TODO: Invoke engine JoinSession at %s:%d (requires RE)", hostIp.c_str(), port);

            } else {
                LOG_NET("Join code lookup failed: %s",
                        res ? res->body.c_str() : "Connection failed");
            }
        } catch (const std::exception& e) {
            LOG_NET("JoinByCode exception: %s", e.what());
        }
    }).detach();
}

void ImGuiOverlay::JoinByIP(const std::string& ip, uint16_t port) {
    LOG_NET("Direct connect initiated: %s:%u", ip.c_str(), port);

    // TODO: RE-REQUIRED — Call engine's internal JoinSession function directly
    // This bypasses NAT punchthrough and the Scaleform UI, forcing a direct
    // UDP socket bind to the specified IP (works with Radmin VPN / ZeroTier).
    LOG_ENGINE("TODO: Invoke engine JoinSession at %s:%u (requires RE)", ip.c_str(), port);
}

} // namespace acu
