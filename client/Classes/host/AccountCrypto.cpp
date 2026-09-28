#include "AccountCrypto.h"

#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <stdlib.h>  // arc4random_buf（iOS / macOS）
#endif

#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"
#include "mbedtls/sha256.h"

#include "HostLog.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

namespace host {

namespace {

void RandomBytes(unsigned char* buf, size_t len) {
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, buf, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        Log("BCryptGenRandom 失败");
        abort();  // 没有安全随机数时不能继续加密
    }
#else
    arc4random_buf(buf, len);
#endif
}

int RngCallback(void*, unsigned char* buf, size_t len) {
    RandomBytes(buf, len);
    return 0;
}

std::string Arg(lua_State* L, int idx) {
    size_t n = 0;
    const char* s = luaL_checklstring(L, idx, &n);
    return std::string(s, n);
}

void Push(lua_State* L, const std::string& s) { lua_pushlstring(L, s.data(), s.size()); }

// random(n) -> n 字节安全随机数
int l_Random(lua_State* L) {
    const int n = luaL_checkint(L, 1);
    luaL_argcheck(L, n > 0 && n <= 4096, 1, "bad size");
    std::string s(static_cast<size_t>(n), '\0');
    RandomBytes(reinterpret_cast<unsigned char*>(&s[0]), s.size());
    Push(L, s);
    return 1;
}

// rsaOaepSha1(publicKeyDer, plain, requiredBits) -> cipher | nil, err
int l_RsaOaep(lua_State* L) {
    const std::string der = Arg(L, 1), plain = Arg(L, 2);
    const int bits = luaL_optint(L, 3, 0);
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    std::string out;
    const char* err = nullptr;
    if (mbedtls_pk_parse_public_key(&pk, reinterpret_cast<const unsigned char*>(der.data()), der.size()) != 0 ||
        mbedtls_pk_get_type(&pk) != MBEDTLS_PK_RSA) {
        err = "bad public key";
    } else {
        mbedtls_rsa_context* rsa = mbedtls_pk_rsa(pk);
        if (bits && mbedtls_rsa_get_bitlen(rsa) != static_cast<size_t>(bits)) {
            err = "unexpected key size";
        } else if (mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA1) != 0) {
            err = "set padding";
        } else {
            out.resize(mbedtls_rsa_get_len(rsa));
            if (mbedtls_rsa_rsaes_oaep_encrypt(rsa, RngCallback, nullptr, nullptr, 0, plain.size(),
                                               reinterpret_cast<const unsigned char*>(plain.data()),
                                               reinterpret_cast<unsigned char*>(&out[0])) != 0) {
                err = "encrypt";
            }
        }
    }
    mbedtls_pk_free(&pk);
    if (err) {
        lua_pushnil(L);
        lua_pushstring(L, err);
        return 2;
    }
    Push(L, out);
    return 1;
}

// aesCbc(encrypt, key16, iv16, data) -> result | nil, err   （PKCS#7 填充，和 Java 的 PKCS5Padding 相同）
int l_AesCbc(lua_State* L) {
    const bool enc = lua_toboolean(L, 1) != 0;
    const std::string key = Arg(L, 2), iv0 = Arg(L, 3), data = Arg(L, 4);
    if ((key.size() != 16 && key.size() != 24 && key.size() != 32) || iv0.size() != 16) {
        lua_pushnil(L);
        lua_pushstring(L, "bad key/iv");
        return 2;
    }
    std::string in = data;
    if (enc) {
        const size_t pad = 16 - in.size() % 16;
        in.append(pad, static_cast<char>(pad));
    } else if (in.empty() || in.size() % 16 != 0) {
        lua_pushnil(L);
        lua_pushstring(L, "bad length");
        return 2;
    }
    unsigned char iv[16];
    memcpy(iv, iv0.data(), 16);
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    const unsigned char* k = reinterpret_cast<const unsigned char*>(key.data());
    const unsigned int kbits = static_cast<unsigned int>(key.size() * 8);
    int rc = enc ? mbedtls_aes_setkey_enc(&ctx, k, kbits) : mbedtls_aes_setkey_dec(&ctx, k, kbits);
    std::string out(in.size(), '\0');
    if (rc == 0) {
        rc = mbedtls_aes_crypt_cbc(&ctx, enc ? MBEDTLS_AES_ENCRYPT : MBEDTLS_AES_DECRYPT, in.size(), iv,
                                   reinterpret_cast<const unsigned char*>(in.data()),
                                   reinterpret_cast<unsigned char*>(&out[0]));
    }
    mbedtls_aes_free(&ctx);
    if (rc != 0) {
        lua_pushnil(L);
        lua_pushstring(L, "aes");
        return 2;
    }
    if (!enc) {
        const unsigned char pad = static_cast<unsigned char>(out.back());
        bool ok = pad >= 1 && pad <= 16 && pad <= out.size();
        for (size_t i = 0; ok && i < pad; ++i) ok = static_cast<unsigned char>(out[out.size() - 1 - i]) == pad;
        if (!ok) {
            lua_pushnil(L);
            lua_pushstring(L, "bad padding");
            return 2;
        }
        out.resize(out.size() - pad);
    }
    Push(L, out);
    return 1;
}

