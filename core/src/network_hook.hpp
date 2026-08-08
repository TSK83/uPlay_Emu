#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Network Hook (Winsock Interception)
// ═══════════════════════════════════════════════════════════════════════

#ifndef ACU_NETWORK_HOOK_HPP
#define ACU_NETWORK_HOOK_HPP

#include <string>
#include <cstdint>

namespace acu {

class NetworkHook {
public:
    static NetworkHook& Instance();

    /// Install Winsock hooks (gethostbyname, getaddrinfo, send, recv, sendto, recvfrom).
    bool Init(const std::string& masterServerIp, uint16_t masterServerPort);

    /// Remove all Winsock hooks.
    void Shutdown();

    /// Set the redirect target for Ubisoft DNS lookups.
    void SetRedirectTarget(const std::string& ip);

    bool IsInitialized() const { return m_initialized; }

private:
    NetworkHook() = default;
    NetworkHook(const NetworkHook&) = delete;
    NetworkHook& operator=(const NetworkHook&) = delete;

    bool         m_initialized    = false;
    std::string  m_redirectIp     = "127.0.0.1";
    uint16_t     m_masterPort     = 3000;
};

} // namespace acu

#endif // ACU_NETWORK_HOOK_HPP
