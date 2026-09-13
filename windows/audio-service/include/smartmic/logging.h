// Structured logging.
//
// Deliberate design constraint (threat model T12): there is no overload that
// accepts a sample buffer, a pointer, or a length that could be audio. If you
// find yourself wanting one, that is the bug.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace smartmic {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

struct LogRecord {
    LogLevel level;
    std::string_view component;
    std::string message;
    uint64_t correlationId;
};

using LogSink = std::function<void(const LogRecord&)>;

void setLogSink(LogSink sink);
void setLogLevel(LogLevel level);
void log(LogLevel level, std::string_view component, std::string message);

inline void logInfo(std::string_view c, std::string m)  { log(LogLevel::Info,  c, std::move(m)); }
inline void logWarn(std::string_view c, std::string m)  { log(LogLevel::Warn,  c, std::move(m)); }
inline void logError(std::string_view c, std::string m) { log(LogLevel::Error, c, std::move(m)); }
inline void logDebug(std::string_view c, std::string m) { log(LogLevel::Debug, c, std::move(m)); }

}  // namespace smartmic