// hmacSha256(key, data) -> 32 字节
int l_HmacSha256(lua_State* L) {
    const std::string key = Arg(L, 1), data = Arg(L, 2);
    unsigned char out[32];
    if (mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), reinterpret_cast<const unsigned char*>(key.data()),
                        key.size(), reinterpret_cast<const unsigned char*>(data.data()), data.size(), out) != 0) {
        return luaL_error(L, "hmac failed");
    }
    lua_pushlstring(L, reinterpret_cast<const char*>(out), sizeof(out));
    return 1;
}

int l_Sha256(lua_State* L) {
    const std::string data = Arg(L, 1);
    unsigned char out[32];
    mbedtls_sha256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), out, 0);
    lua_pushlstring(L, reinterpret_cast<const char*>(out), sizeof(out));
    return 1;
}

// 常量时间比较
int l_Equal(lua_State* L) {
    const std::string a = Arg(L, 1), b = Arg(L, 2);
    unsigned char diff = a.size() == b.size() ? 0 : 1;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    lua_pushboolean(L, diff == 0);
    return 1;
}

const char kUrlAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

// b64u(bytes) -> base64url，无填充（Android Base64 flags = URL_SAFE | NO_WRAP | NO_PADDING）
int l_B64u(lua_State* L) {
    const std::string in = Arg(L, 1);
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const unsigned v = (static_cast<unsigned char>(in[i]) << 16) | (static_cast<unsigned char>(in[i + 1]) << 8) |
                           static_cast<unsigned char>(in[i + 2]);
        out += kUrlAlphabet[(v >> 18) & 63];
        out += kUrlAlphabet[(v >> 12) & 63];
        out += kUrlAlphabet[(v >> 6) & 63];
        out += kUrlAlphabet[v & 63];
    }
    if (i < in.size()) {
        unsigned v = static_cast<unsigned char>(in[i]) << 16;
        if (i + 1 < in.size()) v |= static_cast<unsigned char>(in[i + 1]) << 8;
        out += kUrlAlphabet[(v >> 18) & 63];
        out += kUrlAlphabet[(v >> 12) & 63];
        if (i + 1 < in.size()) out += kUrlAlphabet[(v >> 6) & 63];
    }
    Push(L, out);
    return 1;
}

// unb64(str) -> bytes | nil。同时接受标准和 url 字母表，忽略 '=' 填充
int l_Unb64(lua_State* L) {
    const std::string in = Arg(L, 1);
    std::string out;
    unsigned acc = 0;
    int bits = 0;
    for (char ch : in) {
        int v;
        if (ch >= 'A' && ch <= 'Z') v = ch - 'A';
        else if (ch >= 'a' && ch <= 'z') v = ch - 'a' + 26;
        else if (ch >= '0' && ch <= '9') v = ch - '0' + 52;
        else if (ch == '-' || ch == '+') v = 62;
        else if (ch == '_' || ch == '/') v = 63;
        else if (ch == '=') break;
        else {
            lua_pushnil(L);
            return 1;
        }
        acc = (acc << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((acc >> bits) & 0xFF);
        }
    }
    Push(L, out);
    return 1;
}

// ---------------------------------------------------------------- 协议（对应 CryptoPlatformV1.java）
const char* kProtocol = R"LUA(
local C = HostCrypto
local function print(s) local f = rawget(_G, "HostLog"); if f then f(s) end end
local cj = (cjson.new and cjson.new()) or cjson
pcall(cj.encode_number_precision, 14)

