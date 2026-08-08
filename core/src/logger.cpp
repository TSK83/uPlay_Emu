// ═══════════════════════════════════════════════════════════════════════
//  ACU Custom Client — Logger Implementation
// ═══════════════════════════════════════════════════════════════════════

#include "logger.hpp"
#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <Windows.h>

namespace acu {

const char* LogCategoryToString(LogCategory cat) {
    switch (cat) {
        case LogCategory::GENERAL: return "GENERAL";
        case LogCategory::NET:     return "NET";
        case LogCategory::PRUDP:   return "PRUDP";
        case LogCategory::PIA:     return "PIA";
        case LogCategory::ENGINE:  return "ENGINE";
        case LogCategory::UI:      return "UI";
        case LogCategory::AUTH:    return "AUTH";
        case LogCategory::HOOK:    return "HOOK";
        default:                   return "???";
    }
}

Logger& Logger::Instance() {
    static Logger s_instance;
    return s_instance;
}

void Logger::Init(const std::string& logFilePath) {
    std::lock_guard lock(m_mutex);
    if (m_initialized) return;

    m_entries.resize(m_capacity);
    m_writeIndex = 0;
    m_wrapped    = false;

    if (m_fileOutput) {
        m_logFile = fopen(logFilePath.c_str(), "w");
        if (m_logFile) {
            fprintf(m_logFile, "=== ACU Custom Client Log ===\n");
            fflush(m_logFile);
        }
    }

    m_initialized = true;
}

void Logger::Shutdown() {
    std::lock_guard lock(m_mutex);
    if (!m_initialized) return;

    if (m_logFile) {
        fclose(m_logFile);
        m_logFile = nullptr;
    }
    m_entries.clear();
    m_initialized = false;
}

void Logger::Log(LogCategory category, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogInternal(category, 0, fmt, args);
    va_end(args);
}

void Logger::LogColored(LogCategory category, uint32_t color, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    LogInternal(category, color, fmt, args);
    va_end(args);
}

void Logger::LogInternal(LogCategory category, uint32_t color, const char* fmt, va_list args) {
    // Format the message
    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), fmt, args);

    std::string timestamp = GetTimestamp();
    const char* catStr    = LogCategoryToString(category);

    // Build full log line for file/debug output
    char fullLine[2200];
    snprintf(fullLine, sizeof(fullLine), "[%s][%s] %s", timestamp.c_str(), catStr, buffer);

    {
        std::lock_guard lock(m_mutex);

        if (!m_initialized) return;

        // Write to ring buffer
        LogEntry& entry  = m_entries[m_writeIndex];
        entry.timestamp  = timestamp;
        entry.category   = category;
        entry.message    = buffer;
        entry.color      = color;

        m_writeIndex++;
        if (m_writeIndex >= m_capacity) {
            m_writeIndex = 0;
            m_wrapped    = true;
        }

        // Write to file
        if (m_fileOutput && m_logFile) {
            fprintf(m_logFile, "%s\n", fullLine);
            fflush(m_logFile);
        }
    }

    // Write to debug output (outside lock to minimize contention)
    if (m_debugOutput) {
        // Convert to wide string for OutputDebugStringW
        char debugLine[2210];
        snprintf(debugLine, sizeof(debugLine), "%s\n", fullLine);
        OutputDebugStringA(debugLine);
    }
}

void Logger::ForEachEntry(const std::function<void(const LogEntry&)>& callback) const {
    std::lock_guard lock(m_mutex);

    if (!m_initialized) return;

    if (m_wrapped) {
        // Read from writeIndex to end, then 0 to writeIndex
        for (size_t i = m_writeIndex; i < m_capacity; ++i) {
            callback(m_entries[i]);
        }
        for (size_t i = 0; i < m_writeIndex; ++i) {
            callback(m_entries[i]);
        }
    } else {
        for (size_t i = 0; i < m_writeIndex; ++i) {
            callback(m_entries[i]);
        }
    }
}

void Logger::ForEachFiltered(uint32_t categoryMask, const std::function<void(const LogEntry&)>& callback) const {
    ForEachEntry([&](const LogEntry& entry) {
        if (categoryMask & (1u << static_cast<uint8_t>(entry.category))) {
            callback(entry);
        }
    });
}

size_t Logger::GetEntryCount() const {
    std::lock_guard lock(m_mutex);
    return m_wrapped ? m_capacity : m_writeIndex;
}

void Logger::Clear() {
    std::lock_guard lock(m_mutex);
    m_writeIndex = 0;
    m_wrapped    = false;
}

std::string Logger::GetTimestamp() const {
    auto now   = std::chrono::system_clock::now();
    auto time  = std::chrono::system_clock::to_time_t(now);
    auto ms    = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()) % 1000;

    struct tm localTime;
    localtime_s(&localTime, &time);

    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03lld",
             localTime.tm_hour, localTime.tm_min, localTime.tm_sec,
             static_cast<long long>(ms.count()));
    return buf;
}

} // namespace acu
