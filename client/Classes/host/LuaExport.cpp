// 原版 LuaExport / TwLuaHelperMisc 接口的重写。
// 大部分对象在原版里是 tolua 单例，这里用普通 Lua 表实现：Lua 侧一律通过 X:GetSingleton():Method() 或 X:Method() 调用，
// 两种写法第一个参数都是表本身，C 函数统一忽略 self。
#include "LuaExport.h"

#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "quicklz.h"
}
#include "zlib.h"

#include "cocos2d.h"
#include "support/zip_support/unzip.h"
#include "FrameMap.h"
#include "HostFile.h"
#include "HostLog.h"
#include "LuaCall.h"
#include "Md5.h"
#include "Net.h"
#include "Platform.h"

USING_NS_CC;

namespace host {
namespace {

EnvInfo g_env;
const std::chrono::steady_clock::time_point g_start = std::chrono::steady_clock::now();

// ---------------------------------------------------------------- 常量

const char* kSysVars[] = {"GV_SCREENWIDTH", "GV_SCREENHEIGHT", "GV_RESPATH", "GV_DOCPATH", "GV_OPERATORPATH",
                          "GV_PATCHPATH", "GV_VERSION", "GV_LOGIN_SERVER", "GV_RES_VER", "GV_PRG_VER",
                          "GV_RESTMP_VER", "GV_PRGTMP_VER", "GV_DEVICE_NAME", "GV_PKG_IDENTIFIER", "GV_SHOW_OTHER",
                          "GV_CLOSE_MUSIC", "GV_CLOSE_SOUND", "GV_SOUNDVAL", "GV_MUSICVAL", "GV_KEEP_SCREEN_ON",
                          "GV_APP_ORDER", "GV_BUY_USERINFO", "GV_DEVICE_TOKENID", "GV_MISC", "GV_EXE_VER",
                          "GV_DEVICE_VER", "GV_IDFV", "GV_PLATFORMID", "GV_GAME_BIT"};
enum SysVar {
    GV_SCREENWIDTH, GV_SCREENHEIGHT, GV_RESPATH, GV_DOCPATH, GV_OPERATORPATH, GV_PATCHPATH, GV_VERSION,
    GV_LOGIN_SERVER, GV_RES_VER, GV_PRG_VER, GV_RESTMP_VER, GV_PRGTMP_VER, GV_DEVICE_NAME, GV_PKG_IDENTIFIER,
    GV_SHOW_OTHER, GV_CLOSE_MUSIC, GV_CLOSE_SOUND, GV_SOUNDVAL, GV_MUSICVAL, GV_KEEP_SCREEN_ON, GV_APP_ORDER,
    GV_BUY_USERINFO, GV_DEVICE_TOKENID, GV_MISC, GV_EXE_VER, GV_DEVICE_VER, GV_IDFV, GV_PLATFORMID, GV_GAME_BIT,
    GV_COUNT
};
const char* kUsrVars[] = {"UV_NEWER", "UV_GUIDE", "UV_DRAMA", "UV_MISC"};
const char* kRelayEvents[] = {"RELAY_EVENT_NONE", "RELAY_EVENT_LOGIN", "RELAY_EVENT_LOGOUT", "RELAY_EVENT_INPUT",
                              "RELAY_EVENT_AUTOPATCH", "RELAY_EVENT_APP_BUYED", "RELAY_EVENT_DEVICE_TOKEN",
                              "RELAY_EVENT_START_INDICATEVIEW", "RELAY_EVENT_STOP_INDICATEVIEW",
                              "RELAY_EVENT_SERVERLST", "RELAY_EVENT_ENTER_BACKGROUND", "RELAY_EVENT_ENTER_FOREGROUND",
                              "RELAY_EVENT_CLOSE_CALLBOARD", "RELAY_EVENT_TRANSMIT", "RELAY_EVENT_FB_SHARE_RST",
                              "RELAY_EVENT_WEIXIN_SHARE_RST", "RELAY_EVENT_QQ_SHARE_RST", "RELAY_EVENT_CLOSE_MEDIA"};
const char* kNetwork[] = {"NETWORK_NONE", "NETWORK_WIFI", "NETWORK_MOBILE", "NETWORK_OTHER"};
const char* kDeviceToken[] = {"DEVICE_TOKEN_FAIL", "DEVICE_TOKEN_REGISTER", "DEVICE_TOKEN_LAUNCH", "DEVICE_TOKEN_RECEIVE"};
const char* kReflect[] = {
    "REFLECT_EVENT_NONE", "REFLECT_EVENT_LOGIN", "REFLECT_EVENT_LOGIN_COMPLETE", "REFLECT_EVENT_LOGOUT",
    "REFLECT_EVENT_EXIT", "REFLECT_EVENT_PAY", "REFLECT_EVENT_FOUCSVIEW_CHANGED", "REFLECT_EVENT_EDIT_CHG_POS",
    "REFLECT_EVENT_OPEN_URL", "REFLECT_EVENT_ENTER_PLATFORM", "REFLECT_EVENT_UPDATE_MEMORY",
    "REFLECT_EVENT_CHECK_UPDATE", "REFLECT_EVENT_CHECK_NETWORK", "REFLECT_EVENT_WEIBO_SHARE",
    "REFLECT_EVENT_KEEP_SCREEN_ON", "REFLECT_EVENT_INIT_SDK", "REFLECT_EVENT_CALLBOARD",
    "REFLECT_EVENT_SEND_PLAYER_INFO", "REFLECT_EVENT_BIND_ACCOUNT", "REFLECT_EVENT_DEL_ACCOUNT",
    "REFLECT_EVENT_QUERY_SERVERLST", "REFLECT_EVENT_CLOSE_CALLBOARD", "REFLECT_EVENT_OPEN_URL_IN_RECT",
    "REFLECT_EVENT_CLOSE_WEBPAGE", "REFLECT_EVENT_POP_ADVERT", "REFLECT_EVENT_CHECK_SDK_FUNC",
    "REFLECT_EVENT_FB_SHARE", "REFLECT_EVENT_CHECK_STORAGESIZE", "REFLECT_EVENT_WEIXIN_SHARE",
    "REFLECT_EVENT_QQ_SHARE", "REFLECT_EVENT_GET_KEYCHAIN_ITEM", "REFLECT_EVENT_SET_KEYCHAIN_ITEM",
    "REFLECT_EVENT_OPEN_CHARTBOOST", "REFLECT_EVENT_ADD_LOCAL_NOTIFICATION", "REFLECT_EVENT_DEL_LOCAL_NOTIFICATION",
    "REFLECT_EVENT_PLAY_MEDIA", "REFLECT_EVENT_CLOSE_MEDIA"};
enum Reflect {
    RE_NONE, RE_LOGIN, RE_LOGIN_COMPLETE, RE_LOGOUT, RE_EXIT, RE_PAY, RE_FOCUS, RE_EDIT_POS, RE_OPEN_URL,
    RE_ENTER_PLATFORM, RE_UPDATE_MEMORY, RE_CHECK_UPDATE, RE_CHECK_NETWORK, RE_WEIBO, RE_KEEP_SCREEN_ON,
    RE_INIT_SDK, RE_CALLBOARD, RE_SEND_PLAYER_INFO, RE_BIND_ACCOUNT, RE_DEL_ACCOUNT, RE_QUERY_SERVERLST,
    RE_CLOSE_CALLBOARD, RE_OPEN_URL_IN_RECT, RE_CLOSE_WEBPAGE, RE_POP_ADVERT, RE_CHECK_SDK_FUNC, RE_FB_SHARE,
    RE_CHECK_STORAGESIZE
};
enum { NETWORK_NONE, NETWORK_WIFI, NETWORK_MOBILE, NETWORK_OTHER };

// 平台编号：原版枚举顺序 WIN32/MAC/ANDROID/WP8。
// 这套 Lua 和资源来自安卓包，服务器也按安卓渠道配置，所以所有平台都报 ANDROID。
enum { E_TP_WIN32, E_TP_MAC, E_TP_ANDROID, E_TP_WP8 };
const int kReportedPlatform = E_TP_ANDROID;

void SetEnum(lua_State* L, const char* const* names, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        lua_pushinteger(L, static_cast<lua_Integer>(i));
        lua_setglobal(L, names[i]);
    }
}

// ---------------------------------------------------------------- 工具

std::string ArgString(lua_State* L, int idx) {
    size_t n = 0;
    const char* s = lua_tolstring(L, idx, &n);
    return s ? std::string(s, n) : std::string();
}

bool FileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool IsDir(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && (st.st_mode & S_IFDIR);
}

std::string TrimSlash(std::string p) {
    while (p.size() > 1 && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    return p;
}

bool MakeDirs(const std::string& path) {
    std::string p = TrimSlash(path);
    if (p.empty() || IsDir(p)) return true;
    for (size_t i = 1; i <= p.size(); ++i) {
        if (i == p.size() || p[i] == '/' || p[i] == '\\') {
            const std::string sub = p.substr(0, i);
            if (sub.size() == 2 && sub[1] == ':') continue;  // 盘符
            if (!IsDir(sub)) {
#ifdef _WIN32
                _mkdir(sub.c_str());
#else
                mkdir(sub.c_str(), 0755);
#endif
            }
        }
    }
    return IsDir(p);
}

void ListDir(const std::string& dir, std::vector<std::string>& names) {
#ifdef _WIN32
    _finddata_t fd;
    intptr_t h = _findfirst((TrimSlash(dir) + "/*").c_str(), &fd);
    if (h == -1) return;
    do {
        if (strcmp(fd.name, ".") && strcmp(fd.name, "..")) names.push_back(fd.name);
    } while (_findnext(h, &fd) == 0);
    _findclose(h);
#else
    DIR* d = opendir(TrimSlash(dir).c_str());
    if (!d) return;
    while (dirent* e = readdir(d))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) names.push_back(e->d_name);
    closedir(d);
#endif
}

void RemoveTree(const std::string& path, bool keepRoot) {
    const std::string p = TrimSlash(path);
    if (IsDir(p)) {
        std::vector<std::string> names;
        ListDir(p, names);
        for (auto& n : names) RemoveTree(p + "/" + n, false);
        if (!keepRoot) {
#ifdef _WIN32
            _rmdir(p.c_str());
#else
            rmdir(p.c_str());
#endif
        }
    } else if (!keepRoot) {
        std::remove(p.c_str());
    }
}

bool ReadFileAbs(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool WriteFileAbs(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(f);
}

// 读资源；失败时再按绝对/可写目录路径读（补丁、下载文件）
bool ReadAny(const std::string& path, std::string& out) {
    if (ReadResource(path, out)) return true;
    return ReadFileAbs(path, out);
}

std::string Decrypt(const std::string& s) {
    // 原版 CTwUtil::Decrypt = 全部字节 XOR 0x5A；导出时部分配置已是明文 JSON，原样返回
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\r' || s[i] == '\n' || s[i] == '\t' || s[i] == '\xEF' ||
                            s[i] == '\xBB' || s[i] == '\xBF'))
        ++i;
    if (i < s.size() && (s[i] == '{' || s[i] == '[' || s[i] == '<')) return s;
    std::string r(s);
    for (auto& c : r) c = static_cast<char>(c ^ 0x5A);
    return r;
}

