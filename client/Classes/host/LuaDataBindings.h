// 资源层的 Lua 绑定：CTwFilePack / KFDB 相关全局函数
#pragma once

struct lua_State;

namespace host {

// 注册：
//   CTwFilePackOpen(path) -> string（找不到返回 ""），以及 CTwFilePack 表
//   GetFdbInfoFinder()、KFDBInfoFinderImpl.FILE_STRUCT()、T_UINT/T_INT/T_DOUBLE/T_INT64/T_LPCSTR
//   KFDBGetRecord(type, key)、KFDBGetRecordAmt(type)、KFDBGetRecordByIdx(type, i)
void RegisterDataBindings(lua_State* L);

}  // namespace host
