#include "LuaCall.h"

#include <cctype>
#include <cstring>
#include <string>
#include <typeinfo>

#include "cocos2d.h"
#include "cocos-ext.h"
#include "HostLog.h"

extern "C" {
#include "lauxlib.h"
#include "tolua_fix.h"
}

USING_NS_CC;
USING_NS_CC_EXT;

namespace host {
namespace {

lua_State* g_L = nullptr;

// 错误处理：记录带调用栈的错误，再交给游戏自己的 __G__TRACKBACK__（它会写游戏日志）
int ErrorHandler(lua_State* L) {
    const char* msg = lua_tostring(L, 1);
    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_pushstring(L, msg ? msg : "(非字符串错误)");
    lua_pushinteger(L, 2);
    lua_call(L, 2, 1);
    Log("[Lua 错误] %s", lua_tostring(L, -1));
    lua_pop(L, 2);
    lua_getglobal(L, "__G__TRACKBACK__");
    if (lua_isfunction(L, -1)) {
        lua_pushvalue(L, 1);
        lua_pcall(L, 1, 0, 0);
    } else {
        lua_pop(L, 1);
    }
    lua_pushvalue(L, 1);
    return 1;
}

// typeid 名字转成不带命名空间的类名：MSVC "class cocos2d::CCSprite"，clang/gcc "N7cocos2d8CCSpriteE"
std::string ClassNameOf(const CCObject* obj) {
    std::string n = typeid(*obj).name();
#ifdef _MSC_VER
    const size_t sp = n.find(' ');
    if (sp != std::string::npos) n = n.substr(sp + 1);
    const size_t c = n.rfind("::");
    if (c != std::string::npos) n = n.substr(c + 2);
    return n;
#else
    // Itanium 修饰名：N<len>name<len>name...E 或 <len>name
    std::string last;
    size_t i = 0;
    if (i < n.size() && n[i] == 'N') ++i;
    while (i < n.size() && isdigit(static_cast<unsigned char>(n[i]))) {
        size_t len = 0;
        while (i < n.size() && isdigit(static_cast<unsigned char>(n[i]))) len = len * 10 + (n[i++] - '0');
        last = n.substr(i, len);
        i += len;
    }
    return last.empty() ? n : last;
#endif
}

bool IsToluaType(lua_State* L, const char* name) {
    luaL_getmetatable(L, name);
    const bool ok = lua_istable(L, -1);
    lua_pop(L, 1);
    return ok;
}

}  // namespace

lua_State* GetLuaState() { return g_L; }
void SetLuaState(lua_State* L) { g_L = L; }

bool PCall(lua_State* L, int nargs, int nresults) {
    lua_checkstack(L, 32);
    const int base = lua_gettop(L) - nargs;  // 函数所在位置
    lua_pushcfunction(L, ErrorHandler);
    lua_insert(L, base);
    const int rc = lua_pcall(L, nargs, nresults, base);
    lua_remove(L, base);
    if (rc != 0) {
        lua_pop(L, 1);  // 错误信息
        for (int i = 0; i < nresults; ++i) lua_pushnil(L);
        return false;
    }
    return true;
}

static bool PushGlobalFunc(lua_State* L, const char* name) {
    lua_checkstack(L, 64);
    lua_getglobal(L, name);
    if (lua_isfunction(L, -1)) return true;
    lua_pop(L, 1);
    return false;
}

bool CallGlobal(const char* name) {
    lua_State* L = g_L;
    if (!L || !PushGlobalFunc(L, name)) return false;
    return PCall(L, 0, 0);
}

bool CallGlobal(const char* name, int i1) {
    lua_State* L = g_L;
    if (!L || !PushGlobalFunc(L, name)) return false;
    lua_pushinteger(L, i1);
    return PCall(L, 1, 0);
}

bool CallGlobal(const char* name, int i1, const std::string& s2) {
    lua_State* L = g_L;
    if (!L || !PushGlobalFunc(L, name)) return false;
    lua_pushinteger(L, i1);
    lua_pushlstring(L, s2.data(), s2.size());
    return PCall(L, 2, 0);
}

bool CallGlobal(const char* name, const std::string& s1) {
    lua_State* L = g_L;
    if (!L || !PushGlobalFunc(L, name)) return false;
    lua_pushlstring(L, s1.data(), s1.size());
    return PCall(L, 1, 0);
}

bool CallGlobalBool(const char* name, bool b) {
    lua_State* L = g_L;
    if (!L || !PushGlobalFunc(L, name)) return false;
    lua_pushboolean(L, b ? 1 : 0);
    return PCall(L, 1, 0);
}

bool PushHandler(lua_State* L, int handler) {
    if (handler == 0) return false;
    // 原生回调可能发生在 Lua 调用的 C 函数内部（例如 reloadData 触发 tableCellAtIndex），
    // 此时只保证 LUA_MINSTACK 个空位；先扩容，否则 GC 收缩栈时会破坏越界压入的值
    lua_checkstack(L, 64);
    toluafix_get_function_by_refid(L, handler);
    if (lua_isfunction(L, -1)) return true;
    lua_pop(L, 1);
    return false;
}

int RefFunction(lua_State* L, int index) { return toluafix_ref_function(L, index, 0); }

void UnrefFunction(lua_State* L, int handler) {
    if (handler) toluafix_remove_function_by_refid(L, handler);
}

const char* ObjectTypeName(lua_State* L, CCObject* obj, const char* fallbackType) {
    if (!obj) return fallbackType;
    static std::string s_name;
    s_name = ClassNameOf(obj);
    if (IsToluaType(L, s_name.c_str())) return s_name.c_str();
    // 自定义子类（例如 CCB 生成的节点）按常见基类回退，越具体越靠前
#define TRY_TYPE(T) if (dynamic_cast<T*>(obj)) return #T
    TRY_TYPE(CCControlButton);
    TRY_TYPE(CCControl);
    TRY_TYPE(CCScale9Sprite);
    TRY_TYPE(CCMenuItemImage);
    TRY_TYPE(CCMenuItemSprite);
    TRY_TYPE(CCMenuItemLabel);
    TRY_TYPE(CCMenuItem);
    TRY_TYPE(CCMenu);
    TRY_TYPE(CCLabelTTF);
    TRY_TYPE(CCLabelBMFont);
    TRY_TYPE(CCLabelAtlas);
    TRY_TYPE(CCSprite);
    TRY_TYPE(CCParticleSystemQuad);
    TRY_TYPE(CCParticleSystem);
    TRY_TYPE(CCLayerColor);
    TRY_TYPE(CCScrollView);
    TRY_TYPE(CCLayer);
    TRY_TYPE(CCScene);
    TRY_TYPE(CCNode);
#undef TRY_TYPE
    return fallbackType;
}

void PushObject(lua_State* L, CCObject* obj, const char* fallbackType) {
    if (!obj) {
        lua_pushnil(L);
        return;
    }
    toluafix_pushusertype_ccobject(L, obj->m_uID, &obj->m_nLuaID, obj, ObjectTypeName(L, obj, fallbackType));
}

}  // namespace host
