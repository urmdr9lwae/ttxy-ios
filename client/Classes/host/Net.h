// 网络层：HTTP 请求（ITwHttp）和游戏长连接（CNetMgr），都在后台线程收发，
// 结果放进队列，由主线程每帧 NetPoll() 分发给 Lua 回调。
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace host {

// ---------- HTTP ----------
enum HttpError {
    HTTP_ERR_SUCC = 0,
    HTTP_ERR_CREATE_THREAD,
    HTTP_ERR_HEADER_FORMAT,
    HTTP_ERR_CREATE_SOCKET,
    HTTP_ERR_GETHOSTBYNAME,
    HTTP_ERR_CONNECT,
    HTTP_ERR_SEND,
    HTTP_ERR_RECV,
    HTTP_ERR_TIMEOUT,
    HTTP_ERR_DECODE,
    HTTP_ERR_CONN_RESET,
};

struct HttpRequest {
    uint32_t id = 0;
    std::string method = "GET";
    std::string host;
    int port = 80;
    std::string action = "/";   // 路径 + 查询串
    std::string param;          // POST 表单参数（strParam）
    std::string body;           // AddBuffer 追加的数据
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<std::pair<std::string, std::string>> cookies;
    std::string downloadFile;   // 非空时把响应体写到这个文件
    int retry = 0;
    int timeoutMs = 15000;
};

uint32_t HttpNextId();
void HttpSend(const HttpRequest& req);
// 下载进度（字节），未知请求返回 false
bool HttpProgress(uint32_t id, uint32_t& recv, uint32_t& total);

// ---------- 长连接 ----------
// 帧格式（与原版 CNetMgr 一致）：FF FF FF FF | BE32(负载长度+4) | 负载 | BE32(BPHash(负载))
void NetConnect(const std::string& host, int port);
void NetDisconnect();
bool NetSend(const std::string& payload);
bool NetIsConnected();

// 主线程每帧调用：分发 HTTP 结果和网络事件到 Lua
void NetPoll();
void NetShutdown();

}  // namespace host
