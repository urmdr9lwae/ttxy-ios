#include "Net.h"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET SockT;
#define SOCK_INVALID INVALID_SOCKET
#define CloseSock closesocket
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
typedef int SockT;
#define SOCK_INVALID (-1)
#define CloseSock ::close
#endif

#include "HostLog.h"
#include "LuaCall.h"

extern "C" {
#include "lua.h"
}

namespace host {
namespace {

void InitSockets() {
    static bool done = false;
    if (done) return;
    done = true;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#else
    signal(SIGPIPE, SIG_IGN);
#endif
}

void SetTimeout(SockT s, int ms) {
#ifdef _WIN32
    DWORD t = static_cast<DWORD>(ms);
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof(t));
#else
    timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
#ifdef SO_NOSIGPIPE
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
}

// 解析并连接（支持 IPv4/IPv6，iOS 的 NAT64 网络需要走 getaddrinfo）；失败时 err 为 HttpError
SockT ConnectTo(const std::string& hostName, int port, int timeoutMs, int& err) {
    InitSockets();
    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    const std::string portStr = std::to_string(port);
    if (getaddrinfo(hostName.c_str(), portStr.c_str(), &hints, &res) != 0 || !res) {
        err = HTTP_ERR_GETHOSTBYNAME;
        return SOCK_INVALID;
    }
    SockT s = SOCK_INVALID;
    err = HTTP_ERR_CONNECT;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == SOCK_INVALID) {
            err = HTTP_ERR_CREATE_SOCKET;
            continue;
        }
        SetTimeout(s, timeoutMs);
        if (::connect(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0) {
            err = HTTP_ERR_SUCC;
            break;
        }
        CloseSock(s);
        s = SOCK_INVALID;
        err = HTTP_ERR_CONNECT;
    }
    freeaddrinfo(res);
    return s;
}

bool SendAll(SockT s, const char* p, size_t n) {
    while (n > 0) {
        const int k = ::send(s, p, static_cast<int>(n), 0);
        if (k <= 0) return false;
        p += k;
        n -= static_cast<size_t>(k);
    }
    return true;
}

// ---------------- 事件队列（后台线程 -> 主线程） ----------------

struct Event {
    enum Kind { HTTP_OK, HTTP_FAIL, NET_CONNECT, NET_READ, NET_CLOSED } kind;
    uint32_t id = 0;
    int code = 0;
    std::string data;
    uint64_t generation = 0;  // 长连接事件所属的连接代次，过期事件丢弃
};

std::mutex g_evMutex;
std::deque<Event> g_events;

void Push(Event&& e) {
    std::lock_guard<std::mutex> lock(g_evMutex);
    g_events.push_back(std::move(e));
}

// ---------------- HTTP ----------------

std::atomic<uint32_t> g_nextId{1};
std::mutex g_progMutex;
std::map<uint32_t, std::pair<uint32_t, uint32_t>> g_progress;

void SetProgress(uint32_t id, uint32_t recv, uint32_t total) {
    std::lock_guard<std::mutex> lock(g_progMutex);
    g_progress[id] = std::make_pair(recv, total);
}

std::string UrlHostHeader(const HttpRequest& r) {
    return r.port == 80 ? r.host : r.host + ":" + std::to_string(r.port);
}

