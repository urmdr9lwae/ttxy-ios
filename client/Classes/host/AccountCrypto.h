// 安卓版 com.eyugame.game.CryptoPlatformV1 的移植：
// 账号请求加密信封（RSA-3072-OAEP + AES-128-CBC + HMAC-SHA256）和 TCP 登录证明（p2 凭证）。
// 密码学原语用 Mbed TLS（C++），协议逻辑用 Lua（入口：全局函数 HostAccountCrypto(json)）。
#pragma once

struct lua_State;

namespace host {

void RegisterAccountCrypto(lua_State* L);

}  // namespace host
