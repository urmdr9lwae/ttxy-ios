// MD5（结果为小写十六进制，与原版 CMd5::GetResult 一致）
#pragma once
#include <string>

namespace host {
std::string Md5Hex(const std::string& data);
}
