#pragma once
#include <cstdint>
#include <string>

namespace host {

// 原版资源包文件名哈希。路径先统一斜杠、去掉开头的 /，再把 A-Z 转成小写。
uint32_t PfdwFileId(const std::string& path);

// script/ui/main.dat -> script.dat。不属于这些包的路径返回空。
const char* PfdwPackForPath(const std::string& path);

// 资源包所在目录，例如 iOS 的 .app 根目录。里面有 script.dat、images.dat 这些 PFDW。
void PfdwSetRoot(const std::string& root);
bool PfdwContains(const std::string& path);
// 成功时 out 是包内原始字节。散文件优先的判断由调用方做。
bool PfdwRead(const std::string& path, std::string& out);

}