std::string Encrypt(const std::string& s) {
    std::string r(s);
    for (auto& c : r) c = static_cast<char>(c ^ 0x5A);
    return r;
}

// ---------------------------------------------------------------- 变量系统

// 存档格式：每行 "名字\t值"，值里的 \ \n \r \t 转义
std::string Escape(const std::string& v) {
    std::string r;
    for (char c : v) {
        switch (c) {
        case '\\': r += "\\\\"; break;
        case '\n': r += "\\n"; break;
        case '\r': r += "\\r"; break;
        case '\t': r += "\\t"; break;
        default: r += c;
        }
    }
    return r;
}

std::string Unescape(const std::string& v) {
    std::string r;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            const char n = v[++i];
            r += n == 'n' ? '\n' : n == 'r' ? '\r' : n == 't' ? '\t' : n;
        } else {
            r += v[i];
        }
    }
    return r;
}

void LoadVarFile(const std::string& path, std::map<std::string, std::string>& vars) {
    std::string data;
    if (!ReadFileAbs(path, data)) return;
    size_t start = 0;
    while (start < data.size()) {
        size_t end = data.find('\n', start);
        if (end == std::string::npos) end = data.size();
        const std::string line = data.substr(start, end - start);
        const size_t tab = line.find('\t');
        if (tab != std::string::npos) vars[line.substr(0, tab)] = Unescape(line.substr(tab + 1));
        start = end + 1;
    }
}

void SaveVarFile(const std::string& path, const std::map<std::string, std::string>& vars) {
    std::string data;
    for (auto& kv : vars) data += kv.first + "\t" + Escape(kv.second) + "\n";
    if (!WriteFileAbs(path, data)) Log("保存变量失败：%s", path.c_str());
}

std::map<std::string, std::string> g_sysVars;
std::map<std::string, std::string> g_usrVars;
std::string g_usrFile;

// 每次启动由环境重新给出、不存档的系统变量
bool IsRuntimeVar(int idx) {
    switch (idx) {
    case GV_SCREENWIDTH: case GV_SCREENHEIGHT: case GV_RESPATH: case GV_DOCPATH: case GV_OPERATORPATH:
    case GV_PATCHPATH: case GV_VERSION: case GV_DEVICE_NAME: case GV_PKG_IDENTIFIER: case GV_EXE_VER:
    case GV_DEVICE_VER: case GV_PRG_VER: case GV_GAME_BIT:
        return true;
    default:
        return false;
    }
}

std::string SysVarFile() { return g_env.docPath + "sysvar.dat"; }

const char* SysVarName(lua_State* L, int idx, bool usr) {
    const lua_Integer k = luaL_checkinteger(L, idx);
    if (usr) {
        if (k < 0 || k >= static_cast<lua_Integer>(sizeof(kUsrVars) / sizeof(kUsrVars[0]))) return nullptr;
        return kUsrVars[k];
    }
    if (k < 0 || k >= GV_COUNT) return nullptr;
    return kSysVars[k];
}

// 值统一转成字符串；整数值的 number 不带小数
std::string ValueString(lua_State* L, int idx) {
    if (lua_type(L, idx) == LUA_TNUMBER) {
        const double d = lua_tonumber(L, idx);
        char buf[64];
        if (d == static_cast<double>(static_cast<long long>(d)))
            snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
        else
            snprintf(buf, sizeof(buf), "%.14g", d);
        return buf;
    }
    if (lua_type(L, idx) == LUA_TBOOLEAN) return lua_toboolean(L, idx) ? "1" : "0";
    return ArgString(L, idx);
}

