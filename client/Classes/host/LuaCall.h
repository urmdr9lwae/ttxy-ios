// Lua 调用辅助：带 __G__TRACKBACK__ 的 pcall、调用全局函数、调用 tolua 函数引用（refid）。
#pragma once
#include <string>

extern "C" {
#include "lua.h"
}

namespace cocos2d { class CCObject; }

namespace host {

lua_State* GetLuaState();
void SetLuaState(lua_State* L);

// 调用栈顶下方 nargs 个参数的函数；出错时记录日志并返回 false，栈上留下 nresults 个结果（失败时为 nil）
bool PCall(lua_State* L, int nargs, int nresults);

// 调用全局函数 name(args...)，参数由 pushArgs 回调压栈
bool CallGlobal(const char* name);
bool CallGlobal(const char* name, int i1);
bool CallGlobal(const char* name, int i1, const std::string& s2);
bool CallGlobal(const char* name, const std::string& s1);
bool CallGlobalBool(const char* name, bool b);

// 把 toluafix 的函数引用压栈；成功返回 true
bool PushHandler(lua_State* L, int handler);
// 从栈顶取 Lua 函数并登记为 refid（使用 toluafix 的映射表，和 cocos2d-x 自带绑定共用）
int RefFunction(lua_State* L, int index);
void UnrefFunction(lua_State* L, int handler);

// 以正确的 tolua 类型名压入 CCObject（例如 CCControlButton、CCLabelTTF）；obj 为空时压 nil
void PushObject(lua_State* L, cocos2d::CCObject* obj, const char* fallbackType = "CCObject");
// 推断 CCObject 的 tolua 类型名
const char* ObjectTypeName(lua_State* L, cocos2d::CCObject* obj, const char* fallbackType);

}  // namespace host
