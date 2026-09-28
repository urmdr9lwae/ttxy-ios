#include "HostFile.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace host {
namespace {

FileReader g_reader = nullptr;
std::unordered_map<std::string, std::string> g_lowerToReal;

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

void SetFileReader(FileReader reader) { g_reader = reader; }

std::string NormalizePath(const std::string& path) {
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    while (p.compare(0, 2, "./") == 0) p.erase(0, 2);
    while (!p.empty() && p[0] == '/') p.erase(0, 1);
    return p;
}

void LoadFileList(const std::string& content) {
    g_lowerToReal.clear();
    size_t start = 0;
    while (start < content.size()) {
        size_t end = content.find('\n', start);
        if (end == std::string::npos) end = content.size();
        std::string line = content.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) g_lowerToReal.emplace(ToLower(line), line);
        start = end + 1;
    }
}

std::string ResolveCase(const std::string& relPath) {
    auto it = g_lowerToReal.find(ToLower(relPath));
    return it == g_lowerToReal.end() ? relPath : it->second;
}

bool ReadResource(const std::string& relPath, std::string& out) {
    if (!g_reader) return false;
    const std::string p = NormalizePath(relPath);
    if (g_reader(p, out)) return true;
    const std::string real = ResolveCase(p);
    return real != p && g_reader(real, out);
}

}  // namespace host
