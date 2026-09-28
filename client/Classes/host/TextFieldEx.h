// 原版引擎给 CCTextFieldTTF 加的 setMaxLens / getMaxLens / setPasswordMode（Lua 的 Edit 控件在用）
#pragma once

struct lua_State;

namespace host {

void RegisterTextFieldEx(lua_State* L);

}  // namespace host
