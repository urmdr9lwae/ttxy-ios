#include "Pfdw.h"

#include <cstring>
#include <fstream>
#include <map>
#include <mutex>

namespace host {
namespace {

uint32_t Rol1(uint32_t v) { return (v << 1) | (v >> 31); }

uint32_t StringId(const uint8_t* bytes, size_t n) {
    uint8_t buf[280];
    std::memset(buf, 0, sizeof(buf));
    if (n > 256) n = 256;
    if (n) std::memcpy(buf, bytes, n);
    uint32_t m[70];
    std::memcpy(m, buf, sizeof(m));
    int i = 0;
    while (i < 64 && m[i]) ++i;
    m[i] = 0x9BE74448u;
    m[i + 1] = 0x66F42C48u;
    i += 2;
    uint32_t v = 0xF4FA8928u, esi = 0x37A8470Eu, edi = 0x7758B42Bu;
    for (int k = 0; k < i; ++k) {
        v = Rol1(v);
        uint32_t ebx = 0x267B0B11u ^ v;
        uint32_t eax = m[k];
        esi ^= eax;
        edi ^= eax;
        uint32_t edx1 = (((ebx + edi) & ~0x42148821u) | 0x02040801u);
        uint32_t edx2 = (((ebx + esi) | 0x00804021u) & ~0x82010400u);
        uint64_t p1 = static_cast<uint64_t>(edx1) * esi;
        uint32_t lo1 = static_cast<uint32_t>(p1);
        uint32_t hi1 = static_cast<uint32_t>(p1 >> 32);
        uint64_t t = static_cast<uint64_t>(lo1) + (hi1 ? 1u : 0u) + hi1;
        uint32_t newEsi = static_cast<uint32_t>(t) + (t >> 32 ? 1u : 0u);
        uint64_t p2 = static_cast<uint64_t>(edx2) * edi;
        uint32_t lo2 = static_cast<uint32_t>(p2);
        uint32_t hi2 = static_cast<uint32_t>(p2 >> 32);
        uint64_t t2 = static_cast<uint64_t>(lo2) + (hi2 << 1);
        uint32_t newEdi = static_cast<uint32_t>(t2) + (t2 >> 32 ? 2u : 0u);
        esi = newEsi;
        edi = newEdi;
    }
    return esi ^ edi;
}

std::string Tidy(std::string n) {
    for (char& c : n) {
        if (c == '\\') c = '/';
    }
    while (n.find("//") != std::string::npos) {
        n.replace(n.find("//"), 2, "/");
    }
    while (!n.empty() && n[0] == '/') n.erase(0, 1);
    return n;
}

}  // namespace

uint32_t PfdwFileId(const std::string& path) {
    std::string n = Tidy(path);
    for (char& c : n) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    return StringId(reinterpret_cast<const uint8_t*>(n.data()), n.size());
}

namespace {

std::mutex g_mu;
std::string g_root;
std::map<std::string, std::string> g_bytes;
std::map<std::string, std::map<uint32_t, std::pair<uint32_t, uint32_t>>> g_index;

bool LoadPack(const char* packName) {
    if (g_index.find(packName) != g_index.end()) return !g_bytes[packName].empty();
    if (g_root.empty()) return false;
    std::string path = g_root;
    if (path.back() != '/') path.push_back('/');
    path += packName;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        g_bytes[packName].clear();
        g_index[packName].clear();
        return false;
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < 12 || bytes.compare(0, 4, "PFDW") != 0) return false;
    uint32_t count = 0, indexOff = 0;
    std::memcpy(&count, bytes.data() + 4, 4);
    std::memcpy(&indexOff, bytes.data() + 8, 4);
    if (indexOff > bytes.size() || count > (bytes.size() - indexOff) / 16) return false;
    auto& index = g_index[packName];
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t hash = 0, offset = 0, size = 0;
        const char* row = bytes.data() + indexOff + i * 16;
        std::memcpy(&hash, row, 4);
        std::memcpy(&offset, row + 4, 4);
        std::memcpy(&size, row + 8, 4);
        index[hash] = std::make_pair(offset, size);
    }
    g_bytes[packName].swap(bytes);
    return true;
}

bool Lookup(const std::string& path, const char*& data, uint32_t& size) {
    const char* pack = PfdwPackForPath(path);
    if (!pack || !LoadPack(pack)) return false;
    const auto id = PfdwFileId(path);
    const auto entry = g_index[pack].find(id);
    if (entry == g_index[pack].end()) return false;
    const uint32_t offset = entry->second.first;
    size = entry->second.second;
    if (static_cast<uint64_t>(offset) + size > g_bytes[pack].size()) return false;
    data = g_bytes[pack].data() + offset;
    return true;
}

}  // namespace

void PfdwSetRoot(const std::string& root) {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_root == root) return;
    g_root = root;
    g_bytes.clear();
    g_index.clear();
}

bool PfdwContains(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mu);
    const char* data = nullptr;
    uint32_t size = 0;
    return Lookup(path, data, size);
}

bool PfdwRead(const std::string& path, std::string& out) {
    std::lock_guard<std::mutex> lock(g_mu);
    const char* data = nullptr;
    uint32_t size = 0;
    if (!Lookup(path, data, size)) return false;
    out.assign(data, size);
    return true;
}

const char* PfdwPackForPath(const std::string& path) {
    std::string n = Tidy(path);
    for (char& c : n) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    }
    const auto slash = n.find('/');
    if (slash == std::string::npos) return nullptr;
    const std::string top = n.substr(0, slash);
    if (top == "script") return "script.dat";
    if (top == "db") return "db.dat";
    if (top == "images") return "images.dat";
    if (top == "data") return "data.dat";
    if (top == "ccb") return "ccb.dat";
    if (top == "sdk") return "sdk.dat";
    if (top == "particles") return "particles.dat";
    if (top == "ccbresources") return "ccbResources.dat";
    return nullptr;
}

}  // namespace host
