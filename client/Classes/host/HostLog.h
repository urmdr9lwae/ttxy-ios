// 原生层日志：同时输出到调试器/控制台，并追加写入日志文件（真机上排查问题用）
#pragma once
#include <string>

namespace host {

// 设置日志文件路径（为空则只输出到控制台）
void SetLogFile(const std::string& path);
void Log(const char* fmt, ...);

}  // namespace host