int PushVar(lua_State* L, const std::map<std::string, std::string>& vars, const char* name, bool withFlag) {
    auto it = name ? vars.find(name) : vars.end();
    const bool found = it != vars.end();
    const std::string v = found ? it->second : std::string();
    if (withFlag) {
        lua_pushboolean(L, found);
        lua_pushlstring(L, v.data(), v.size());
        return 2;
    }
    lua_pushlstring(L, v.data(), v.size());
    return 1;
}

int l_GetSysVariable(lua_State* L) {
    return PushVar(L, g_sysVars, SysVarName(L, 2, false), lua_gettop(L) >= 3);
}
int l_SetSysVariable(lua_State* L) {
    if (const char* n = SysVarName(L, 2, false)) g_sysVars[n] = ValueString(L, 3);
    return 0;
}
int l_GetUsrVariable(lua_State* L) {
    return PushVar(L, g_usrVars, SysVarName(L, 2, true), lua_gettop(L) >= 3);
}
int l_SetUsrVariable(lua_State* L) {
    if (const char* n = SysVarName(L, 2, true)) g_usrVars[n] = ValueString(L, 3);
    return 0;
}

void SaveSys() {
    std::map<std::string, std::string> keep;
    for (int i = 0; i < GV_COUNT; ++i) {
        if (IsRuntimeVar(i)) continue;
        auto it = g_sysVars.find(kSysVars[i]);
        if (it != g_sysVars.end()) keep.insert(*it);
    }
    for (auto& kv : g_sysVars)
        if (kv.first.compare(0, 2, "__") == 0) keep.insert(kv);  // 宿主私有项（设备 ID 等）
    SaveVarFile(SysVarFile(), keep);
}

int l_SaveSysVariable(lua_State*) {
    SaveSys();
    return 0;
}
int l_LoadSysVariable(lua_State*) {
    LoadVarFile(SysVarFile(), g_sysVars);
    return 0;
}
int l_SaveUsrVariable(lua_State* L) {
    const std::string f = lua_gettop(L) >= 2 && lua_isstring(L, 2) ? ArgString(L, 2) : g_usrFile;
    if (!f.empty()) SaveVarFile(g_env.docPath + f, g_usrVars);
    return 0;
}
int l_LoadUsrVariable(lua_State* L) {
    g_usrFile = ArgString(L, 2);
    g_usrVars.clear();
    if (!g_usrFile.empty()) LoadVarFile(g_env.docPath + g_usrFile, g_usrVars);
    return 0;
}
int l_ResetVars(lua_State*) {
    g_usrVars.clear();
    g_usrFile.clear();
    return 0;
}

void InitSysVars() {
    LoadVarFile(SysVarFile(), g_sysVars);
    g_sysVars["GV_SCREENWIDTH"] = std::to_string(g_env.screenWidth);
    g_sysVars["GV_SCREENHEIGHT"] = std::to_string(g_env.screenHeight);
    g_sysVars["GV_RESPATH"] = g_env.resPath;
    g_sysVars["GV_DOCPATH"] = g_env.docPath;
    g_sysVars["GV_PATCHPATH"] = g_env.patchPath;
    g_sysVars["GV_OPERATORPATH"] = g_env.operatorPath;
    g_sysVars["GV_VERSION"] = g_env.version;
    g_sysVars["GV_PRG_VER"] = g_env.version;
    g_sysVars["GV_DEVICE_NAME"] = g_env.deviceName;
    g_sysVars["GV_PKG_IDENTIFIER"] = g_env.packageName;
    g_sysVars["GV_EXE_VER"] = "3";  // >= INIT_SDK：不走 SDK 初始化
    g_sysVars["GV_DEVICE_VER"] = PlatformOSVersion();
    g_sysVars["GV_GAME_BIT"] = sizeof(void*) == 8 ? "64" : "32";
    if (g_sysVars["__UNIQUE_ID"].empty()) {
        std::random_device rd;
        std::mt19937_64 rng((static_cast<uint64_t>(rd()) << 32) ^ rd() ^
                            static_cast<uint64_t>(std::time(nullptr)));
        char buf[40];
        snprintf(buf, sizeof(buf), "%016llx%016llx", static_cast<unsigned long long>(rng()),
                 static_cast<unsigned long long>(rng()));
        g_sysVars["__UNIQUE_ID"] = buf;
        SaveSys();
    }
}

// ---------------------------------------------------------------- 字符串表

std::unordered_map<uint32_t, std::string> g_strings;

void LoadStrRes() {
    std::string data;
    if (ReadResource("ini/strres.dat", data))
        data = Decrypt(data);
    else if (!ReadResource("ini/strres.ini", data)) {
        Log("找不到 ini/strres.ini");
        return;
    }
    if (data.compare(0, 3, "\xEF\xBB\xBF") == 0) data.erase(0, 3);
    size_t start = 0;
    while (start < data.size()) {
        size_t end = data.find('\n', start);
        if (end == std::string::npos) end = data.size();
        std::string line = data.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == ';') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const uint32_t id = static_cast<uint32_t>(strtoul(line.c_str(), nullptr, 10));
        if (id) g_strings[id] = line.substr(eq + 1);
    }
    Log("字符串表 %u 条", static_cast<unsigned>(g_strings.size()));
}

int l_TwGetStr(lua_State* L) {
    const uint32_t id = static_cast<uint32_t>(luaL_checknumber(L, 1));
    auto it = g_strings.find(id);
    if (it != g_strings.end()) {
        lua_pushlstring(L, it->second.data(), it->second.size());
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "SE:%u", id);
        lua_pushstring(L, buf);
    }
    return 1;
}

// ---------------------------------------------------------------- 全局函数

int l_WriteLog(lua_State* L) {
    Log("%s", lua_isstring(L, 1) ? lua_tostring(L, 1) : luaL_typename(L, 1));
    return 0;
}

uint32_t NowMs() {
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - g_start).count());
}

int l_TimeGetTime(lua_State* L) {
    lua_pushnumber(L, NowMs());
    return 1;
}

// 把 zip 解到 dst 目录（热更新补丁包）；返回 0 表示成功，和原版 TwUnzip 一致
int UnzipTo(const std::string& zip, std::string dst, int& count) {
    count = 0;
    if (!dst.empty() && dst.back() != '/' && dst.back() != '\\') dst += '/';
    MakeDirs(dst);
    unzFile uf = cocos2d::unzOpen(zip.c_str());
    if (!uf) return 1;
    int rc = 0;
    std::vector<char> buf(64 * 1024);
    for (int r = cocos2d::unzGoToFirstFile(uf); r == UNZ_OK; r = cocos2d::unzGoToNextFile(uf)) {
        char name[1024] = {0};
        cocos2d::unz_file_info info;
        if (cocos2d::unzGetCurrentFileInfo(uf, &info, name, sizeof(name) - 1, nullptr, 0, nullptr, 0) != UNZ_OK) {
            rc = 2;
            break;
        }
        std::string rel = name;
        std::replace(rel.begin(), rel.end(), '\\', '/');
        // 不接受绝对路径和 ".."，防止补丁包把文件写到可写目录以外
        if (rel.empty() || rel[0] == '/' || rel.find("..") != std::string::npos || rel.find(':') != std::string::npos) {
            Log("TwUnzip: 跳过可疑路径 %s", rel.c_str());
            continue;
        }
        const std::string out = dst + rel;
        if (rel.back() == '/') {
            MakeDirs(out);
            continue;
        }
        const size_t slash = out.find_last_of('/');
        if (slash != std::string::npos) MakeDirs(out.substr(0, slash));
        if (cocos2d::unzOpenCurrentFile(uf) != UNZ_OK) {
            rc = 3;
            break;
        }
        FILE* f = std::fopen(out.c_str(), "wb");
        if (!f) {
            cocos2d::unzCloseCurrentFile(uf);
            rc = 4;
            break;
        }
        int n = 0;
        while ((n = cocos2d::unzReadCurrentFile(uf, buf.data(), static_cast<unsigned>(buf.size()))) > 0)
            std::fwrite(buf.data(), 1, static_cast<size_t>(n), f);
        std::fclose(f);
        if (cocos2d::unzCloseCurrentFile(uf) != UNZ_OK || n < 0) {  // 关闭时校验 CRC
            rc = 5;
            break;
        }
        ++count;
    }
    cocos2d::unzClose(uf);
    return rc;
}