local KEY_ID = "rsa-v1"
local PUBLIC_KEY = C.unb64("MIIBojANBgkqhkiG9w0BAQEFAAOCAY8AMIIBigKCAYEAnYPGoYcg9nImxJz0KI3cJFcDWn4OYHjHsW0tgAI5OfsbaYWHjYVCBAa8A8pA19CIeW6KWUECEARiNxT6X5wsq1CbZMawY1cHp2V+12YpA4sWVIFxqwT7lnGtYfJuqr/9w9yOjppz88u9/UnqoOJFMuqwKr2wLvlLEJUCStW+IMTtnjLMWf5GtkGH071dgfahF/oxCzh2Ajnz1M2YXQwUsoA0+/yELXwfdOdumgBjs/Owr0NgaGr+T8WLr7kTlJzm5cQQpzmTD5wM5F72TgybV5NZXFjMTr/IOc1+rJPmTZipJg4YWa05Ie38W6yfNS+zDhCbnLCLUpC2W9n8POZN9Cxsfi37vEdwJlfYgUbBKz7bo139HViklJOqjXVJTxtkR02rFTj5asfQQoMC9ijVmnNUSHOENynvWS9oYZo6dFMOHRxcmg+QrfJ2Wd49iBZ8x8kAnioduzHiChRqd70mKu2wumOtkD8oJJRX9HAYFN1lrmNY2WZ0z52/ptBsyjvXAgMBAAE=")
local REQUEST_DOMAIN = "AXY2 C2S\0"
local RESPONSE_DOMAIN = "AXY2 S2C\0"
local PROOF_DOMAIN = "AXY TCP POP V2\0"
local PENDING_SECONDS = 60
local MAX_PENDING = 4

local pending, pendingOrder = {}, {}
local tickets, ticketOrder = {}, {}

local function now() return os.clock() end

local function be32(n)
  n = n % 4294967296
  return string.char(math.floor(n / 16777216) % 256, math.floor(n / 65536) % 256, math.floor(n / 256) % 256, n % 256)
