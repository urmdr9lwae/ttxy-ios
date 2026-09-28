// 资源文件访问：所有原生层读取资源都经过这里。
// 游戏里用 cocos2d 的 CCFileUtils 实现；单元测试里用普通文件实现。
#pragma once
#include <string>

namespace host {

// 读取资源（相对 Resources 根目录的路径，如 "db/BaseHero.sqldat"），成功返回 true
using FileReader = bool (*)(const std::string& relPath, std::string& out);

void SetFileReader(FileReader reader);
bool ReadResource(const std::string& relPath, std::string& out);

// 规范化资源路径：反斜杠转正斜杠、去掉开头的 "./" 和 "/"
std::string NormalizePath(const std::string& path);

// 大小写不敏感查找（iOS 文件系统区分大小写，而 Lua 里的写法不一定和导出文件名大小写一致）
// 需要先用 LoadFileList 载入导出时生成的 _filelist.txt
void LoadFileList(const std::string& content);
std::string ResolveCase(const std::string& relPath);

}  // namespace host