int l_TwUnzip(lua_State* L) {
    int count = 0;
    const int rc = UnzipTo(ArgString(L, 1), ArgString(L, 2), count);
    Log("TwUnzip(%s -> %s) = %d，%d 个文件", ArgString(L, 1).c_str(), ArgString(L, 2).c_str(), rc, count);
    lua_pushinteger(L, rc);
    return 1;
}

// CEnvRoot:SetReloadAll()：热更新补丁装好后调用。原版会重启整个 Lua 虚拟机再从头走一遍自动更新。
// 这里不重启虚拟机：清掉 CCFileUtils 的路径缓存（补丁目录在搜索路径最前面，之后加载的脚本/资源用补丁版本），
// 下一帧让自动更新接着走“无更新”时的收尾流程（AutoPatch:done，进入选服）。已经加载过的模块要下次启动才换成新版。
bool g_reloadPending = false;

int l_SetReloadAll(lua_State*) {
    CCFileUtils::sharedFileUtils()->purgeCachedEntries();
    g_reloadPending = true;
    Log("热更新补丁已安装，继续进入选服");
    return 0;
}

int l_False(lua_State* L) {
    lua_pushboolean(L, 0);
    return 1;
}

int l_Noop(lua_State*) { return 0; }

int l_Self(lua_State* L) {
    lua_pushvalue(L, lua_upvalueindex(1));
    return 1;
}

// ---------------------------------------------------------------- 压缩

std::string ZlibInflate(const std::string& in) {
    std::string out;
    if (in.empty()) return out;
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit(&zs) != Z_OK) return out;
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    zs.avail_in = static_cast<uInt>(in.size());
    char buf[16384];
    int rc;
    do {
        zs.next_out = reinterpret_cast<Bytef*>(buf);
        zs.avail_out = sizeof(buf);
        rc = inflate(&zs, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) break;
        out.append(buf, sizeof(buf) - zs.avail_out);
    } while (rc != Z_STREAM_END);
    inflateEnd(&zs);
    return out;
}

std::string ZlibDeflate(const std::string& in) {
    std::string out;
    if (in.empty()) return out;
    uLongf n = compressBound(static_cast<uLong>(in.size()));
    out.resize(n);
    if (compress(reinterpret_cast<Bytef*>(&out[0]), &n, reinterpret_cast<const Bytef*>(in.data()),
                 static_cast<uLong>(in.size())) != Z_OK)
        return std::string();
    out.resize(n);
    return out;
}

std::string QlzInflate(const std::string& in) {
    std::string out;
    std::vector<char> state(sizeof(qlz_state_decompress), 0);
    size_t pos = 0;
    while (pos + 9 <= in.size()) {
        const char* src = in.data() + pos;
        const size_t csize = qlz_size_compressed(src);
        const size_t dsize = qlz_size_decompressed(src);
        if (csize == 0 || pos + csize > in.size()) break;
        const size_t old = out.size();
        out.resize(old + dsize);
        qlz_decompress(src, &out[old], reinterpret_cast<qlz_state_decompress*>(&state[0]));
        pos += csize;
    }
    return out;
}

std::string QlzDeflate(const std::string& in) {
    std::string out;
    std::vector<char> state(sizeof(qlz_state_compress), 0);
    std::vector<char> buf(0x40000 + 400);
    for (size_t pos = 0; pos < in.size(); pos += 0x40000) {
        const size_t n = std::min<size_t>(0x40000, in.size() - pos);
        const size_t c = qlz_compress(in.data() + pos, &buf[0], n, reinterpret_cast<qlz_state_compress*>(&state[0]));
        out.append(&buf[0], c);
    }
    return out;
}

// 这几个函数在原版里既作为全局函数（一个参数），也被挂到 CTwUtil.Encrypt（冒号调用，两个参数）上：取最后一个参数
std::string LastArg(lua_State* L) { return ArgString(L, lua_gettop(L)); }

#define STR_FUNC(name, expr)                          \
    int name(lua_State* L) {                          \
        const std::string in = LastArg(L);            \
        const std::string out = expr;                 \
        lua_pushlstring(L, out.data(), out.size());   \
        return 1;                                     \
    }
STR_FUNC(l_ZlibInflate, ZlibInflate(in))
STR_FUNC(l_ZlibDeflate, ZlibDeflate(in))
STR_FUNC(l_QlzInflate, QlzInflate(in))
STR_FUNC(l_QlzDeflate, QlzDeflate(in))
STR_FUNC(l_Decrypt, Decrypt(in))
STR_FUNC(l_Encrypt, Encrypt(in))
STR_FUNC(l_Identity, in)

// ---------------------------------------------------------------- CTwUtil

std::string g_fontName = "fonts/YaHei.ttf";
int g_fontSize = 20;

void LoadFontIni() {
    std::string data;
    if (!ReadResource("ini/font.ini", data)) return;
    const std::string key = std::string(PlatformFontKey()) + "=";
    std::istringstream ss(data);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.compare(0, key.size(), key) != 0) continue;
        const std::string v = line.substr(key.size());
        const size_t bar = v.find('|');
        g_fontName = v.substr(0, bar);
        if (bar != std::string::npos) g_fontSize = atoi(v.c_str() + bar + 1);
    }
}

int l_GetPlatform(lua_State* L) {
    lua_pushinteger(L, kReportedPlatform);
    return 1;
}

int l_GetDefaultFontName(lua_State* L) {
    lua_pushstring(L, g_fontName.c_str());
    return 1;
}

int l_GetFontSize(lua_State* L) {
    lua_pushinteger(L, g_fontSize);
    return 1;
}

int l_GetCurrentMem(lua_State* L) {
    lua_createtable(L, 0, 5);
    lua_pushnumber(L, 96.0 * 1024 * 1024); lua_setfield(L, -2, "uMemUsed");
    lua_pushnumber(L, 256.0 * 1024 * 1024); lua_setfield(L, -2, "uMemVirtual");
    lua_pushnumber(L, 1024.0 * 1024 * 1024); lua_setfield(L, -2, "uMemAvailble");
    lua_pushinteger(L, 0); lua_setfield(L, -2, "nWarningLev");
    lua_pushnumber(L, 0); lua_setfield(L, -2, "fCpuUsage");
    return 1;
}

// 盐值文件：XML，根节点属性名就是 key 类型（例如 ACCOUNT="..."），内容经 XOR 加密
std::string ServerSalt(const std::string& key, const std::string& file) {
    std::string data;
    if (file.empty() || !ReadAny(file, data) || data.empty()) return std::string();
    data = Decrypt(data);
    const std::string pat = key + "=\"";
    const size_t p = data.find(pat);
    if (p == std::string::npos) return std::string();
    const size_t b = p + pat.size();
    const size_t e = data.find('"', b);
    return e == std::string::npos ? std::string() : data.substr(b, e - b);
}

