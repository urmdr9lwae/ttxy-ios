// 平台相关信息与功能（Windows / iOS 各自实现）
#pragma once
#include <string>

namespace host {

// 可写目录（存档、下载、日志），以 '/' 结尾
std::string PlatformDocPath();
// 资源根目录（Resources 的绝对路径），以 '/' 结尾
std::string PlatformResPath();
// 设备型号，例如 "Windows" / "iPhone17,1"
std::string PlatformDeviceName();
// 系统版本
std::string PlatformOSVersion();
// 用系统浏览器打开网址
void PlatformOpenURL(const std::string& url);
// 保持屏幕常亮
void PlatformKeepScreenOn(bool on);
// 字体配置项（ini/font.ini 里的键）
const char* PlatformFontKey();

}  // namespace host