// 执行一次请求：返回 HttpError；body 为响应体（已处理 chunked）
int DoHttp(const HttpRequest& r, std::string& body, int& status) {
    int err = 0;
    SockT s = ConnectTo(r.host, r.port, r.timeoutMs, err);
    if (s == SOCK_INVALID) return err;

    std::string action = r.action.empty() ? "/" : r.action;
    if (action[0] != '/') action = "/" + action;
    std::string payload = r.param + r.body;
    const bool isPost = r.method == "POST" || !payload.empty();
    std::string req = (isPost ? "POST " : "GET ") + action + " HTTP/1.1\r\n";
    req += "Host: " + UrlHostHeader(r) + "\r\n";
    req += "Connection: close\r\nAccept: */*\r\nUser-Agent: TwMobile\r\n";
    bool hasType = false;
    for (auto& h : r.headers) {
        req += h.first + ": " + h.second + "\r\n";
        if (h.first == "Content-Type") hasType = true;
    }
    if (!r.cookies.empty()) {
        req += "Cookie: ";
        for (size_t i = 0; i < r.cookies.size(); ++i)
            req += (i ? "; " : "") + r.cookies[i].first + "=" + r.cookies[i].second;
        req += "\r\n";
    }
    if (isPost) {
        if (!hasType) req += "Content-Type: application/x-www-form-urlencoded\r\n";
        req += "Content-Length: " + std::to_string(payload.size()) + "\r\n";
    }
    req += "\r\n";
    req += payload;
    if (!SendAll(s, req.data(), req.size())) {
        CloseSock(s);
        return HTTP_ERR_SEND;
    }

    std::string raw;
    char buf[16384];
    size_t headerEnd = std::string::npos;
    uint32_t contentLength = 0;
    for (;;) {
        const int k = ::recv(s, buf, sizeof(buf), 0);
        if (k < 0) {
            CloseSock(s);
            return HTTP_ERR_TIMEOUT;
        }
        if (k == 0) break;
        raw.append(buf, static_cast<size_t>(k));
        if (headerEnd == std::string::npos) {
            headerEnd = raw.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                const char* cl = nullptr;
                std::string head = raw.substr(0, headerEnd);
                for (auto& c : head) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                const size_t p = head.find("content-length:");
                if (p != std::string::npos) cl = raw.c_str() + p + 15;
                if (cl) contentLength = static_cast<uint32_t>(strtoul(cl, nullptr, 10));
            }
        }
        if (headerEnd != std::string::npos) {
            const uint32_t got = static_cast<uint32_t>(raw.size() - headerEnd - 4);
            SetProgress(r.id, got, contentLength ? contentLength : got);
            if (contentLength && got >= contentLength) break;
        }
    }
    CloseSock(s);
    if (headerEnd == std::string::npos) return HTTP_ERR_HEADER_FORMAT;

    std::string head = raw.substr(0, headerEnd);
    status = 0;
    const size_t sp = head.find(' ');
    if (sp != std::string::npos) status = atoi(head.c_str() + sp + 1);
    body = raw.substr(headerEnd + 4);

    std::string lower = head;
    for (auto& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    if (lower.find("transfer-encoding: chunked") != std::string::npos) {
        std::string out;
        size_t pos = 0;
        while (pos < body.size()) {
            const size_t eol = body.find("\r\n", pos);
            if (eol == std::string::npos) break;
            const size_t n = strtoul(body.c_str() + pos, nullptr, 16);
            if (n == 0) break;
            out.append(body, eol + 2, n);
            pos = eol + 2 + n + 2;
        }
        body.swap(out);
    }
    if (status == 301 || status == 302) {
        const size_t p = lower.find("\r\nlocation:");
        if (p != std::string::npos) {
            size_t b = p + 11;
            while (b < head.size() && head[b] == ' ') ++b;
            const size_t e = head.find("\r\n", b);
            body = head.substr(b, e == std::string::npos ? std::string::npos : e - b);
        }
    }
    return HTTP_ERR_SUCC;
}

bool ParseUrl(const std::string& url, HttpRequest& r) {
    const std::string pfx = "http://";
    if (url.compare(0, pfx.size(), pfx) != 0) return false;
    const size_t slash = url.find('/', pfx.size());
    std::string hostPort = url.substr(pfx.size(), slash == std::string::npos ? std::string::npos : slash - pfx.size());
    r.action = slash == std::string::npos ? "/" : url.substr(slash);
    const size_t colon = hostPort.find(':');
    r.host = hostPort.substr(0, colon);
    r.port = colon == std::string::npos ? 80 : atoi(hostPort.c_str() + colon + 1);
    return true;
}

void HttpWorker(HttpRequest r) {
    int err = HTTP_ERR_SUCC;
    int status = 0;
    std::string body;
    for (int attempt = 0; attempt <= r.retry; ++attempt) {
        err = DoHttp(r, body, status);
        for (int redirect = 0; err == HTTP_ERR_SUCC && (status == 301 || status == 302) && redirect < 3; ++redirect) {
            if (!ParseUrl(body, r)) break;
            err = DoHttp(r, body, status);
        }
        if (err == HTTP_ERR_SUCC) break;
    }
    if (err == HTTP_ERR_SUCC && (status < 200 || status >= 300)) {
        Log("HTTP %s:%d%s 返回状态 %d", r.host.c_str(), r.port, r.action.c_str(), status);
        err = HTTP_ERR_HEADER_FORMAT;
    }
    Event e;
    e.id = r.id;
    if (err == HTTP_ERR_SUCC) {
        if (!r.downloadFile.empty()) {
            std::ofstream f(r.downloadFile, std::ios::binary | std::ios::trunc);
            f.write(body.data(), static_cast<std::streamsize>(body.size()));
            if (!f) Log("HTTP 写文件失败：%s", r.downloadFile.c_str());
        }
        e.kind = Event::HTTP_OK;
        e.data.swap(body);
    } else {
        Log("HTTP 请求失败 %s:%d%s 错误 %d", r.host.c_str(), r.port, r.action.c_str(), err);
        e.kind = Event::HTTP_FAIL;
        e.code = err;
    }
    Push(std::move(e));
}