// MakeSaltKey(src, keyType, saltFile) -> {strKey = md5(...), dwTime = 毫秒}
int l_MakeSaltKey(lua_State* L) {
    const int base = lua_istable(L, 1) ? 2 : 1;
    const std::string src = ArgString(L, base);
    const std::string type = ArgString(L, base + 1);
    const std::string file = ArgString(L, base + 2);
    const uint32_t t = NowMs();
    const std::string ts = std::to_string(t);
    const bool account = type == "ACCOUNT";
    std::string salt = ServerSalt(type, file);
    if (salt.empty()) salt = account ? "16#eyugame" : "long2_37wan_gs_KEY_eWRRE44JJelorerJEIrAD89KJCkjksj";
    const std::string key = account ? src + salt + ts : src + ts + salt;
    lua_createtable(L, 0, 2);
    const std::string md5 = Md5Hex(key);
    lua_pushstring(L, md5.c_str());
    lua_setfield(L, -2, "strKey");
    lua_pushnumber(L, t);
    lua_setfield(L, -2, "dwTime");
    return 1;
}

// ---------------------------------------------------------------- CMd5 / 事件参数 / 点矩形

int l_Md5GetResult(lua_State* L) {
    lua_getfield(L, 1, "__result");
    return 1;
}

int l_CMd5Call(lua_State* L) {
    // __call 的第一个参数是类表。三种用法（见 Login / AutoPatch 脚本）：
    //   CMd5(str)          字符串的 MD5
    //   CMd5(path, true)   资源包里文件内容的 MD5（db/describe.dat）
    //   CMd5(path, false)  磁盘文件内容的 MD5（热更新下载的补丁包，绝对路径）
    std::string s = ArgString(L, 2);
    if (lua_type(L, 3) == LUA_TBOOLEAN) {
        std::string data;
        const bool ok = lua_toboolean(L, 3) ? ReadResource(s, data) : ReadAny(s, data);
        if (!ok) Log("CMd5: 读不到文件 %s", s.c_str());
        s.swap(data);
    }
    lua_createtable(L, 0, 2);
    const std::string r = Md5Hex(s);
    lua_pushstring(L, r.c_str());
    lua_setfield(L, -2, "__result");
    lua_pushcfunction(L, l_Md5GetResult);
    lua_setfield(L, -2, "GetResult");
    return 1;
}

int l_GetEvtType(lua_State* L) {
    lua_getfield(L, 1, "EvtType");
    return 1;
}

// TwEvtArgs(type) / TwReflectEvtArgs(type, n1, n2, str)
int l_EvtArgsCall(lua_State* L) {
    const int n = lua_gettop(L);
    lua_createtable(L, n, 6);
    if (n >= 2) lua_pushvalue(L, 2); else lua_pushinteger(L, 0);
    lua_setfield(L, -2, "EvtType");
    if (n >= 3) { lua_pushvalue(L, 3); lua_setfield(L, -2, "nParam1"); }
    if (n >= 4) { lua_pushvalue(L, 4); lua_setfield(L, -2, "nParam2"); }
    if (n >= 5) { lua_pushvalue(L, 5); lua_setfield(L, -2, "strParam"); }
    for (int i = 2; i <= n; ++i) {
        lua_pushvalue(L, i);
        lua_rawseti(L, -2, i - 1);
    }
    lua_pushcfunction(L, l_GetEvtType);
    lua_setfield(L, -2, "GetEvtType");
    return 1;
}

// TwPoint(x, y)
int l_PointCall(lua_State* L) {
    lua_createtable(L, 0, 2);
    lua_pushnumber(L, luaL_optnumber(L, 2, 0)); lua_setfield(L, -2, "x");
    lua_pushnumber(L, luaL_optnumber(L, 3, 0)); lua_setfield(L, -2, "y");
    luaL_getmetatable(L, "host.TwPoint");
    lua_setmetatable(L, -2);
    return 1;
}

int l_PointDist(lua_State* L) {
    lua_getfield(L, 1, "x"); lua_getfield(L, 1, "y");
    lua_getfield(L, 2, "x"); lua_getfield(L, 2, "y");
    const double dx = lua_tonumber(L, -4) - lua_tonumber(L, -2);
    const double dy = lua_tonumber(L, -3) - lua_tonumber(L, -1);
    lua_pushnumber(L, std::sqrt(dx * dx + dy * dy));
    return 1;
}

int l_PointEq(lua_State* L) {
    lua_getfield(L, 1, "x"); lua_getfield(L, 1, "y");
    lua_getfield(L, 2, "x"); lua_getfield(L, 2, "y");
    lua_pushboolean(L, lua_tonumber(L, -4) == lua_tonumber(L, -2) && lua_tonumber(L, -3) == lua_tonumber(L, -1));
    return 1;
}

// TwRect(l, t, r, b)
int l_RectCall(lua_State* L) {
    lua_createtable(L, 0, 4);
    const char* names[] = {"l", "t", "r", "b"};
    for (int i = 0; i < 4; ++i) {
        lua_pushnumber(L, luaL_optnumber(L, 2 + i, 0));
        lua_setfield(L, -2, names[i]);
    }
    luaL_getmetatable(L, "host.TwRect");
    lua_setmetatable(L, -2);
    return 1;
}

double Field(lua_State* L, int idx, const char* k) {
    lua_getfield(L, idx, k);
    const double v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return v;
}

int l_RectWidth(lua_State* L) { lua_pushnumber(L, Field(L, 1, "r") - Field(L, 1, "l")); return 1; }
int l_RectHeight(lua_State* L) { lua_pushnumber(L, Field(L, 1, "b") - Field(L, 1, "t")); return 1; }
int l_RectIsIn(lua_State* L) {
    const double x = Field(L, 2, "x"), y = Field(L, 2, "y");
    lua_pushboolean(L, x >= Field(L, 1, "l") && x < Field(L, 1, "r") && y >= Field(L, 1, "t") && y < Field(L, 1, "b"));
    return 1;
}

// ---------------------------------------------------------------- CReflectSystem

int l_FireEvent(lua_State* L) {
    if (!lua_istable(L, 2)) return 0;
    const int type = static_cast<int>(Field(L, 2, "EvtType"));
    lua_getfield(L, 2, "strParam");
    const std::string str = ArgString(L, -1);
    lua_pop(L, 1);
    // 登录事件里带密码摘要 / 票据，日志里只记长度
    Log("ReflectEvent %s %s", type >= 0 && type < static_cast<int>(sizeof(kReflect) / sizeof(kReflect[0])) ? kReflect[type] : "?",
        type == RE_LOGIN ? ("<" + std::to_string(str.size()) + " bytes>").c_str() : str.c_str());
    switch (type) {
    case RE_LOGIN:
        // 安卓版由 Java 的 CryptoPlatformV1 处理（账号加密信封 / TCP 登录证明），这里由 AccountCrypto.cpp 的 Lua 实现
        CallGlobal("HostAccountCrypto", str);
        break;
    case RE_CHECK_NETWORK:
        // Lua 触发后立即同步读取网络类型
        CallGlobal("OnNetworkStatusChanged", NETWORK_WIFI);
        break;
    case RE_OPEN_URL:
    case RE_OPEN_URL_IN_RECT:
        if (!str.empty()) PlatformOpenURL(str);
        break;
    case RE_KEEP_SCREEN_ON:
        PlatformKeepScreenOn(Field(L, 2, "nParam1") != 0);
        break;
    case RE_EXIT:
        Log("Lua 请求退出游戏");
        CCDirector::sharedDirector()->end();
        break;
    case RE_CHECK_STORAGESIZE:
        CallGlobal("OnAvaliableStorageSize", 1024);  // MB
        break;
    default:
        break;
    }
    return 0;
}