end
local function part(s) return be32(#s) .. s end
local function macInput(domain, ...)
  local t = { domain }
  for i = 1, select("#", ...) do t[#t + 1] = part(select(i, ...)) end
  return table.concat(t)
end

local function reqString(t, k, maxLen)
  local v = t[k]
  if type(v) ~= "string" or #v == 0 or #v > maxLen then error("invalid string " .. k, 0) end
  return v
end
local function reqInt(t, k)
  local v = tonumber(t[k])
  if not v or v ~= math.floor(v) then error("invalid int " .. k, 0) end
  return v
end
local function decodeCanonical(s)
  if type(s) ~= "string" or not s:match("^[A-Za-z0-9_%-]+$") then error("invalid base64url", 0) end
  local b = C.unb64(s)
  if not b or C.b64u(b) ~= s then error("noncanonical base64url", 0) end
  return b
end
local function check(ok, msg) if not ok then error(msg, 0) end end

local function removeKey(order, key)
  for i = #order, 1, -1 do if order[i] == key then table.remove(order, i) end end
end

local function prune()
  local t = now()
  for i = #pendingOrder, 1, -1 do
    local k = pendingOrder[i]
    local p = pending[k]
    if not p or t - p.created >= PENDING_SECONDS then pending[k] = nil; table.remove(pendingOrder, i) end
  end
  for i = #ticketOrder, 1, -1 do
    local k = ticketOrder[i]
    local c = tickets[k]
    if not c or t >= c.expires then tickets[k] = nil; table.remove(ticketOrder, i) end
  end
end

-- 结果通过 OnLogin(0, {"checkSidFunc":..., "checkSidFuncParam":...}) 回给 Lua；和安卓一样放到下一帧
local queue = {}
local scheduled = nil
local function flush()
  local sch = CCDirector:sharedDirector():getScheduler()
  if scheduled then sch:unscheduleScriptEntry(scheduled); scheduled = nil end
  local q = queue
  queue = {}
  for _, s in ipairs(q) do
    local ok, err = pcall(OnLogin, 0, s)
    if not ok then print("[AccountCrypto] OnLogin error: " .. tostring(err)) end
  end
end
local function callback(func, param)
  queue[#queue + 1] = cj.encode({ checkSidFunc = func, checkSidFuncParam = param })
  if not scheduled then
    scheduled = CCDirector:sharedDirector():getScheduler():scheduleScriptFunc(flush, 0, false)
  end
end
local function callbackAccount(token, code, data)
  callback("OnAccountCrypto", { token = token or "", transportCode = code, data = data or "" })
end
local function callbackProof(token, code, credential)
  local p = { token = token or "", transportCode = code }
  if code == 0 then p.credential = credential end
  callback("OnTcpLoginProof", p)
end

local function seal(req, token)
  local op = reqString(req, "op", 32)
  local clientVersion = reqString(req, "clientVersion", 32)
  local zoneId = reqInt(req, "zoneId")
  check(zoneId > 0, "invalid zone")
  check(type(req.payload) == "table", "invalid payload")
  check(#cj.encode(req.payload) <= 2048, "payload is too large")
  local rnd = C.random(96)
  local encKey, macKey = rnd:sub(1, 16), rnd:sub(17, 48)
  local respEnc, respMac = rnd:sub(49, 64), rnd:sub(65, 96)
  local rid = C.b64u(C.random(16))
  local nonce = C.b64u(C.random(16))
  local ivRaw = C.random(16)
  local iv = C.b64u(ivRaw)
  local inner = cj.encode({ v = 2, op = op, clientVersion = clientVersion, zoneId = zoneId, nonce = nonce,
                            issuedAt = os.time(), payload = req.payload })
  local ct = C.b64u(assert(C.aesCbc(true, encKey, ivRaw, inner)))
  local ekRaw, err = C.rsaOaepSha1(PUBLIC_KEY, rnd, 3072)
  check(ekRaw, "rsa: " .. tostring(err))
  local ek = C.b64u(ekRaw)
  local mac = C.b64u(C.hmacSha256(macKey, macInput(REQUEST_DOMAIN, "2", KEY_ID, rid, ek, iv, ct)))
  local envelope = cj.encode({ v = 2, kid = KEY_ID, rid = rid, ek = ek, iv = iv, ct = ct, mac = mac })
  prune()
  removeKey(pendingOrder, token)
  pending[token] = { rid = rid, nonce = nonce, op = op, clientVersion = clientVersion, zoneId = zoneId,
                     respEnc = respEnc, respMac = respMac, created = now() }
  pendingOrder[#pendingOrder + 1] = token
  while #pendingOrder > MAX_PENDING do pending[table.remove(pendingOrder, 1)] = nil end
  callbackAccount(token, 0, cj.encode({ requestId = rid, envelope = C.b64u(envelope) }))
end

local function rememberTicket(p, zoneId)
  check(tonumber(p.code) == 0 and tonumber(p.ticketVersion) == 2, "invalid ticket response")
  local sign = reqString(p, "sign", 2048)
  local raw = decodeCanonical(sign)
  check(#raw >= 31 and raw:byte(1) == 2, "invalid ticket")
  local klen = raw:byte(2)
  check(klen ~= 0 and #raw >= klen + 30, "invalid ticket key id")
  local secret = decodeCanonical(reqString(p, "sessionSecret", 128))
  check(#secret == 32, "invalid session secret")
  local userId = reqString(p, "userId", 32)
  check(userId:match("^[1-9]%d*$") and #userId <= 19, "invalid account id")
  local serverId, t, expiresAt = reqInt(p, "serverId"), reqInt(p, "time"), reqInt(p, "expiresAt")
  check(serverId == zoneId and expiresAt > t and expiresAt - t <= 300, "invalid ticket time")
  check(raw:sub(3, 2 + klen) == "z" .. serverId .. "-v1", "invalid ticket key id")
  prune()
  for i = #ticketOrder, 1, -1 do
    local c = tickets[ticketOrder[i]]
    if c and c.zoneId == serverId and c.accountId == userId then tickets[ticketOrder[i]] = nil; table.remove(ticketOrder, i) end
  end
  removeKey(ticketOrder, sign)
  tickets[sign] = { ticketHash = C.sha256(sign), secret = secret, accountId = userId, zoneId = serverId,
                    expires = now() + (expiresAt - t) }
  ticketOrder[#ticketOrder + 1] = sign
  while #ticketOrder > 4 do tickets[table.remove(ticketOrder, 1)] = nil end
end

local function open(req, token)
  prune()
  local p = pending[token]
  pending[token] = nil
  removeKey(pendingOrder, token)
  check(p, "unknown request")
  local resp = cj.decode(reqString(req, "response", 16384))
  check(type(resp) == "table" and tonumber(resp.code) == 0 and tonumber(resp.secureVersion) == 2, "invalid response")
  local env = cj.decode(decodeCanonical(reqString(resp, "envelope", 16384)))
  check(type(env) == "table" and tonumber(env.v) == 2 and env.kid == KEY_ID and env.rid == p.rid, "response binding mismatch")
  local ivS, ctS, macS = reqString(env, "iv", 64), reqString(env, "ct", 16384), reqString(env, "mac", 64)
  local iv, ct, mac = decodeCanonical(ivS), decodeCanonical(ctS), decodeCanonical(macS)
  check(#iv == 16 and #mac == 32 and #ct > 0 and #ct % 16 == 0, "invalid encrypted response size")
  local expect = C.hmacSha256(p.respMac, macInput(RESPONSE_DOMAIN, "2", KEY_ID, p.rid, "", ivS, ctS))
  check(C.equal(expect, mac), "response authentication failed")
  local plain = C.aesCbc(false, p.respEnc, iv, ct)
  check(plain and #plain <= 8192, "invalid response body")
  local body = cj.decode(plain)
  check(type(body) == "table" and tonumber(body.v) == 2 and body.rid == p.rid and body.requestNonce == p.nonce and
        body.op == p.op and body.clientVersion == p.clientVersion and tonumber(body.zoneId) == p.zoneId,
        "response binding mismatch")
  local payload = body.payload
  check(type(payload) == "table", "invalid payload")
  if payload.sessionSecret ~= nil then rememberTicket(payload, p.zoneId) end
  local copy = {}
  for k, v in pairs(payload) do if k ~= "sessionSecret" then copy[k] = v end end
  callbackAccount(token, 0, cj.encode(copy))
end

local function tcpLoginProof(req)
  local token = reqString(req, "token", 64)
  local ticket = reqString(req, "ticket", 2048)
  local reference = reqString(req, "reference", 96):match("^%s*(.-)%s*$")
  local zoneId = reqInt(req, "zoneId")
  prune()
  local c = tickets[ticket]
  local okRef = c and reference:match("^[1-9]%d*%.%d+_" .. zoneId .. "$") and reference:sub(1, reference:find(".", 1, true) - 1) == c.accountId
  if not c or c.zoneId ~= zoneId or not okRef then return callbackProof(token, -1, "") end
  local d = cj.decode(req.describeJson)
  local expected = reqString(req, "expectedDescribeMd5", 32):lower()
  local got = type(d) == "table" and type(d.describeMd5) == "string" and d.describeMd5:lower() or ""
  if not expected:match("^[0-9a-f]+$") or #expected ~= 32 or expected ~= got or (tonumber(d.expiresAt) or 0) <= 0 then
    return callbackProof(token, -1, "")
  end
  local ok, nonce = pcall(decodeCanonical, d.tcpNonce)
  if not ok or #nonce ~= 32 then return callbackProof(token, -1, "") end
  local msg = PROOF_DOMAIN .. part(nonce) .. part(be32(zoneId)) .. part(reference) .. part(c.ticketHash) .. part(be32(4))
  callbackProof(token, 0, "p2." .. ticket .. "." .. C.b64u(C.hmacSha256(c.secret, msg)))
end

-- 入口：LuaExport.cpp 的 CReflectSystem:FireEvent(REFLECT_EVENT_LOGIN) 调过来
function HostAccountCrypto(str)
  local ok, req = pcall(cj.decode, str or "")
  if not ok or type(req) ~= "table" then return end
  local kind = req.type
  local token = type(req.token) == "string" and req.token or ""
  if kind == "accountCrypto" then
    local ok2, err = pcall(function()
      local action = reqString(req, "action", 16)
      reqString(req, "token", 64)
      if action == "seal" then return seal(req, token) end
      if action == "open" then return open(req, token) end
      if action == "discard" then
        pending[token] = nil
        removeKey(pendingOrder, token)
        return callbackAccount(token, -1, "")
      end
      error("invalid action", 0)
    end)
    if not ok2 then
      print("[AccountCrypto] " .. tostring(err))
      callbackAccount(token, -1, "")
    end
  elseif kind == "tcpLoginProof" then
    local ok2, err = pcall(tcpLoginProof, req)
    if not ok2 then
      print("[AccountCrypto] proof: " .. tostring(err))
      callbackProof(token, -1, "")
    end
  elseif kind == "accountCryptoClear" then
    pending, pendingOrder, tickets, ticketOrder = {}, {}, {}, {}
  else
    -- 其它登录类型安卓也不支持（LocalSdkPlatform）
    OnLogin(-1, '{"code":-1,"message":"external login unsupported"}')
  end
end
)LUA";

}  // namespace

void RegisterAccountCrypto(lua_State* L) {
    static const luaL_Reg fns[] = {
        {"random", l_Random},       {"rsaOaepSha1", l_RsaOaep}, {"aesCbc", l_AesCbc}, {"hmacSha256", l_HmacSha256},
        {"sha256", l_Sha256},       {"equal", l_Equal},         {"b64u", l_B64u},     {"unb64", l_Unb64},
        {nullptr, nullptr}};
    luaL_register(L, "HostCrypto", fns);
    lua_pop(L, 1);
    if (luaL_loadbuffer(L, kProtocol, strlen(kProtocol), "=host_account_crypto") != 0 || lua_pcall(L, 0, 0, 0) != 0) {
        Log("AccountCrypto 脚本加载失败：%s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

}  // namespace host
