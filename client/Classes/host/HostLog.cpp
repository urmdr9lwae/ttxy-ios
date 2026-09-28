#include "HostLog.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace host {
namespace {
std::mutex g_mutex;
std::string g_path;
}  // namespace

void SetLogFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_path = path;
}

void Log(const char* fmt, ...) {
    char msg[4096];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char stamp[32];
    const std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S", std::localtime(&now));

    std::lock_guard<std::mutex> lock(g_mutex);
    std::fprintf(stderr, "[%s] %s\n", stamp, msg);
#ifdef _WIN32
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
#endif
    if (!g_path.empty()) {
        if (FILE* f = std::fopen(g_path.c_str(), "ab")) {
            std::fprintf(f, "[%s] %s\n", stamp, msg);
            std::fclose(f);
        }
    }
}

}  // namespace host
