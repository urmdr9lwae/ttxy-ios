#include "LuaDataBindings.h"

#include <cstdlib>
#include <string>

extern "C" {
#include "lauxlib.h"
#include "lua.h"
}

#include "HostFile.h"
#include "HostLog.h"
#include "KFDB.h"

namespace host {
namespace {

// ---------------- CTwFilePack ----------------

// CTwFilePackOpen(path) -> 文件内容字符串；找不到返回 ""
int l_FilePackOpen(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    std::string data;
    if (ReadResource(path, data))
        lua_pushlstring(L, data.data(), data.size());
    else
        lua_pushliteral(L, "");
    return 1;
}

// ---------------- 表结构注册（EnvConfig.lua 的 DEF 调用） ----------------
// FILE_STRUCT() 返回一个 Lua 表，AddField 把字段依次追加进去：{ {type=, name=, idx=, desc=}, ... }

int l_FileStruct(lua_State* L) {
    lua_newtable(L);
    return 1;
}

// g_fdbInfo:AddField(info, type, name, isIdx)
int l_AddField(lua_State* L) {
    luaL_checktype(L, 2, LUA_TTABLE);
    const int type = static_cast<int>(luaL_checkinteger(L, 3));
    const char* name = luaL_checkstring(L, 4);
    const int isIdx = lua_toboolean(L, 5);
    const int n = static_cast<int>(lua_objlen(L, 2));
    lua_createtable(L, 0, 4);
    lua_pushinteger(L, type); lua_setfield(L, -2, "type");
    lua_pushstring(L, name); lua_setfield(L, -2, "name");
    lua_pushboolean(L, isIdx); lua_setfield(L, -2, "idx");
    lua_rawseti(L, 2, n + 1);
    return 0;
}

// g_fdbInfo:SetFieldDesc(info, desc) —— 给最后添加的字段写说明
int l_SetFieldDesc(lua_State* L) {
    luaL_checktype(L, 2, LUA_TTABLE);
    const int n = static_cast<int>(lua_objlen(L, 2));
    if (n == 0) return 0;
    lua_rawgeti(L, 2, n);
    lua_pushvalue(L, 3);
    lua_setfield(L, -2, "desc");
    lua_pop(L, 1);
    return 0;
}

// g_fdbInfo:AddStruct(typeName, info)
int l_AddStruct(lua_State* L) {
    const char* typeName = luaL_checkstring(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    StructDef def;
    const int n = static_cast<int>(lua_objlen(L, 3));
    for (int i = 1; i <= n; ++i) {
        lua_rawgeti(L, 3, i);
        FieldDef f;
        lua_getfield(L, -1, "type"); f.type = static_cast<int>(lua_tointeger(L, -1)); lua_pop(L, 1);
        lua_getfield(L, -1, "name"); f.name = lua_isstring(L, -1) ? lua_tostring(L, -1) : ""; lua_pop(L, 1);
        lua_getfield(L, -1, "idx"); f.isIndex = lua_toboolean(L, -1) != 0; lua_pop(L, 1);
        lua_getfield(L, -1, "desc"); f.desc = lua_isstring(L, -1) ? lua_tostring(L, -1) : ""; lua_pop(L, 1);
        lua_pop(L, 1);
        def.fields.push_back(f);
    }
    Database::Instance().AddStruct(typeName, def);
    return 0;
}

// g_fdbInfo:AddFile(file, desc, typeName)
int l_AddFile(lua_State* L) {
    const char* file = luaL_checkstring(L, 2);
    const char* desc = luaL_optstring(L, 3, "");
    const char* type = luaL_checkstring(L, 4);
    Database::Instance().AddFile(file, desc, type);
    return 0;
}

int l_GetFdbInfoFinder(lua_State* L) {
    // 单例：第一次调用时创建并存到注册表里
    lua_getfield(L, LUA_REGISTRYINDEX, "host.fdbInfoFinder");
    if (!lua_isnil(L, -1)) return 1;
    lua_pop(L, 1);
    lua_createtable(L, 0, 4);
    lua_pushcfunction(L, l_AddField); lua_setfield(L, -2, "AddField");
    lua_pushcfunction(L, l_SetFieldDesc); lua_setfield(L, -2, "SetFieldDesc");
    lua_pushcfunction(L, l_AddStruct); lua_setfield(L, -2, "AddStruct");
    lua_pushcfunction(L, l_AddFile); lua_setfield(L, -2, "AddFile");
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, "host.fdbInfoFinder");
    return 1;
}

// ---------------- 取记录 ----------------

// 把一行压成 Lua 表（每次都新建，Lua 代码会直接修改返回的记录）
void PushRow(lua_State* L, const Table& t, size_t row) {
    const auto& cols = t.Columns();
    const auto& cells = t.Row(row);
    lua_createtable(L, 0, static_cast<int>(cols.size()));
    for (size_t c = 0; c < cols.size(); ++c) {
        const Cell& cell = cells[c];
        if (cell.isString)
            lua_pushlstring(L, cell.text.data(), cell.text.size());
        else
            lua_pushnumber(L, cell.number);
        lua_setfield(L, -2, cols[c].c_str());
    }
}

// KFDBGetRecord(typeName, key) -> 记录表或 nil；key 可以是数字或字符串
int l_GetRecord(lua_State* L) {
    const char* type = luaL_checkstring(L, 1);
    const Table* t = Database::Instance().Get(type);
    if (!t) return 0;
    std::string key;
    if (lua_type(L, 2) == LUA_TNUMBER)
        key = Table::KeyOf(lua_tonumber(L, 2));
    else if (lua_type(L, 2) == LUA_TSTRING)
        key = lua_tostring(L, 2);
    else
        return 0;
    long row = t->Find(key);
    if (row < 0 && lua_type(L, 2) == LUA_TSTRING) {
        // 字符串形式的数字主键（例如 "11"）再按数字试一次
        char* end = nullptr;
        const double d = std::strtod(key.c_str(), &end);
        if (end && *end == '\0' && end != key.c_str()) row = t->Find(Table::KeyOf(d));
    }
    if (row < 0) return 0;
    PushRow(L, *t, static_cast<size_t>(row));
    return 1;
}

// KFDBGetRecordAmt(typeName) -> 行数（表不存在时为 0）
int l_GetRecordAmt(lua_State* L) {
    const Table* t = Database::Instance().Get(luaL_checkstring(L, 1));
    lua_pushinteger(L, t ? static_cast<lua_Integer>(t->RowCount()) : 0);
    return 1;
}

// KFDBGetRecordByIdx(typeName, i) -> 第 i 行（从 1 开始，按文件里的顺序）
int l_GetRecordByIdx(lua_State* L) {
    const Table* t = Database::Instance().Get(luaL_checkstring(L, 1));
    const lua_Integer i = luaL_checkinteger(L, 2);
    if (!t || i < 1 || static_cast<size_t>(i) > t->RowCount()) return 0;
    PushRow(L, *t, static_cast<size_t>(i - 1));
    return 1;
}

void SetGlobalInt(lua_State* L, const char* name, int v) {
    lua_pushinteger(L, v);
    lua_setglobal(L, name);
}

}  // namespace

void RegisterDataBindings(lua_State* L) {
    lua_register(L, "CTwFilePackOpen", l_FilePackOpen);
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, l_FilePackOpen);
    lua_setfield(L, -2, "Open");
    lua_setglobal(L, "CTwFilePack");

    lua_register(L, "GetFdbInfoFinder", l_GetFdbInfoFinder);
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, l_FileStruct);
    lua_setfield(L, -2, "FILE_STRUCT");
    lua_setglobal(L, "KFDBInfoFinderImpl");
    SetGlobalInt(L, "T_UINT", T_UINT);
    SetGlobalInt(L, "T_INT", T_INT);
    SetGlobalInt(L, "T_DOUBLE", T_DOUBLE);
    SetGlobalInt(L, "T_INT64", T_INT64);
    SetGlobalInt(L, "T_LPCSTR", T_LPCSTR);

    lua_register(L, "KFDBGetRecord", l_GetRecord);
    lua_register(L, "KFDBGetRecordAmt", l_GetRecordAmt);
    lua_register(L, "KFDBGetRecordByIdx", l_GetRecordByIdx);
}

}  // namespace host
