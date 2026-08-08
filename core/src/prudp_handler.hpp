#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — PRUDP V1 Protocol Handler
// ═══════════════════════════════════════════════════════════════════════
//  Implements the Quazal PRUDP V1 transport protocol:
//    - Packet parsing with 0xEA 0xD0 magic number
//    - RC4 stream encryption with substream key mutation
//    - HMAC-MD5 packet signature generation/validation
//    - Three-way handshake state machine
//    - Rendez-Vous ticket granting emulation
// ═══════════════════════════════════════════════════════════════════════

#ifndef ACU_PRUDP_HANDLER_HPP
#define ACU_PRUDP_HANDLER_HPP

#include <cstdint>
#include <vector>
#include <array>
#include <string>
#include <mutex>

namespace acu {

// ── PRUDP V1 Packet Types ────────────────────────────────────────────
enum class PrudpPacketType : uint8_t {
    SYN        = 0,
    CONNECT    = 1,
    DATA       = 2,
    DISCONNECT = 3,
    PING       = 4,
};

// ── PRUDP V1 Flags ───────────────────────────────────────────────────
enum PrudpFlags : uint16_t {
    FLAG_ACK        = 0x001,
    FLAG_RELIABLE   = 0x002,
    FLAG_NEED_ACK   = 0x004,
    FLAG_HAS_SIZE   = 0x008,
    FLAG_MULTI_ACK  = 0x200,
};

// ── PRUDP V1 Parsed Header ──────────────────────────────────────────
#pragma pack(push, 1)
struct PrudpV1Header {
    uint16_t magic;              // 0xEA 0xD0
    uint8_t  version;            // Always 1 for V1
    uint8_t  optionDataLen;      // Length of optional data
    uint16_t payloadSize;        // Encrypted payload size
    uint8_t  srcPort;            // Source port identifier
    uint8_t  dstPort;            // Destination port identifier
    uint16_t typeAndFlags;       // Packet type (bits 0-3) + flags (bits 4-15)
    uint8_t  sessionId;          // Session ID
    uint8_t  substreamId;        // Substream identifier
    uint16_t sequenceId;         // Sequence ID for ordering
    uint8_t  signature[16];      // HMAC-MD5 packet signature
};
#pragma pack(pop)

// ── Connection State ─────────────────────────────────────────────────
enum class PrudpConnectionState : uint8_t {
    Idle,
    SynSent,
    SynReceived,
    Connected,
    Disconnecting,
    Disconnected,
};

class PrudpHandler {
public:
    static PrudpHandler& Instance();

    /// Initialize the PRUDP handler.
    void Init();

    /// Process an incoming PRUDP packet (from recv/recvfrom hook).
    void OnIncomingPacket(const uint8_t* data, int length);

    /// Process an outgoing PRUDP packet (from send/sendto hook).
    void OnOutgoingPacket(const uint8_t* data, int length);

    /// Set the access key for HMAC-MD5 signatures.
    void SetAccessKey(const std::string& hexKey);

    /// Set the session key for RC4 encryption.
    void SetSessionKey(const std::vector<uint8_t>& key);

    /// Get the current connection state.
    PrudpConnectionState GetState() const { return m_state; }

    // ── RC4 Sub-stream Key Mutation ──────────────────────────────────
    /// Mutate an RC4 key for the next substream.
    /// Key mutation: for i in [0, len/2): key[i] = (key[i] + (len/2 + 1) - i) & 0xFF
    static void MutateKey(std::vector<uint8_t>& key);

    // ── HMAC-MD5 Signature ───────────────────────────────────────────
    /// Compute the 16-byte HMAC-MD5 signature for a PRUDP V1 packet.
    static std::array<uint8_t, 16> ComputeSignature(
        const uint8_t* headerBytes,     // Bytes from offset 0x4 to 0xC (8 bytes)
        const uint8_t* sessionKey,      // Session key (may be null for SYN)
        size_t sessionKeyLen,
        const std::string& accessKey,   // 8-char hex access key
        const uint8_t* connSignature,   // Connection signature (16 bytes, may be null)
        const uint8_t* payload,         // Encrypted payload
        size_t payloadLen
    );

    // ── RC4 Encrypt/Decrypt ──────────────────────────────────────────
    static void RC4Crypt(const uint8_t* key, size_t keyLen,
                         const uint8_t* input, uint8_t* output, size_t dataLen);

private:
    PrudpHandler() = default;
    PrudpHandler(const PrudpHandler&) = delete;
    PrudpHandler& operator=(const PrudpHandler&) = delete;

    /// Parse a raw PRUDP V1 header from bytes.
    bool ParseHeader(const uint8_t* data, int length, PrudpV1Header& out) const;

    /// Log a parsed packet for the developer console.
    void LogPacket(const char* direction, const PrudpV1Header& header, int totalLen) const;

    /// Handle state machine transitions.
    void HandleSyn(const PrudpV1Header& header, bool incoming);
    void HandleConnect(const PrudpV1Header& header, const uint8_t* payload, size_t payloadLen, bool incoming);
    void HandleData(const PrudpV1Header& header, const uint8_t* payload, size_t payloadLen, bool incoming);
    void HandleDisconnect(const PrudpV1Header& header, bool incoming);
    void HandlePing(const PrudpV1Header& header, bool incoming);

    // ── State ─────────────────────────────────────────────────────────
    mutable std::mutex         m_mutex;
    PrudpConnectionState       m_state = PrudpConnectionState::Idle;

    std::string                m_accessKey;        // 8-char hex, e.g., "AABBCCDD"
    std::vector<uint8_t>       m_sessionKey;       // Base session key
    std::vector<uint8_t>       m_currentRC4Key;    // Current RC4 key (mutated per substream)
    uint8_t                    m_currentSubstream = 0;

    std::array<uint8_t, 16>    m_connSignature{};  // Connection signature from SYN handshake
    uint8_t                    m_sessionId = 0;
    uint16_t                   m_sequenceId = 0;

    bool                       m_initialized = false;
};

} // namespace acu

#endif // ACU_PRUDP_HANDLER_HPP
