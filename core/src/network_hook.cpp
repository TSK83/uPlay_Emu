// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Network Hook Implementation
// ═══════════════════════════════════════════════════════════════════════
//  Hooks Winsock functions to:
//    1. Redirect DNS lookups for *.ubisoft.com / *.quazal.net → master server
//    2. Inspect send/recv/sendto/recvfrom for PRUDP and Pia magic numbers
//    3. Log all intercepted network traffic to the developer console
// ═══════════════════════════════════════════════════════════════════════

#include "network_hook.hpp"
#include "prudp_handler.hpp"
#include "pia_handler.hpp"
#include "logger.hpp"

#include <MinHook.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <windns.h>
#include <cstring>
#include <string>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

namespace acu {

// ═══════════════════════════════════════════════════════════════════════
//  Original Function Pointers
// ═══════════════════════════════════════════════════════════════════════

static decltype(&gethostbyname)  o_gethostbyname  = nullptr;
static decltype(&getaddrinfo)    o_getaddrinfo     = nullptr;
static decltype(&send)           o_send            = nullptr;
static decltype(&recv)           o_recv            = nullptr;
static decltype(&sendto)         o_sendto          = nullptr;
static decltype(&recvfrom)       o_recvfrom        = nullptr;

// Static redirect IP storage (accessible from hook functions)
static char s_redirectIpStr[64] = "127.0.0.1";

// ═══════════════════════════════════════════════════════════════════════
//  DNS Domain Matching
// ═══════════════════════════════════════════════════════════════════════

static bool ShouldRedirectDomain(const char* hostname) {
    if (!hostname) return false;

    std::string host(hostname);
    std::transform(host.begin(), host.end(), host.begin(), ::tolower);

    // Redirect all Ubisoft and Quazal domains
    static const char* redirectDomains[] = {
        "ubisoft.com",
        "ubi.com",
        "quazal.net",
        "rdv.ubisoft.com",
        "onlineconfigservice.ubi.com",
        "msr-public-ubiservices.ubi.com",
        "public-ws-ubiservices.ubi.com",
    };

    for (const auto& domain : redirectDomains) {
        if (host.find(domain) != std::string::npos) {
            return true;
        }
    }

    return false;
}

// ═══════════════════════════════════════════════════════════════════════
//  Hooked Functions
// ═══════════════════════════════════════════════════════════════════════

static hostent* WSAAPI hk_gethostbyname(const char* name) {
    if (ShouldRedirectDomain(name)) {
        LOG_NET("DNS Redirect: %s -> %s", name, s_redirectIpStr);

        // Build a static hostent pointing to our master server
        static in_addr  s_addr;
        static in_addr* s_addrList[2] = { &s_addr, nullptr };
        static hostent  s_hostent;

        inet_pton(AF_INET, s_redirectIpStr, &s_addr);

        s_hostent.h_name      = const_cast<char*>(name);
        s_hostent.h_aliases   = nullptr;
        s_hostent.h_addrtype  = AF_INET;
        s_hostent.h_length    = sizeof(in_addr);
        s_hostent.h_addr_list = reinterpret_cast<char**>(s_addrList);

        return &s_hostent;
    }

    return o_gethostbyname(name);
}

static int WSAAPI hk_getaddrinfo(PCSTR pNodeName, PCSTR pServiceName,
                                  const ADDRINFOA* pHints, PADDRINFOA* ppResult) {
    if (ShouldRedirectDomain(pNodeName)) {
        LOG_NET("DNS Redirect (getaddrinfo): %s -> %s", pNodeName, s_redirectIpStr);
        // Redirect by replacing the node name with our IP
        return o_getaddrinfo(s_redirectIpStr, pServiceName, pHints, ppResult);
    }

    return o_getaddrinfo(pNodeName, pServiceName, pHints, ppResult);
}

static int WSAAPI hk_send(SOCKET s, const char* buf, int len, int flags) {
    if (len >= 2) {
        auto magic = *reinterpret_cast<const uint16_t*>(buf);
        if (magic == 0xD0EA) {  // PRUDP magic 0xEA 0xD0 (little-endian)
            LOG_PRUDP("SEND PRUDP packet (%d bytes)", len);
            PrudpHandler::Instance().OnOutgoingPacket(
                reinterpret_cast<const uint8_t*>(buf), len);
        }
    }
    return o_send(s, buf, len, flags);
}

static int WSAAPI hk_recv(SOCKET s, char* buf, int len, int flags) {
    int result = o_recv(s, buf, len, flags);
    if (result > 0 && result >= 2) {
        auto magic = *reinterpret_cast<const uint16_t*>(buf);
        if (magic == 0xD0EA) {
            LOG_PRUDP("RECV PRUDP packet (%d bytes)", result);
            PrudpHandler::Instance().OnIncomingPacket(
                reinterpret_cast<const uint8_t*>(buf), result);
        }
    }
    return result;
}

static int WSAAPI hk_sendto(SOCKET s, const char* buf, int len, int flags,
                             const sockaddr* to, int tolen) {
    if (len >= 4) {
        auto magic = *reinterpret_cast<const uint32_t*>(buf);
        if (magic == 0x6498AB32) {  // Pia magic 0x32 0xAB 0x98 0x64 (little-endian)
            LOG_PIA("SEND Pia P2P packet (%d bytes)", len);
            PiaHandler::Instance().OnOutgoingPacket(
                reinterpret_cast<const uint8_t*>(buf), len, to, tolen);
        } else if (len >= 2 && *reinterpret_cast<const uint16_t*>(buf) == 0xD0EA) {
            LOG_PRUDP("SEND PRUDP/UDP packet (%d bytes)", len);
            PrudpHandler::Instance().OnOutgoingPacket(
                reinterpret_cast<const uint8_t*>(buf), len);
        }
    }
    return o_sendto(s, buf, len, flags, to, tolen);
}

static int WSAAPI hk_recvfrom(SOCKET s, char* buf, int len, int flags,
                               sockaddr* from, int* fromlen) {
    int result = o_recvfrom(s, buf, len, flags, from, fromlen);
    if (result > 0) {
        if (result >= 4) {
            auto magic = *reinterpret_cast<const uint32_t*>(buf);
            if (magic == 0x6498AB32) {
                LOG_PIA("RECV Pia P2P packet (%d bytes)", result);
                PiaHandler::Instance().OnIncomingPacket(
                    reinterpret_cast<const uint8_t*>(buf), result, from, fromlen ? *fromlen : 0);
            }
        }
        if (result >= 2) {
            auto magic = *reinterpret_cast<const uint16_t*>(buf);
            if (magic == 0xD0EA) {
                LOG_PRUDP("RECV PRUDP/UDP packet (%d bytes)", result);
                PrudpHandler::Instance().OnIncomingPacket(
                    reinterpret_cast<const uint8_t*>(buf), result);
            }
        }
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════
//  NetworkHook Singleton
// ═══════════════════════════════════════════════════════════════════════

NetworkHook& NetworkHook::Instance() {
    static NetworkHook s_instance;
    return s_instance;
}

void NetworkHook::SetRedirectTarget(const std::string& ip) {
    m_redirectIp = ip;
    strncpy_s(s_redirectIpStr, ip.c_str(), sizeof(s_redirectIpStr) - 1);
}

bool NetworkHook::Init(const std::string& masterServerIp, uint16_t masterServerPort) {
    if (m_initialized) return true;

    m_redirectIp = masterServerIp;
    m_masterPort = masterServerPort;
    strncpy_s(s_redirectIpStr, masterServerIp.c_str(), sizeof(s_redirectIpStr) - 1);

    LOG_NET("NetworkHook: Initializing Winsock hooks...");
    LOG_NET("NetworkHook: DNS redirect target: %s", s_redirectIpStr);

    // Ensure ws2_32.dll is loaded
    HMODULE hWs2 = GetModuleHandleA("ws2_32.dll");
    if (!hWs2) {
        hWs2 = LoadLibraryA("ws2_32.dll");
    }

    if (!hWs2) {
        LOG_NET("NetworkHook: Failed to load ws2_32.dll");
        return false;
    }

    // Get function addresses
    auto p_gethostbyname = GetProcAddress(hWs2, "gethostbyname");
    auto p_getaddrinfo   = GetProcAddress(hWs2, "getaddrinfo");
    auto p_send          = GetProcAddress(hWs2, "send");
    auto p_recv          = GetProcAddress(hWs2, "recv");
    auto p_sendto        = GetProcAddress(hWs2, "sendto");
    auto p_recvfrom      = GetProcAddress(hWs2, "recvfrom");

    // Create hooks
    bool allOk = true;

    auto createHook = [&](const char* name, LPVOID target, LPVOID detour, LPVOID* original) -> bool {
        if (!target) {
            LOG_NET("NetworkHook: Function '%s' not found", name);
            return false;
        }
        MH_STATUS status = MH_CreateHook(target, detour, original);
        if (status != MH_OK) {
            LOG_NET("NetworkHook: MH_CreateHook failed for '%s' (status: %d)", name, status);
            return false;
        }
        status = MH_EnableHook(target);
        if (status != MH_OK) {
            LOG_NET("NetworkHook: MH_EnableHook failed for '%s' (status: %d)", name, status);
            return false;
        }
        LOG_NET("NetworkHook: Hooked '%s' at 0x%p", name, target);
        return true;
    };

    allOk &= createHook("gethostbyname", (LPVOID)p_gethostbyname, (LPVOID)hk_gethostbyname, (LPVOID*)&o_gethostbyname);
    allOk &= createHook("getaddrinfo",   (LPVOID)p_getaddrinfo,   (LPVOID)hk_getaddrinfo,   (LPVOID*)&o_getaddrinfo);
    allOk &= createHook("send",          (LPVOID)p_send,          (LPVOID)hk_send,           (LPVOID*)&o_send);
    allOk &= createHook("recv",          (LPVOID)p_recv,          (LPVOID)hk_recv,           (LPVOID*)&o_recv);
    allOk &= createHook("sendto",        (LPVOID)p_sendto,        (LPVOID)hk_sendto,         (LPVOID*)&o_sendto);
    allOk &= createHook("recvfrom",      (LPVOID)p_recvfrom,      (LPVOID)hk_recvfrom,       (LPVOID*)&o_recvfrom);

    m_initialized = allOk;
    LOG_NET("NetworkHook: %s (%s)", allOk ? "All hooks installed" : "Some hooks failed",
            allOk ? "OK" : "PARTIAL");

    return allOk;
}

void NetworkHook::Shutdown() {
    if (!m_initialized) return;
    // MinHook's MH_DisableHook(MH_ALL_HOOKS) in the global shutdown handles this
    m_initialized = false;
    LOG_NET("NetworkHook: Shutdown");
}

} // namespace acu