// ---------------------------------------------------------------- CEnvRoot

int l_GetUniqueId(lua_State* L) {
    lua_pushstring(L, g_sysVars["__UNIQUE_ID"].c_str());
    return 1;
}

int l_GetVersionName(lua_State* L) {
    lua_pushstring(L, g_env.versionName.c_str());
    return 1;
}

int l_EmptyString(lua_State* L) {
    lua_pushliteral(L, "");
    return 1;
}

// ---------------------------------------------------------------- CTwDirUtils

std::string PathArg(lua_State* L) {
    // 冒号调用：参数从 2 开始；点号调用：从 1 开始
    return ArgString(L, lua_istable(L, 1) ? 2 : 1);
}
std::string PathArg2(lua_State* L) { return ArgString(L, lua_istable(L, 1) ? 3 : 2); }

int l_MkDir(lua_State* L) { lua_pushboolean(L, MakeDirs(PathArg(L))); return 1; }
int l_RmDir(lua_State* L) { RemoveTree(PathArg(L), false); lua_pushboolean(L, 1); return 1; }
int l_FileStat(lua_State* L) {
    const std::string p = PathArg(L);
    lua_pushboolean(L, FileExists(p) && !IsDir(p));
    return 1;
}
int l_DirStat(lua_State* L) { lua_pushboolean(L, IsDir(PathArg(L))); return 1; }
int l_DelFile(lua_State* L) { lua_pushboolean(L, std::remove(PathArg(L).c_str()) == 0); return 1; }
int l_ClrContent(lua_State* L) { RemoveTree(PathArg(L), true); lua_pushboolean(L, 1); return 1; }
// 把目录 a 的内容合并进 b（同名文件覆盖），完成后删掉 a。
// 热更新把新补丁解到 unzip_tmp/ 再“改名”到 patch/，合并才能保留以前的补丁
bool MergeMove(const std::string& a, const std::string& b) {
    if (!IsDir(a)) {
        std::remove(b.c_str());
        return std::rename(a.c_str(), b.c_str()) == 0;
    }
    if (!IsDir(b)) {
        std::remove(b.c_str());
        if (std::rename(a.c_str(), b.c_str()) == 0) return true;
        MakeDirs(b);
    }
    std::vector<std::string> names;
    ListDir(a, names);
    bool ok = true;
    for (auto& n : names) ok = MergeMove(a + "/" + n, b + "/" + n) && ok;
#ifdef _WIN32
    _rmdir(a.c_str());
#else
    rmdir(a.c_str());
#endif
    return ok;
}

int l_Rename(lua_State* L) {
    const std::string a = TrimSlash(PathArg(L)), b = TrimSlash(PathArg2(L));
    const bool ok = MergeMove(a, b);
    if (!ok) Log("Rename %s -> %s 失败", a.c_str(), b.c_str());
    lua_pushboolean(L, ok);
    return 1;
}

// ---------------------------------------------------------------- CNetMgr

int l_NetConnect(lua_State* L) {
    NetConnect(ArgString(L, 2), static_cast<int>(luaL_checknumber(L, 3)));
    return 0;
}
int l_NetDisconnect(lua_State*) {
    NetDisconnect();
    return 0;
}
int l_NetSendMsg(lua_State* L) {
    lua_pushboolean(L, NetSend(LastArg(L)));
    return 1;
}

// ---------------------------------------------------------------- ITwHttp