// ---------------- 长连接 ----------------

// BPHash：h = (h << 7) ^ (signed char)c
uint32_t BPHash(const std::string& s) {
    uint32_t h = 0;
    for (char c : s) h = (h << 7) ^ static_cast<uint32_t>(static_cast<int32_t>(static_cast<signed char>(c)));
    return h;
}

void PutBE32(std::string& out, uint32_t v) {
    out.push_back(static_cast<char>(v >> 24));
    out.push_back(static_cast<char>(v >> 16));
    out.push_back(static_cast<char>(v >> 8));
    out.push_back(static_cast<char>(v));
}

uint32_t GetBE32(const char* p) {
    const unsigned char* u = reinterpret_cast<const unsigned char*>(p);
    return (uint32_t(u[0]) << 24) | (uint32_t(u[1]) << 16) | (uint32_t(u[2]) << 8) | uint32_t(u[3]);
}

// 调试用：包的前 48 字节十六进制
std::string HexHead(const std::string& s) {
    static const char* d = "0123456789abcdef";
    std::string o;
    for (size_t i = 0; i < s.size() && i < 48; ++i) {
        o += d[(static_cast<unsigned char>(s[i]) >> 4) & 15];
        o += d[static_cast<unsigned char>(s[i]) & 15];
    }
    if (s.size() > 48) o += "...";
    return o;
}

struct Connection {
    std::mutex mutex;
    SockT sock = SOCK_INVALID;
    std::atomic<bool> closing{false};
    uint64_t generation = 0;
};

std::mutex g_connMutex;
std::shared_ptr<Connection> g_conn;
std::atomic<uint64_t> g_generation{0};

void NetWorker(std::shared_ptr<Connection> c, std::string hostName, int port) {
    int err = 0;
    SockT s = ConnectTo(hostName, port, 10000, err);
    Log("长连接 %s:%d 连接%s（err=%d）", hostName.c_str(), port, s == SOCK_INVALID ? "失败" : "成功", err);
    if (s == SOCK_INVALID || c->closing) {
        if (s != SOCK_INVALID) CloseSock(s);
        Event e;
        e.kind = Event::NET_CONNECT;
        e.code = 0;
        e.generation = c->generation;
        Push(std::move(e));
        return;
    }
    SetTimeout(s, 0);  // 长连接收包不设超时（超时由 Lua 自己的计时器处理）
    int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));
    {
        std::lock_guard<std::mutex> lock(c->mutex);
        c->sock = s;
    }
    {
        Event e;
        e.kind = Event::NET_CONNECT;
        e.code = 1;
        e.generation = c->generation;
        Push(std::move(e));
    }
    std::string buf;
    char tmp[16384];
    for (;;) {
        const int k = ::recv(s, tmp, sizeof(tmp), 0);
        if (k <= 0) {
#ifdef _WIN32
            Log("长连接 recv 返回 %d（WSA %d），连接结束", k, WSAGetLastError());
#else
            Log("长连接 recv 返回 %d（errno %d），连接结束", k, errno);
#endif
            break;
        }
#ifdef _DEBUG
        Log("长连接收到 %d 字节：%s", k, HexHead(std::string(tmp, static_cast<size_t>(k))).c_str());
#endif
        buf.append(tmp, static_cast<size_t>(k));
        // 切包
        bool bad = false;
        for (;;) {
            if (buf.size() < 8) break;
            if (GetBE32(buf.data()) != 0xFFFFFFFFu) {
                Log("长连接收到非法包头，断开");
                bad = true;
                break;
            }
            const uint32_t n = GetBE32(buf.data() + 4);
            if (n < 4 || n > 64u * 1024 * 1024) {
                bad = true;
                break;
            }
            if (buf.size() < 8 + static_cast<size_t>(n)) break;
            std::string payload = buf.substr(8, n - 4);
            const uint32_t hash = GetBE32(buf.data() + 8 + n - 4);
            buf.erase(0, 8 + static_cast<size_t>(n));
            if (hash != BPHash(payload)) {
                Log("长连接包校验失败，丢弃（长度 %u）", static_cast<unsigned>(payload.size()));
                continue;
            }
            Event e;
            e.kind = Event::NET_READ;
            e.data.swap(payload);
            e.generation = c->generation;
            Push(std::move(e));
        }
        if (bad) break;
    }
    {
        std::lock_guard<std::mutex> lock(c->mutex);
        if (c->sock != SOCK_INVALID) CloseSock(c->sock);
        c->sock = SOCK_INVALID;
    }
    if (!c->closing) {
        Event e;
        e.kind = Event::NET_CLOSED;
        e.generation = c->generation;
        Push(std::move(e));
    }
}

}  // namespace

