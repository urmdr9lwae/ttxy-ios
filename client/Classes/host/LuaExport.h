// 原版 tolua_LuaExport_open / TwLuaHelperMisc::Setup 导出给 Lua 的原生接口的重写版
#pragma once
#include <string>

struct lua_State;

namespace host {

struct EnvInfo {
    std::string resPath;       // 资源根目录，以 '/' 结尾
    std::string docPath;       // 可写目录，以 '/' 结尾
    std::string patchPath;     // 补丁目录
    std::string operatorPath;  // 运营配置目录（相对资源根，如 "sdk/mi/"）
    std::string version;       // 程序包版本号（整数字符串，和服务器 version.xml 的 package 比较）
    std::string versionName;   // 显示用版本号
    std::string deviceName;
    std::string packageName;
    int screenWidth = 640;
    int screenHeight = 960;
};

void RegisterExportBindings(lua_State* L, const EnvInfo& env);

// 保存系统变量（退出时调用）
void SaveSystemVariables();

// 每帧调用：处理热更新完成后的“重新加载”（CEnvRoot:SetReloadAll）
void LuaExportTick(lua_State* L);

}  // namespace host
