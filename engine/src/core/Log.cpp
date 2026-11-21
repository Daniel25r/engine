#include "titan/core/Log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <mutex>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace titan {

namespace {

struct LogState {
    std::mutex mutex;
    std::vector<std::pair<int, Log::Sink>> sinks;
    std::deque<LogMessage> recent;
    int nextId = 1;
    bool platformOutput = true;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

LogState& state() {
    static LogState s;
    return s;
}

constexpr size_t kMaxRecent = 1000;

} // namespace

void Log::write(LogLevel level, const char* category, const char* fmt, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    LogState& s = state();
    LogMessage msg{level, category ? category : "", buffer,
                   std::chrono::duration<double>(std::chrono::steady_clock::now() - s.start).count()};

    std::vector<Sink> sinks;
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        s.recent.push_back(msg);
        if (s.recent.size() > kMaxRecent) s.recent.pop_front();
        for (auto& [id, sink] : s.sinks) sinks.push_back(sink);
        if (s.platformOutput) {
#if defined(__ANDROID__)
            const int prio = level == LogLevel::Error     ? ANDROID_LOG_ERROR
                             : level == LogLevel::Warning ? ANDROID_LOG_WARN
                                                          : ANDROID_LOG_INFO;
            __android_log_print(prio, "Titan", "[%s] %s", msg.category.c_str(), buffer);
#else
            const char* tag = level == LogLevel::Error ? "ERROR" : level == LogLevel::Warning ? "AVISO" : "info";
            std::FILE* out = level == LogLevel::Info ? stdout : stderr;
            std::fprintf(out, "[%8.3f] %-5s [%s] %s\n", msg.seconds, tag, msg.category.c_str(), buffer);
            std::fflush(out);
#endif
        }
    }
    // Los sinks se llaman fuera del cerrojo para que puedan registrar a su vez.
    for (auto& sink : sinks) sink(msg);
}

int Log::addSink(Sink sink) {
    LogState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.sinks.emplace_back(s.nextId, std::move(sink));
    return s.nextId++;
}

void Log::removeSink(int id) {
    LogState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    for (auto it = s.sinks.begin(); it != s.sinks.end(); ++it)
        if (it->first == id) {
            s.sinks.erase(it);
            return;
        }
}

std::vector<LogMessage> Log::recent() {
    LogState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return {s.recent.begin(), s.recent.end()};
}

void Log::setPlatformOutput(bool enabled) {
    LogState& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.platformOutput = enabled;
}

} // namespace titan
