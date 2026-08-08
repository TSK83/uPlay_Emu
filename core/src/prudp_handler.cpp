// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — PRUDP V1 Protocol Handler Implementation
// ═══════════════════════════════════════════════════════════════════════

#include "prudp_handler.hpp"
#include "logger.hpp"

#include <Windows.h>
#include <wincrypt.h>
#include <cstring>
#include <numeric>

#pragma comment(lib, "crypt32.lib")

namespace acu {

PrudpHandler& PrudpHandler::Instance() {
    static PrudpHandler s_instance;
    return s_instance;
}

void PrudpHandler::Init() {
    std::lock_guard lock(m_mutex);
    if (m_initialized) return;

    m_state          = PrudpConnectionState::Idle;
    m_sessionId      = 0;
    m_sequenceId     = 0;
    m_currentSubstream = 0;
    m_connSignature  = {};

    // Default access key (must be extracted from ACU.exe via RE)
    // TODO: RE-REQUIRED — Extract the actual access key from the ACU.exe binary.
    // This is typically an 8-character hex string hardcoded in the executable.
    m_accessKey = "CD45BF17";  // Placeholder — replace with actual key

    m_initialized = true;
    LOG_PRUDP("PRUDP Handler initialized (access key: %s)", m_accessKey.c_str());
}

// ═══════════════════════════════════════════════════════════════════════
//  Packet Parsing
// ═══════════════════════════════════════════════════════════════════════

bool PrudpHandler::ParseHeader(const uint8_t* data, int length, PrudpV1Header& out) const {
    if (length < static_cast<int>(sizeof(PrudpV1Header))) {
        return false;
    }

    memcpy(&out, data, sizeof(PrudpV1Header));

    // Validate magic number (0xEA 0xD0 in little-endian = 0xD0EA as uint16_t)
    if (out.magic != 0xD0EA) {
        return false;
    }

    // Validate version
    if (out.version != 1) {
        return false;
    }

    return true;
}

void PrudpHandler::LogPacket(const char* direction, const PrudpV1Header& header, int totalLen) const {
    const char* typeStr;
    uint8_t type = header.typeAndFlags & 0x0F;
    switch (type) {
        case 0: typeStr = "SYN";        break;
        case 1: typeStr = "CONNECT";    break;
        case 2: typeStr = "DATA";       break;
        case 3: typeStr = "DISCONNECT"; break;
        case 4: typeStr = "PING";       break;
        default: typeStr = "UNKNOWN";   break;
    }

    uint16_t flags = header.typeAndFlags >> 4;

    LOG_PRUDP("%s %s | src=%u dst=%u | session=%u substream=%u | seq=%u | flags=0x%03X | payload=%u bytes | total=%d",
              direction, typeStr,
              header.srcPort, header.dstPort,
              header.sessionId, header.substreamId,
              header.sequenceId, flags,
              header.payloadSize, totalLen);
}

// ═══════════════════════════════════════════════════════════════════════
//  Packet Processing
// ═══════════════════════════════════════════════════════════════════════

void PrudpHandler::OnIncomingPacket(const uint8_t* data, int length) {
    std::lock_guard lock(m_mutex);

    PrudpV1Header header;
    if (!ParseHeader(data, length, header)) {
        return;
    }

    LogPacket("<<IN ", header, length);

    uint8_t type = header.typeAndFlags & 0x0F;
    const uint8_t* payload = data + sizeof(PrudpV1Header) + header.optionDataLen;
    size_t payloadLen = header.payloadSize;

    switch (static_cast<PrudpPacketType>(type)) {
        case PrudpPacketType::SYN:
            HandleSyn(header, true);
            break;
        case PrudpPacketType::CONNECT:
            HandleConnect(header, payload, payloadLen, true);
            break;
        case PrudpPacketType::DATA:
            HandleData(header, payload, payloadLen, true);
            break;
        case PrudpPacketType::DISCONNECT:
            HandleDisconnect(header, true);
            break;
        case PrudpPacketType::PING:
            HandlePing(header, true);
            break;
    }
}

void PrudpHandler::OnOutgoingPacket(const uint8_t* data, int length) {
    std::lock_guard lock(m_mutex);

    PrudpV1Header header;
    if (!ParseHeader(data, length, header)) {
        return;
    }

    LogPacket(">>OUT", header, length);

    uint8_t type = header.typeAndFlags & 0x0F;
    const uint8_t* payload = data + sizeof(PrudpV1Header) + header.optionDataLen;
    size_t payloadLen = header.payloadSize;

    switch (static_cast<PrudpPacketType>(type)) {
        case PrudpPacketType::SYN:
            HandleSyn(header, false);
            break;
        case PrudpPacketType::CONNECT:
            HandleConnect(header, payload, payloadLen, false);
            break;
        case PrudpPacketType::DATA:
            HandleData(header, payload, payloadLen, false);
            break;
        case PrudpPacketType::DISCONNECT:
            HandleDisconnect(header, false);
            break;
        case PrudpPacketType::PING:
            HandlePing(header, false);
            break;
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  State Machine Handlers
// ═══════════════════════════════════════════════════════════════════════

void PrudpHandler::HandleSyn(const PrudpV1Header& header, bool incoming) {
    uint16_t flags = header.typeAndFlags >> 4;

    if (incoming && (flags & FLAG_ACK)) {
        // SYN-ACK received — extract connection signature from response
        LOG_PRUDP("SYN-ACK received, capturing connection signature");
        memcpy(m_connSignature.data(), header.signature, 16);
        m_sessionId = header.sessionId;
        m_state = PrudpConnectionState::SynReceived;
    } else if (!incoming) {
        // Outgoing SYN — we're initiating a connection
        LOG_PRUDP("SYN sent — initiating handshake");
        m_state = PrudpConnectionState::SynSent;
    }
}

void PrudpHandler::HandleConnect(const PrudpV1Header& header, const uint8_t* payload,
                                  size_t payloadLen, bool incoming) {
    uint16_t flags = header.typeAndFlags >> 4;

    if (incoming && (flags & FLAG_ACK)) {
        // CONNECT-ACK — connection established!
        LOG_PRUDP("CONNECT-ACK received — connection ESTABLISHED");
        m_state = PrudpConnectionState::Connected;

        // The CONNECT-ACK response from the Secure Server contains the Kerberos
        // ticket validation result. If valid, the session key is now active.
        if (payloadLen > 0 && !m_sessionKey.empty()) {
            // Decrypt the payload with the session key to extract session params
            std::vector<uint8_t> decrypted(payloadLen);
            RC4Crypt(m_sessionKey.data(), m_sessionKey.size(),
                     payload, decrypted.data(), payloadLen);
            LOG_PRUDP("CONNECT payload decrypted (%zu bytes)", payloadLen);
        }
    } else if (!incoming) {
        // Outgoing CONNECT — contains credentials or Kerberos ticket
        LOG_PRUDP("CONNECT sent (payload: %zu bytes)", payloadLen);
    }
}

void PrudpHandler::HandleData(const PrudpV1Header& header, const uint8_t* payload,
                               size_t payloadLen, bool incoming) {
    // For DATA packets, the substream ID determines which RC4 key to use.
    // If the substream changed, mutate the key.
    if (header.substreamId != m_currentSubstream && !m_sessionKey.empty()) {
        m_currentRC4Key = m_sessionKey;
        for (uint8_t i = 0; i < header.substreamId; ++i) {
            MutateKey(m_currentRC4Key);
        }
        m_currentSubstream = header.substreamId;
        LOG_PRUDP("RC4 key mutated for substream %u", header.substreamId);
    }
}

void PrudpHandler::HandleDisconnect(const PrudpV1Header& header, bool incoming) {
    if (incoming) {
        LOG_PRUDP("DISCONNECT received");
    } else {
        LOG_PRUDP("DISCONNECT sent");
    }
    m_state = PrudpConnectionState::Disconnected;
}

void PrudpHandler::HandlePing(const PrudpV1Header& header, bool incoming) {
    // PING packets keep NAT translations alive
    if (incoming) {
        LOG_PRUDP("PING received (seq=%u)", header.sequenceId);
    } else {
        LOG_PRUDP("PING sent (seq=%u)", header.sequenceId);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  RC4 Sub-stream Key Mutation
// ═══════════════════════════════════════════════════════════════════════
//
//  Algorithm from the Quazal PRUDP spec:
//    def modify_key(key):
//        add = len(key) // 2 + 1
//        for i in range(len(key) // 2):
//            key[i] = (key[i] + add - i) & 0xFF
// ═══════════════════════════════════════════════════════════════════════

void PrudpHandler::MutateKey(std::vector<uint8_t>& key) {
    if (key.empty()) return;

    size_t halfLen = key.size() / 2;
    int add = static_cast<int>(halfLen) + 1;

    for (size_t i = 0; i < halfLen; ++i) {
        key[i] = static_cast<uint8_t>((key[i] + add - static_cast<int>(i)) & 0xFF);
    }
}

// ═══════════════════════════════════════════════════════════════════════
//  HMAC-MD5 Signature Computation
// ═══════════════════════════════════════════════════════════════════════
//
//  The HMAC key is the MD5 hash of the access key string.
//  The signed data is:
//    header_bytes[0x4..0xC] + session_key + sum(access_key_bytes) + conn_sig + payload
// ═══════════════════════════════════════════════════════════════════════

std::array<uint8_t, 16> PrudpHandler::ComputeSignature(
    const uint8_t* headerBytes, const uint8_t* sessionKey, size_t sessionKeyLen,
    const std::string& accessKey, const uint8_t* connSignature,
    const uint8_t* payload, size_t payloadLen)
{
    std::array<uint8_t, 16> result = {};

    // Compute the HMAC key: MD5(access_key)
    std::array<uint8_t, 16> hmacKey;
    {
        HCRYPTPROV hProv = 0;
        HCRYPTHASH hHash = 0;
        CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);
        CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash);
        CryptHashData(hHash, reinterpret_cast<const BYTE*>(accessKey.c_str()),
                      static_cast<DWORD>(accessKey.size()), 0);
        DWORD hashLen = 16;
        CryptGetHashParam(hHash, HP_HASHVAL, hmacKey.data(), &hashLen, 0);
        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
    }

    // Build the data buffer to sign:
    // [header 0x4..0xC (8 bytes)] + [session key] + [sum of access key bytes as uint8] + [conn sig (16)] + [payload]
    std::vector<uint8_t> signData;
    signData.reserve(8 + sessionKeyLen + 1 + 16 + payloadLen);

    // Header bytes (0x4 to 0xC = 8 bytes)
    if (headerBytes) {
        signData.insert(signData.end(), headerBytes, headerBytes + 8);
    }

    // Session key
    if (sessionKey && sessionKeyLen > 0) {
        signData.insert(signData.end(), sessionKey, sessionKey + sessionKeyLen);
    }

    // Sum of access key bytes
    uint8_t accessKeySum = 0;
    for (char c : accessKey) {
        accessKeySum = static_cast<uint8_t>(accessKeySum + static_cast<uint8_t>(c));
    }
    signData.push_back(accessKeySum);

    // Connection signature
    if (connSignature) {
        signData.insert(signData.end(), connSignature, connSignature + 16);
    }

    // Encrypted payload
    if (payload && payloadLen > 0) {
        signData.insert(signData.end(), payload, payload + payloadLen);
    }

    // Compute HMAC-MD5
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    HCRYPTKEY  hKey  = 0;

    CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT);

    // Import HMAC key
    struct {
        BLOBHEADER header;
        DWORD      keySize;
        BYTE       keyData[16];
    } keyBlob;

    keyBlob.header.bType    = PLAINTEXTKEYBLOB;
    keyBlob.header.bVersion = CUR_BLOB_VERSION;
    keyBlob.header.reserved = 0;
    keyBlob.header.aiKeyAlg = CALG_RC2;
    keyBlob.keySize         = 16;
    memcpy(keyBlob.keyData, hmacKey.data(), 16);

    CryptImportKey(hProv, reinterpret_cast<const BYTE*>(&keyBlob),
                   sizeof(keyBlob), 0, CRYPT_IPSEC_HMAC_KEY, &hKey);

    CryptCreateHash(hProv, CALG_HMAC, hKey, 0, &hHash);

    HMAC_INFO hmacInfo = {};
    hmacInfo.HashAlgid = CALG_MD5;
    CryptSetHashParam(hHash, HP_HMAC_INFO, reinterpret_cast<const BYTE*>(&hmacInfo), 0);

    CryptHashData(hHash, signData.data(), static_cast<DWORD>(signData.size()), 0);

    DWORD hashLen = 16;
    CryptGetHashParam(hHash, HP_HASHVAL, result.data(), &hashLen, 0);

    CryptDestroyHash(hHash);
    CryptDestroyKey(hKey);
    CryptReleaseContext(hProv, 0);

    return result;
}

// ═══════════════════════════════════════════════════════════════════════
//  RC4 Stream Cipher
// ═══════════════════════════════════════════════════════════════════════

void PrudpHandler::RC4Crypt(const uint8_t* key, size_t keyLen,
                             const uint8_t* input, uint8_t* output, size_t dataLen) {
    if (keyLen == 0 || dataLen == 0) return;

    // RC4 Key Scheduling Algorithm (KSA)
    uint8_t S[256];
    std::iota(std::begin(S), std::end(S), 0);  // S = [0, 1, 2, ..., 255]

    uint8_t j = 0;
    for (int i = 0; i < 256; ++i) {
        j = static_cast<uint8_t>(j + S[i] + key[i % keyLen]);
        std::swap(S[i], S[j]);
    }

    // RC4 Pseudo-Random Generation Algorithm (PRGA)
    uint8_t i_rc4 = 0;
    j = 0;
    for (size_t n = 0; n < dataLen; ++n) {
        i_rc4 = static_cast<uint8_t>(i_rc4 + 1);
        j     = static_cast<uint8_t>(j + S[i_rc4]);
        std::swap(S[i_rc4], S[j]);
        uint8_t k = S[static_cast<uint8_t>(S[i_rc4] + S[j])];
        output[n] = input[n] ^ k;
    }
}

void PrudpHandler::SetAccessKey(const std::string& hexKey) {
    std::lock_guard lock(m_mutex);
    m_accessKey = hexKey;
    LOG_PRUDP("Access key set: %s", hexKey.c_str());
}

void PrudpHandler::SetSessionKey(const std::vector<uint8_t>& key) {
    std::lock_guard lock(m_mutex);
    m_sessionKey   = key;
    m_currentRC4Key = key;  // First substream uses the base key
    m_currentSubstream = 0;
    LOG_PRUDP("Session key set (%zu bytes)", key.size());
}

} // namespace acu
