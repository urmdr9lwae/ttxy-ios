#include "FrameMap.h"

#include <set>
#include <unordered_map>

#include "cocos2d.h"
#include "HostFile.h"

USING_NS_CC;

namespace host {
namespace {

std::unordered_map<std::string, std::string> g_frameToPlist;
std::set<std::string> g_loadedPlists;

}  // namespace

void LoadFrameMap(const std::string& content) {
    g_frameToPlist.clear();
    size_t start = 0;
    while (start < content.size()) {
        size_t end = content.find('\n', start);
        if (end == std::string::npos) end = content.size();
        std::string line = content.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t tab = line.find('\t');
        if (tab != std::string::npos) g_frameToPlist[line.substr(0, tab)] = line.substr(tab + 1);
        start = end + 1;
    }
}

std::string PlistForFrame(const std::string& frameName) {
    auto it = g_frameToPlist.find(NormalizePath(frameName));
    return it == g_frameToPlist.end() ? std::string() : it->second;
}

CCSpriteFrame* ResolveFrame(const char* frameName) {
    if (!frameName || !*frameName) return nullptr;
    const std::string name = NormalizePath(frameName);
    CCSpriteFrameCache* cache = CCSpriteFrameCache::sharedSpriteFrameCache();
    if (CCSpriteFrame* f = cache->spriteFrameByName(name.c_str())) return f;
    auto it = g_frameToPlist.find(name);
    if (it == g_frameToPlist.end()) return nullptr;
    // 帧缓存可能被清理过（purgeCachedData），所以每次找不到都重新加载一次 plist
    cache->addSpriteFramesWithFile(it->second.c_str());
    g_loadedPlists.insert(it->second);
    return cache->spriteFrameByName(name.c_str());
}

static CCSpriteFrame* ResolverHook(const char* file) {
    // 先按 framemap 取图集帧。isFileExist 会把 PFDW 里的名字也算成文件，
    // 黄条 progress_ft.png 会被跳过图集，CCControlSlider 拿不到贴图。
    // 没有图集记录时返回空，CCSprite::initWithFile 再走散文件 addImage。
    return ResolveFrame(file);
}

void InstallFrameResolver() { CCSpriteFrameCache::setFileFrameResolver(ResolverHook); }

}  // namespace host
