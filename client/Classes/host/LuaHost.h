// 游戏脚本宿主：对应原版 CTwLua（Init/Setup/Startup/Process）
#pragma once
#include "LuaExport.h"

namespace host {

// 初始化 Lua（第三方库、原生接口、脚本加载器），require HostAdapter 并调用 OnSysStartup
bool LuaHostStart(const EnvInfo& env);
// 每帧：分发网络事件，调用 OnProcess
void LuaHostTick();
void LuaHostShutdown();

// 调试：每帧递增的心跳（看门狗线程用来判断主线程是否卡住）
long LuaHostHeartbeat();
// 调试：可在其它线程调用；下一条 Lua 指令执行时把 Lua 调用栈写进 client.log
void LuaHostRequestTraceback();

}  // namespace host