std::string TableString(lua_State* L, int idx, const char* k, const char* def = "") {
    lua_getfield(L, idx, k);
    std::string v = lua_isstring(L, -1) ? ArgString(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

void ReadPairs(lua_State* L, int idx, const char* k, std::vector<std::pair<std::string, std::string>>& out) {
    lua_getfield(L, idx, k);
    if (lua_istable(L, -1)) {
        lua_pushnil(L);
        while (lua_next(L, -2)) {
            if (lua_type(L, -2) == LUA_TSTRING) out.emplace_back(ArgString(L, -2), ValueString(L, -1));
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 1);
}

// req.pBufferReader：热更新下载用。原版支持断点续传（GetInitFileSize 返回已下载的字节数，脚本据此加 Range 头），
// 这里每次都从头下载（返回 0，下载时覆盖写文件），GetFileName 返回下载文件路径
int l_BufGetInitFileSize(lua_State* L) {
    lua_pushnumber(L, 0);
    return 1;
}
int l_BufGetFileName(lua_State* L) {
    lua_getfield(L, 1, "__file");
    return 1;
}

int l_ReqSetDownloadFile(lua_State* L) {
    lua_pushvalue(L, 2);
    lua_setfield(L, 1, "__downloadFile");
    lua_pushboolean(L, 1);
    lua_setfield(L, 1, "bDownloadFile");
    lua_createtable(L, 0, 3);
    lua_pushvalue(L, 2);
    lua_setfield(L, -2, "__file");
    lua_pushcfunction(L, l_BufGetInitFileSize);
    lua_setfield(L, -2, "GetInitFileSize");
    lua_pushcfunction(L, l_BufGetFileName);
    lua_setfield(L, -2, "GetFileName");
    lua_setfield(L, 1, "pBufferReader");
    return 0;
}

int l_ReqAddPair(lua_State* L, const char* field) {
    lua_getfield(L, 1, field);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, 1, field);
    }
    lua_pushvalue(L, 3);
    lua_setfield(L, -2, lua_tostring(L, 2));
    return 0;
}
int l_ReqAddHeader(lua_State* L) { return l_ReqAddPair(L, "Header"); }
int l_ReqAddCookie(lua_State* L) { return l_ReqAddPair(L, "Cookie"); }
int l_ReqAddBuffer(lua_State* L) {
    lua_getfield(L, 1, "__buffer");
    const std::string old = ArgString(L, -1);
    lua_pop(L, 1);
    const std::string add = old + ArgString(L, 2);
    lua_pushlstring(L, add.data(), add.size());
    lua_setfield(L, 1, "__buffer");
    return 0;
}
int l_ReqIsSucc(lua_State* L) {
    lua_pushboolean(L, Field(L, 1, "nErrorCode") == 0);
    return 1;
}

int l_NewRequest(lua_State* L) {
    lua_createtable(L, 0, 16);
    lua_pushnumber(L, HttpNextId()); lua_setfield(L, -2, "uReqId");
    lua_pushliteral(L, "GET"); lua_setfield(L, -2, "strMethod");
    lua_pushboolean(L, 1); lua_setfield(L, -2, "bHttpNewVer");
    lua_pushliteral(L, ""); lua_setfield(L, -2, "strHost");
    lua_pushinteger(L, 80); lua_setfield(L, -2, "usPort");
    lua_pushliteral(L, "/"); lua_setfield(L, -2, "strAction");
    lua_pushliteral(L, ""); lua_setfield(L, -2, "strParam");
    lua_pushinteger(L, 0); lua_setfield(L, -2, "ucRetry");
    lua_pushinteger(L, 0); lua_setfield(L, -2, "nErrorCode");
    lua_pushinteger(L, 0); lua_setfield(L, -2, "nSocketErrno");
    lua_pushinteger(L, 15000); lua_setfield(L, -2, "nTimeReq");
    lua_pushboolean(L, 0); lua_setfield(L, -2, "bDownloadFile");
    lua_newtable(L); lua_setfield(L, -2, "Header");
    lua_newtable(L); lua_setfield(L, -2, "Cookie");
    lua_pushcfunction(L, l_ReqSetDownloadFile); lua_setfield(L, -2, "SetDownloadFile");
    lua_pushcfunction(L, l_ReqAddHeader); lua_setfield(L, -2, "AddHeader");
    lua_pushcfunction(L, l_ReqAddCookie); lua_setfield(L, -2, "AddCookie");
    lua_pushcfunction(L, l_ReqAddBuffer); lua_setfield(L, -2, "AddBuffer");
    lua_pushcfunction(L, l_ReqIsSucc); lua_setfield(L, -2, "IsSucc");
    lua_pushcfunction(L, l_Noop); lua_setfield(L, -2, "Reset");
    return 1;
}

// ITwHttp:GetInstance():SendRequest(req)
int l_SendRequest(lua_State* L) {
    const int idx = lua_istable(L, 2) ? 2 : 1;
    luaL_checktype(L, idx, LUA_TTABLE);
    HttpRequest r;
    r.id = static_cast<uint32_t>(Field(L, idx, "uReqId"));
    r.method = TableString(L, idx, "strMethod", "GET");
    r.host = TableString(L, idx, "strHost");
    lua_getfield(L, idx, "port");
    const bool hasPort = lua_isnumber(L, -1) != 0;
    const int altPort = hasPort ? static_cast<int>(lua_tonumber(L, -1)) : 0;
    lua_pop(L, 1);
    r.port = static_cast<int>(Field(L, idx, "usPort"));
    if (hasPort && (r.port == 0 || r.port == 80)) r.port = altPort;
    if (r.port <= 0) r.port = 80;
    r.action = TableString(L, idx, "strAction", "/");
    r.param = TableString(L, idx, "strParam");
    r.body = TableString(L, idx, "__buffer");
    lua_getfield(L, idx, "Buffer");
    if (lua_isstring(L, -1)) r.body += ArgString(L, -1);
    lua_pop(L, 1);
    ReadPairs(L, idx, "Header", r.headers);
    ReadPairs(L, idx, "Cookie", r.cookies);
    r.downloadFile = TableString(L, idx, "__downloadFile");
    r.retry = static_cast<int>(Field(L, idx, "ucRetry"));
    const int t = static_cast<int>(Field(L, idx, "nTimeReq"));
    if (t > 0) r.timeoutMs = t;
    Log("HTTP %s %s:%d%s%s%s", r.method.c_str(), r.host.c_str(), r.port, r.action.c_str(),
        r.downloadFile.empty() ? "" : " -> ", r.downloadFile.c_str());
    HttpSend(r);
    lua_pushboolean(L, 1);
    return 1;
}

// GetDownloadInfo(reqId) -> {nRecvSize, nTotalSize}
int l_GetDownloadInfo(lua_State* L) {
    const int idx = lua_isnumber(L, 2) ? 2 : 1;
    uint32_t recv = 0, total = 0;
    HttpProgress(static_cast<uint32_t>(lua_tonumber(L, idx)), recv, total);
    lua_createtable(L, 0, 2);
    lua_pushnumber(L, recv); lua_setfield(L, -2, "nRecvSize");
    lua_pushnumber(L, total); lua_setfield(L, -2, "nTotalSize");
    return 1;
}

// ---------------------------------------------------------------- CCocos2dxDelegate

int l_IsFileExist(lua_State* L) {
    const std::string p = PathArg(L);
    CCFileUtils* fu = CCFileUtils::sharedFileUtils();
    lua_pushboolean(L, !p.empty() && fu->isFileExist(fu->fullPathForFilename(p.c_str())));
    return 1;
}

int l_GetSpriteMap(lua_State* L) {
    const std::string plist = PlistForFrame(PathArg(L));
    lua_pushlstring(L, plist.data(), plist.size());
    return 1;
}

int l_GetTexturePath(lua_State* L) {
    std::string p = PathArg(L);
    const size_t dot = p.rfind('.');
    if (dot != std::string::npos) p = p.substr(0, dot);
    p += ".png";
    lua_pushlstring(L, p.data(), p.size());
    return 1;
}

// ---------------------------------------------------------------- 注册

// 新建一个表作为全局 name，带 GetSingleton（返回自身）
void BeginClass(lua_State* L, const char* name) {
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_pushcclosure(L, l_Self, 1);
    lua_setfield(L, -2, "GetSingleton");
    lua_pushvalue(L, -1);
    lua_setglobal(L, name);
}

void Fn(lua_State* L, const char* name, lua_CFunction f) {
    lua_pushcfunction(L, f);
    lua_setfield(L, -2, name);
}

void IntField(lua_State* L, const char* name, int v) {
    lua_pushinteger(L, v);
    lua_setfield(L, -2, name);
}

// 任意方法都是空函数的表（统计 SDK 等）
int l_NoopIndex(lua_State* L) {
    lua_pushcfunction(L, l_Noop);
    return 1;
}

void NoopClass(lua_State* L, const char* name) {
    BeginClass(L, name);
    lua_newtable(L);
    lua_pushcfunction(L, l_NoopIndex);
    lua_setfield(L, -2, "__index");
    lua_setmetatable(L, -2);
    lua_pop(L, 1);
}

void CallableClass(lua_State* L, const char* name, lua_CFunction call) {
    lua_newtable(L);
    lua_newtable(L);
    lua_pushcfunction(L, call);
    lua_setfield(L, -2, "__call");
    lua_setmetatable(L, -2);
    lua_setglobal(L, name);
}

}  // namespace

void SaveSystemVariables() { SaveSys(); }

