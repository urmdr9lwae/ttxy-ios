// 图集帧索引：原版资源里大量图片只存在于图集中（帧名就是原始路径，如 "data/Activity/exchangeBg1.png"）。
// framemap.txt 每行 "帧名<TAB>plist 路径"，导出资源时生成。
#pragma once
#include <string>

namespace cocos2d { class CCSpriteFrame; }

namespace host {

void LoadFrameMap(const std::string& content);
// 帧所在 plist，找不到返回空串
std::string PlistForFrame(const std::string& frameName);
// 按帧名取精灵帧（必要时先加载 plist）；找不到返回 nullptr
cocos2d::CCSpriteFrame* ResolveFrame(const char* frameName);
// 安装到引擎：CCSprite / CCBReader / CCScale9Sprite 找不到图片文件时回退到图集帧
void InstallFrameResolver();

}  // namespace host
