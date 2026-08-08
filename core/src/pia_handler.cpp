// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Pia P2P Mesh Protocol Handler Implementation
// ═══════════════════════════════════════════════════════════════════════

#include "pia_handler.hpp"
#include "logger.hpp"

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <iphlpapi.h>
#include <cstring>
#include <random>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace acu {

PiaHandler& PiaHandler::Instance() {
    static PiaHandler s_instance;
    return s_instance;
}

void PiaHandler::Init() {
    std::lock_guard lock(m_mutex);
    if (m_initialized) return;

    // Generate a random connection ID in [2, 255]
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(2, 255);
    m_localConnectionId = static_cast<uint8_t>(dist(gen));

    m_nextPacketId = 1;

    // Detect VLAN adapters for direct mode
    std::string vlanIp = DetectVLANAdapter();
    if (!vlanIp.empty()) {
        LOG_PIA("Detected VLAN adapter IP: %s", vlanIp.c_str());
        LOG_PIA("Direct P2P mode available — bypassing NAT punchthrough");
        m_directBindIp = vlanIp;
        m_directMode = true;
    }

    m_initialized = true;
    LOG_PIA("Pia Handler initialized (connectionId=%u, directMode=%s)",
            m_localConnectionId, m_directMode ? "true" : "false");
}

void PiaHandler::LogPacket(const char* direction, const PiaHeader& header,
                            int totalLen, const char* peerInfo) const {
    LOG_PIA("%s Pia | connId=%u | pktId=%u | ver=%u | %d bytes | peer=%s",
            direction, header.connectionId, header.packetId,
            header.version, totalLen, peerInfo ? peerInfo : "unknown");
}

void PiaHandler::OnIncomingPacket(const uint8_t* data, int length,
                                   const sockaddr* from, int fromlen) {
    std::lock_guard lock(m_mutex);

    if (length < static_cast<int>(sizeof(PiaHeader))) return;

    PiaHeader header;
    memcpy(&header, data, sizeof(PiaHeader));

    // Validate magic
    if (header.magic != 0x6498AB32) return;  // 0x32 0xAB 0x98 0x64 little-endian

    // Extract peer address
    char peerStr[INET6_ADDRSTRLEN + 8] = "unknown";
    if (from && fromlen >= static_cast<int>(sizeof(sockaddr_in))) {
        const auto* addr4 = reinterpret_cast<const sockaddr_in*>(from);
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr4->sin_addr, ipStr, sizeof(ipStr));
        snprintf(peerStr, sizeof(peerStr), "%s:%u", ipStr, ntohs(addr4->sin_port));
    }

    LogPacket("<<IN ", header, length, peerStr);
}

void PiaHandler::OnOutgoingPacket(const uint8_t* data, int length,
                                    const sockaddr* to, int tolen) {
    std::lock_guard lock(m_mutex);

    if (length < static_cast<int>(sizeof(PiaHeader))) return;

    PiaHeader header;
    memcpy(&header, data, sizeof(PiaHeader));

    if (header.magic != 0x6498AB32) return;

    // Extract destination address
    char peerStr[INET6_ADDRSTRLEN + 8] = "unknown";
    if (to && tolen >= static_cast<int>(sizeof(sockaddr_in))) {
        const auto* addr4 = reinterpret_cast<const sockaddr_in*>(to);
        char ipStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr4->sin_addr, ipStr, sizeof(ipStr));
        snprintf(peerStr, sizeof(peerStr), "%s:%u", ipStr, ntohs(addr4->sin_port));
    }

    LogPacket(">>OUT", header, length, peerStr);

    // Track packet ID for rollover detection
    // When the 16-bit integer rolls over from 65535, skip 0 and reset to 1
    if (header.packetId == 65535) {
        LOG_PIA("Packet ID rollover detected — next ID will be 1 (skipping 0)");
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  VLAN Adapter Detection
// ═══════════════════════════════════════════════════════════════════════
//  Scans network adapters for known VLAN software (Radmin VPN, ZeroTier,
//  Hamachi, etc.) and returns the IP address of the first match.
// ═══════════════════════════════════════════════════════════════════════

std::string PiaHandler::DetectVLANAdapter() {
    ULONG bufSize = 0;
    GetAdaptersAddresses(AF_INET, 0, nullptr, nullptr, &bufSize);
    if (bufSize == 0) return "";

    std::vector<uint8_t> buffer(bufSize);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());

    DWORD result = GetAdaptersAddresses(AF_INET, 0, nullptr, adapters, &bufSize);
    if (result != NO_ERROR) return "";

    // Known VLAN adapter name patterns (case-insensitive search)
    static const wchar_t* vlanPatterns[] = {
        L"Radmin",
        L"ZeroTier",
        L"Hamachi",
        L"TAP-Windows",
        L"WireGuard",
    };

    for (auto* adapter = adapters; adapter; adapter = adapter->Next) {
        // Check adapter description for VLAN patterns
        std::wstring desc(adapter->Description);

        for (const auto* pattern : vlanPatterns) {
            if (desc.find(pattern) != std::wstring::npos) {
                // Found a VLAN adapter — get its first unicast IP
                for (auto* ua = adapter->FirstUnicastAddress; ua; ua = ua->Next) {
                    auto* addr4 = reinterpret_cast<sockaddr_in*>(ua->Address.lpSockaddr);
                    if (addr4->sin_family == AF_INET) {
                        char ipStr[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &addr4->sin_addr, ipStr, sizeof(ipStr));

                        // Skip 0.0.0.0 and 127.x.x.x
                        if (strcmp(ipStr, "0.0.0.0") != 0 &&
                            strncmp(ipStr, "127.", 4) != 0) {
                            return std::string(ipStr);
                        }
                    }
                }
            }
        }
    }

    return "";
}

void PiaHandler::SetDirectBindIP(const std::string& ip) {
    std::lock_guard lock(m_mutex);
    m_directBindIp = ip;
    m_directMode   = true;
    LOG_PIA("Direct bind IP set: %s", ip.c_str());
}

} // namespace acu
