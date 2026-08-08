#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Pia P2P Mesh Protocol Handler
// ═══════════════════════════════════════════════════════════════════════

#ifndef ACU_PIA_HANDLER_HPP
#define ACU_PIA_HANDLER_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>

struct sockaddr;

namespace acu {

// ── Pia Packet Header (simplified) ──────────────────────────────────
#pragma pack(push, 1)
struct PiaHeader {
    uint32_t magic;          // 0x32 0xAB 0x98 0x64
    uint8_t  version;
    uint8_t  connectionId;   // Random ID in [2, 255]
    uint16_t packetId;       // Monotonically incrementing, skips 0 on rollover
    // Followed by AES-GCM nonce (8 bytes) and auth tag (16 bytes) in modern versions
};
#pragma pack(pop)

class PiaHandler {
public:
    static PiaHandler& Instance();

    void Init();

    /// Process an incoming Pia P2P packet.
    void OnIncomingPacket(const uint8_t* data, int length,
                          const sockaddr* from, int fromlen);

    /// Process an outgoing Pia P2P packet.
    void OnOutgoingPacket(const uint8_t* data, int length,
                          const sockaddr* to, int tolen);

    /// Detect VLAN adapters (Radmin VPN, ZeroTier) and return their IP.
    /// Returns empty string if none found.
    static std::string DetectVLANAdapter();

    /// Force bind to a specific local IP for direct P2P connections.
    void SetDirectBindIP(const std::string& ip);

    bool IsDirectMode() const { return m_directMode; }

private:
    PiaHandler() = default;
    PiaHandler(const PiaHandler&) = delete;
    PiaHandler& operator=(const PiaHandler&) = delete;

    void LogPacket(const char* direction, const PiaHeader& header,
                   int totalLen, const char* peerInfo) const;

    mutable std::mutex m_mutex;
    bool               m_initialized = false;

    // Connection tracking
    uint8_t            m_localConnectionId = 0;
    uint16_t           m_nextPacketId = 1;

    // Direct IP / VLAN mode
    bool               m_directMode = false;
    std::string        m_directBindIp;
};

} // namespace acu

#endif // ACU_PIA_HANDLER_HPP