void LuaExportTick(lua_State* L) {
    if (!g_reloadPending || !L) return;
    g_reloadPending = false;
    static const char* kContinue =
        "local ap = Logic and Logic:Get('AutoPatch')\n"
        "if ap and ap.done then ap:done() end\n";
    if (luaL_loadbuffer(L, kContinue, strlen(kContinue), "=reload") != 0) {
        Log("热更新收尾脚本编译失败：%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }
    PCall(L, 0, 0);
}

void RegisterExportBindings(lua_State* L, const EnvInfo& env) {
    g_env = env;
    InitSysVars();
    LoadStrRes();
    LoadFontIni();

    SetEnum(L, kSysVars, GV_COUNT);
    SetEnum(L, kUsrVars, sizeof(kUsrVars) / sizeof(kUsrVars[0]));
    SetEnum(L, kRelayEvents, sizeof(kRelayEvents) / sizeof(kRelayEvents[0]));
    SetEnum(L, kNetwork, sizeof(kNetwork) / sizeof(kNetwork[0]));
    SetEnum(L, kDeviceToken, sizeof(kDeviceToken) / sizeof(kDeviceToken[0]));
    SetEnum(L, kReflect, sizeof(kReflect) / sizeof(kReflect[0]));

    lua_register(L, "WriteLog", l_WriteLog);
    lua_register(L, "DbgPrtCon", l_WriteLog);
    lua_register(L, "DbgPrtOut", l_WriteLog);
    lua_register(L, "TimeGetTime", l_TimeGetTime);
    lua_register(L, "TwUnzip", l_TwUnzip);
    lua_register(L, "TwGetStr", l_TwGetStr);
    lua_register(L, "CheckDeviceIsSupportETC1", l_False);
    lua_register(L, "TwZlibDeflate", l_ZlibDeflate);
    lua_register(L, "TwZlibInflate", l_ZlibInflate);
    lua_register(L, "TwQuickLZDeflate", l_QlzDeflate);
    lua_register(L, "TwQuickLZInflate", l_QlzInflate);
    lua_register(L, "CTwUtilEncrypt", l_Encrypt);
    lua_register(L, "CTwUtilDecrypt", l_Decrypt);
    lua_register(L, "CNetMgrSendMsg", l_NetSendMsg);

    // CTwUtil
    BeginClass(L, "CTwUtil");
    IntField(L, "E_TP_WIN32", E_TP_WIN32);
    IntField(L, "E_TP_MAC", E_TP_MAC);
    IntField(L, "E_TP_ANDROID", E_TP_ANDROID);
    IntField(L, "E_TP_WP8", E_TP_WP8);
    Fn(L, "GetPlatform", l_GetPlatform);
    Fn(L, "IsDBCSLeadByte", l_False);
    Fn(L, "PrintCurrentMem", l_Noop);
    Fn(L, "GetCurrentMem", l_GetCurrentMem);
    Fn(L, "MakeSaltKey", l_MakeSaltKey);
    Fn(L, "Encrypt", l_Encrypt);
    Fn(L, "Decrypt", l_Decrypt);
    Fn(L, "GetDefaultFontName", l_GetDefaultFontName);
    Fn(L, "GetFontSize", l_GetFontSize);
    lua_pop(L, 1);

    // Tw.Zlib / Tw.QuickLZ / Tw.Utf8
    lua_newtable(L);
    lua_newtable(L);
    Fn(L, "Deflate", l_ZlibDeflate);
    Fn(L, "Inflate", l_ZlibInflate);
    lua_setfield(L, -2, "Zlib");
    lua_newtable(L);
    Fn(L, "Deflate", l_QlzDeflate);
    Fn(L, "Inflate", l_QlzInflate);
    lua_setfield(L, -2, "QuickLZ");
    lua_newtable(L);
    Fn(L, "FromAnsi", l_Identity);  // 资源和脚本都是 UTF-8
    Fn(L, "ToAnsi", l_Identity);
    lua_setfield(L, -2, "Utf8");
    lua_setglobal(L, "Tw");

    // CNetMgr
    BeginClass(L, "CNetMgr");
    Fn(L, "Connect", l_NetConnect);
    Fn(L, "Disconnect", l_NetDisconnect);
    Fn(L, "SendMsg", l_NetSendMsg);
    lua_pop(L, 1);

    // CVariableSystem
    BeginClass(L, "CVariableSystem");
    Fn(L, "Reset", l_ResetVars);
    Fn(L, "SaveSysVariable", l_SaveSysVariable);
    Fn(L, "SaveUsrVariable", l_SaveUsrVariable);
    Fn(L, "LoadSysVariable", l_LoadSysVariable);
    Fn(L, "LoadUsrVariable", l_LoadUsrVariable);
    Fn(L, "GetSysVariable", l_GetSysVariable);
    Fn(L, "SetSysVariable", l_SetSysVariable);
    Fn(L, "GetSysVarDelay", l_GetSysVariable);
    Fn(L, "SetSysVarDelay", l_SetSysVariable);
    Fn(L, "GetUsrVariable", l_GetUsrVariable);
    Fn(L, "SetUsrVariable", l_SetUsrVariable);
    Fn(L, "GetUsrVarDelay", l_GetUsrVariable);
    Fn(L, "SetUsrVarDelay", l_SetUsrVariable);
    lua_pop(L, 1);

    // CReflectSystem / CRelayEvent
    BeginClass(L, "CReflectSystem");
    Fn(L, "RegisterEvent", l_Noop);
    Fn(L, "OnProcEditFoucs", l_Noop);
    Fn(L, "OnProcEditChgPos", l_Noop);
    Fn(L, "FireEvent", l_FireEvent);
    lua_pop(L, 1);
    BeginClass(L, "CRelayEvent");
    Fn(L, "RegisterEvent", l_Noop);
    lua_pop(L, 1);

    // CEnvRoot（不提供 GetKeychainItem/SetKeychainItem/GetMacAddr：Lua 用 rawget/nil 检查后会走默认逻辑）
    BeginClass(L, "CEnvRoot");
    Fn(L, "IsDevMode", l_False);
    Fn(L, "GetUniqueId", l_GetUniqueId);
    Fn(L, "GetVersionName", l_GetVersionName);
    Fn(L, "GetIdfa", l_EmptyString);
    Fn(L, "GetPushUri", l_EmptyString);
    Fn(L, "SetReloadAll", l_SetReloadAll);
    Fn(L, "IsReloading", l_False);
    lua_pop(L, 1);
    NoopClass(L, "CEnvInstanceMgr");

    // CTwDirUtils
    BeginClass(L, "CTwDirUtils");
    Fn(L, "MkDir", l_MkDir);
    Fn(L, "RmDir", l_RmDir);
    Fn(L, "FileStat", l_FileStat);
    Fn(L, "DirStat", l_DirStat);
    Fn(L, "DelFile", l_DelFile);
    Fn(L, "ClrContent", l_ClrContent);
    Fn(L, "Rename", l_Rename);
    lua_pop(L, 1);

    // ITwHttp
    BeginClass(L, "ITwHttp");
    lua_pushvalue(L, -1);
    lua_pushcclosure(L, l_Self, 1);
    lua_setfield(L, -2, "GetInstance");
    lua_pushvalue(L, -1);
    lua_pushcclosure(L, l_Self, 1);
    lua_setfield(L, -2, "CreateInstance");
    Fn(L, "ReleaseInstance", l_Noop);
    Fn(L, "Init", l_Noop);
    Fn(L, "SendRequest", l_SendRequest);
    Fn(L, "GetDownloadInfo", l_GetDownloadInfo);
    Fn(L, "Request", l_NewRequest);
    const char* errs[] = {"ERR_SUCC", "ERR_CREATE_THREAD", "ERR_HEADER_FORMAT", "ERR_CREATE_SOCKET", "ERR_GETHOSTBYNAME",
                          "ERR_CONNECT", "ERR_SEND", "ERR_RECV", "ERR_TIMEOUT", "ERR_DECODE", "ERR_CONN_RESET"};
    for (int i = 0; i < 11; ++i) IntField(L, errs[i], i);
    lua_pop(L, 1);

    // CCocos2dxDelegate
    BeginClass(L, "CCocos2dxDelegate");
    Fn(L, "isFileExist", l_IsFileExist);
    Fn(L, "getSpriteMap", l_GetSpriteMap);
    Fn(L, "getTexturePath", l_GetTexturePath);
    lua_pop(L, 1);

    NoopClass(L, "CUMengAgent");
    NoopClass(L, "CTalkingDataAgent");
    NoopClass(L, "TwHostHelperAction");

    // 值类型
    CallableClass(L, "CMd5", l_CMd5Call);
    CallableClass(L, "TwEvtArgs", l_EvtArgsCall);
    CallableClass(L, "TwReflectEvtArgs", l_EvtArgsCall);
    luaL_newmetatable(L, "host.TwPoint");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    Fn(L, "Dist", l_PointDist);
    Fn(L, "__eq", l_PointEq);
    lua_pop(L, 1);
    CallableClass(L, "TwPoint", l_PointCall);
    luaL_newmetatable(L, "host.TwRect");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    Fn(L, "Width", l_RectWidth);
    Fn(L, "Height", l_RectHeight);
    Fn(L, "IsIn", l_RectIsIn);
    lua_pop(L, 1);
    CallableClass(L, "TwRect", l_RectCall);
}

}  // namespace host
