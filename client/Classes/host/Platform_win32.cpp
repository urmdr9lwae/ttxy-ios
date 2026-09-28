#ifdef _WIN32
#include "Platform.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>

namespace host {

static std::string Slashes(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    if (!s.empty() && s.back() != '/') s.push_back('/');
    return s;
}

std::string PlatformResPath() {
    char buf[MAX_PATH] = {0};
    GetCurrentDirectoryA(MAX_PATH, buf);  // 启动时工作目录就是 Resources
    return Slashes(buf);
}

std::string PlatformDocPath() {
    // Resources 同级的 UserData 目录，便于调试时查看
    std::string res = PlatformResPath();
    res.pop_back();
    const size_t p = res.rfind('/');
    std::string doc = (p == std::string::npos ? res : res.substr(0, p)) + "/UserData/";
    CreateDirectoryA(doc.c_str(), nullptr);
    return doc;
}

std::string PlatformLogDir() { return PlatformDocPath(); }

std::string PlatformDeviceName() { return "Windows"; }

std::string PlatformOSVersion() { return "10"; }

void PlatformOpenURL(const std::string& url) {
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void PlatformKeepScreenOn(bool) {}

const char* PlatformFontKey() { return "win32"; }

}  // namespace host
#endif
