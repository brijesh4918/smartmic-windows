#include "smartmic/logging.h"

#include <atomic>
#include <cstdio>
#include <mutex>

namespace smartmic {
namespace {

std::mutex g_mu;
LogSink g_sink;
std::atomic<int> g_level{static_cast<int>(LogLevel::Info)};
std::atomic<uint64_t> g_correlation{1};

const char* levelName(LogLevel l) {
    switch (l) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

}  // namespace

void setLogSink(LogSink sink) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_sink = std::move(sink);
}

void setLogLevel(LogLevel level) { g_level.store(static_cast<int>(level)); }

void log(LogLevel level, std::string_view component, std::string message) {
    if (static_cast<int>(level) < g_level.load(std::memory_order_relaxed)) return;
    LogRecord rec{level, component, std::move(message), g_correlation.fetch_add(1)};
    LogSink sink;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        sink = g_sink;
    }
    if (sink) {
        sink(rec);
    } else {
        std::fprintf(stderr, "[%-5s] %.*s: %s\n", levelName(rec.level),
                     static_cast<int>(rec.component.size()), rec.component.data(),
                     rec.message.c_str());
    }
}

}  // namespace smartmic
