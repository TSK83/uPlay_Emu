#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Dedicated Server Mode
// ═══════════════════════════════════════════════════════════════════════

#ifndef ACU_DEDICATED_MODE_HPP
#define ACU_DEDICATED_MODE_HPP

#include <cstdint>
#include <string>
#include <atomic>

namespace acu {

class DedicatedMode {
public:
    static DedicatedMode& Instance();

    /// Initialize dedicated server mode. Disables rendering and audio.
    void Init(const std::string& masterIp, uint16_t masterPort);

    /// Start the heartbeat loop (runs on a background thread).
    void StartHeartbeat();

    /// Stop the heartbeat loop.
    void StopHeartbeat();

    /// Is dedicated mode active?
    bool IsActive() const { return m_active; }

private:
    DedicatedMode() = default;
    DedicatedMode(const DedicatedMode&) = delete;
    DedicatedMode& operator=(const DedicatedMode&) = delete;

    void DisableRendering();
    void DisableAudio();
    void AutoCreateSession();
    void HeartbeatLoop();

    bool              m_active = false;
    std::atomic<bool> m_heartbeatRunning{false};
    std::string       m_masterIp;
    uint16_t          m_masterPort = 3000;
    std::string       m_serverId;
};

} // namespace acu

#endif // ACU_DEDICATED_MODE_HPP
