#pragma once
// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Thread-Safe Logger
// ═══════════════════════════════════════════════════════════════════════
//  Ring buffer logger with multiple output sinks (ImGui console, file,
//  OutputDebugString). Thread-safe via mutex. Categories enable filtering
//  in the developer console.
// ═══════════════════════════════════════════════════════════════════════

#ifndef ACU_LOGGER_HPP
#define ACU_LOGGER_HPP

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include <functional>

namespace acu {

enum class LogCategory : uint8_t {
    GENERAL = 0,
    NET,
    PRUDP,
    PIA,
    ENGINE,
    UI,
    AUTH,
    HOOK,
    COUNT
};

const char* LogCategoryToString(LogCategory cat);

struct LogEntry {
    std::string    timestamp;
    LogCategory    category;
    std::string    message;
    uint32_t       color;  // RGBA packed for ImGui (0 = default)
};

class Logger {
public:
    static Logger& Instance();

    void Init(const std::string& logFilePath = "acu_client.log");
    void Shutdown();

    void Log(LogCategory category, const char* fmt, ...);
    void LogColored(LogCategory category, uint32_t color, const char* fmt, ...);

    // Access log entries for ImGui rendering (lock is held during callback)
    void ForEachEntry(const std::function<void(const LogEntry&)>& callback) const;

    // Get entries matching a category filter bitmask
    void ForEachFiltered(uint32_t categoryMask, const std::function<void(const LogEntry&)>& callback) const;

    size_t GetEntryCount() const;
    void   Clear();

    // Enable/disable output sinks
    void SetFileOutput(bool enabled)   { m_fileOutput = enabled; }
    void SetDebugOutput(bool enabled)  { m_debugOutput = enabled; }

private:
    Logger() = default;
    ~Logger() { Shutdown(); }
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void LogInternal(LogCategory category, uint32_t color, const char* fmt, va_list args);
    std::string GetTimestamp() const;

    mutable std::mutex         m_mutex;
    std::vector<LogEntry>      m_entries;
    size_t                     m_capacity    = 4096;
    size_t                     m_writeIndex  = 0;
    bool                       m_wrapped     = false;

    FILE*                      m_logFile     = nullptr;
    bool                       m_fileOutput  = true;
    bool                       m_debugOutput = true;
    bool                       m_initialized = false;
};

// ── Convenience Macros ────────────────────────────────────────────────
#define LOG(cat, fmt, ...)   acu::Logger::Instance().Log(acu::LogCategory::cat, fmt, ##__VA_ARGS__)
#define LOG_NET(fmt, ...)    LOG(NET, fmt, ##__VA_ARGS__)
#define LOG_PRUDP(fmt, ...)  LOG(PRUDP, fmt, ##__VA_ARGS__)
#define LOG_PIA(fmt, ...)    LOG(PIA, fmt, ##__VA_ARGS__)
#define LOG_ENGINE(fmt, ...) LOG(ENGINE, fmt, ##__VA_ARGS__)
#define LOG_UI(fmt, ...)     LOG(UI, fmt, ##__VA_ARGS__)
#define LOG_AUTH(fmt, ...)   LOG(AUTH, fmt, ##__VA_ARGS__)
#define LOG_HOOK(fmt, ...)   LOG(HOOK, fmt, ##__VA_ARGS__)

} // namespace acu

#endif // ACU_LOGGER_HPP