uint32_t HttpNextId() { return g_nextId++; }

void HttpSend(const HttpRequest& req) {
    SetProgress(req.id, 0, 0);
    std::thread(HttpWorker, req).detach();
}

bool HttpProgress(uint32_t id, uint32_t& recv, uint32_t& total) {
    std::lock_guard<std::mutex> lock(g_progMutex);
    auto it = g_progress.find(id);
    if (it == g_progress.end()) return false;
    recv = it->second.first;
    total = it->second.second;
    return true;
}

void NetConnect(const std::string& hostName, int port) {
    NetDisconnect();
    auto c = std::make_shared<Connection>();
    c->generation = ++g_generation;
    {
        std::lock_guard<std::mutex> lock(g_connMutex);
        g_conn = c;
    }
    Log("长连接 %s:%d", hostName.c_str(), port);
    std::thread(NetWorker, c, hostName, port).detach();
}

void NetDisconnect() {
    std::shared_ptr<Connection> c;
    {
        std::lock_guard<std::mutex> lock(g_connMutex);
        c.swap(g_conn);
    }
    if (!c) return;
    c->closing = true;
    std::lock_guard<std::mutex> lock(c->mutex);
    if (c->sock != SOCK_INVALID) {
#ifdef _WIN32
        shutdown(c->sock, SD_BOTH);
#else
        shutdown(c->sock, SHUT_RDWR);
#endif
    }
}

bool NetSend(const std::string& payload) {
    std::shared_ptr<Connection> c;
    {
        std::lock_guard<std::mutex> lock(g_connMutex);
        c = g_conn;
    }
    if (!c) return false;
    std::string frame;
    frame.reserve(payload.size() + 12);
    PutBE32(frame, 0xFFFFFFFFu);
    PutBE32(frame, static_cast<uint32_t>(payload.size() + 4));
    frame += payload;
    PutBE32(frame, BPHash(payload));
    std::lock_guard<std::mutex> lock(c->mutex);
    if (c->sock == SOCK_INVALID) {
        Log("长连接未就绪，丢弃 %u 字节", static_cast<unsigned>(payload.size()));
        return false;
    }
#ifdef _DEBUG
    Log("长连接发送 %u 字节：%s", static_cast<unsigned>(payload.size()), HexHead(payload).c_str());
#endif
    return SendAll(c->sock, frame.data(), frame.size());
}

bool NetIsConnected() {
    std::lock_guard<std::mutex> lock(g_connMutex);
    if (!g_conn) return false;
    std::lock_guard<std::mutex> lock2(g_conn->mutex);
    return g_conn->sock != SOCK_INVALID;
}

void NetPoll() {
    std::deque<Event> events;
    {
        std::lock_guard<std::mutex> lock(g_evMutex);
        events.swap(g_events);
    }
    lua_State* L = GetLuaState();
    if (!L) return;
    const uint64_t gen = g_generation;
    for (auto& e : events) {
        switch (e.kind) {
        case Event::HTTP_OK: {
            lua_getglobal(L, "OnHttpRespose");
            lua_pushnumber(L, e.id);
            lua_pushlstring(L, e.data.data(), e.data.size());
            if (lua_isfunction(L, -3)) PCall(L, 2, 0); else lua_pop(L, 3);
            break;
        }
        case Event::HTTP_FAIL: {
            lua_getglobal(L, "OnHttpError");
            lua_pushnumber(L, e.id);
            lua_pushinteger(L, e.code);
            if (lua_isfunction(L, -3)) PCall(L, 2, 0); else lua_pop(L, 3);
            break;
        }
        case Event::NET_CONNECT:
            if (e.generation == gen) CallGlobalBool("OnNetworkConnect", e.code != 0);
            break;
        case Event::NET_READ:
            if (e.generation == gen) CallGlobal("OnNetworkRead", e.data);
            break;
        case Event::NET_CLOSED:
            if (e.generation == gen) CallGlobal("OnNetworkClosed");
            break;
        }
    }
}

void NetShutdown() { NetDisconnect(); }

}  // namespace host
