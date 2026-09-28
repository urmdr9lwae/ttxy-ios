#include "AppDelegate.h"

#include "SimpleAudioEngine.h"
#include "host/FrameMap.h"
#include "host/HostFile.h"
#include "host/HostLog.h"
#include "host/LuaCall.h"
#include "host/LuaHost.h"
#include "host/Platform.h"

USING_NS_CC;
using namespace CocosDenshion;

namespace {

// 与原版一致的程序包版本（和服务器 version.xml 的 package 比较）
const char* kPackageVersion = "52";
const char* kVersionName = "1.0.6.2";
const char* kOperatorPath = "sdk/mi/";

// 资源读取：先判断存在，避免 CCFileUtils 对不存在的文件打印错误
bool ReadViaCocos(const std::string& rel, std::string& out) {
    CCFileUtils* fu = CCFileUtils::sharedFileUtils();
    const std::string full = fu->fullPathForFilename(rel.c_str());
    if (!fu->isFileExist(full)) return false;
    unsigned long size = 0;
    unsigned char* data = fu->getFileData(full.c_str(), "rb", &size);
    if (!data) return false;
    out.assign(reinterpret_cast<char*>(data), size);
    delete[] data;
    return true;
}

host::EnvInfo g_env;

// 资源名大小写纠正：脚本/ccbi 里的路径和实际文件名大小写可能不一致（Windows 不区分，iOS 包内区分）
std::string ResolveName(const std::string& name) {
    const std::string p = host::NormalizePath(name);
    const std::string real = host::ResolveCase(p);
    if (real == p) return name;
#if defined(_WIN32) && defined(_DEBUG)
    static int logged = 0;
    if (real != p && ++logged <= 200) host::Log("资源名大小写不一致：%s -> %s", name.c_str(), real.c_str());
#endif
    return real;
}

// 每帧驱动 Lua 的 OnProcess。
// Lua 启动推迟到第 2 帧：runWithScene 的场景要到第一帧绘制后才成为 runningScene，
// 而 Lua 一启动就会 replaceScene（cocos2d-x 要求此时已有正在运行的场景）。
class Ticker : public CCObject {
public:
    void update(float) {
        if (frames_ < 2) {
            if (++frames_ == 2 && !host::LuaHostStart(g_env)) host::Log("Lua 启动失败，详见上面的错误");
            return;
        }
        host::LuaHostTick();
#if defined(_WIN32) && defined(_DEBUG)
        // 调试：每 5 秒把画面存成 UserData/shot.png，便于在没有显示器输出的环境下检查界面
        if (++shotCounter_ % 300 == 0) SaveShot();
#endif
    }

    void SaveShot() {
        CCScene* scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene) return;
        const CCSize win = CCDirector::sharedDirector()->getWinSize();
        CCRenderTexture* rt = CCRenderTexture::create(static_cast<int>(win.width), static_cast<int>(win.height));
        rt->begin();
        scene->visit();
        rt->end();
        if (CCImage* img = rt->newCCImage(true)) {
            img->saveToFile((host::PlatformDocPath() + "shot.png").c_str(), false);
            delete img;
        }
    }

    int shotCounter_ = 0;

private:
    int frames_ = 0;
};
Ticker* g_ticker = nullptr;

}  // namespace

AppDelegate::AppDelegate() {}

AppDelegate::~AppDelegate() {
    host::LuaHostShutdown();
    SimpleAudioEngine::sharedEngine()->end();
}

bool AppDelegate::applicationDidFinishLaunching() {
    CCDirector* director = CCDirector::sharedDirector();
    CCEGLView* view = CCEGLView::sharedOpenGLView();
    director->setOpenGLView(view);
    director->setProjection(kCCDirectorProjection2D);
    CCFileUtils::sharedFileUtils()->setPopupNotify(false);
    // 原版设计分辨率 640x960（竖屏）；Lua（Tw.Controller:loadAsScene）按 min/max 缩放比例自行适配
    view->setDesignResolutionSize(640, 960, kResolutionNoBorder);
    director->setAnimationInterval(1.0 / 60);
    director->setDisplayStats(false);

    const std::string doc = host::PlatformDocPath();
    host::SetLogFile(host::PlatformLogDir() + "client.log");
    host::Log("==== 启动 ====");
    host::SetFileReader(ReadViaCocos);
    std::string list;
    if (host::ReadResource("_filelist.txt", list)) host::LoadFileList(list);
    CCFileUtils::setNameResolver(ResolveName);
    std::string frames;
    if (host::ReadResource("framemap.txt", frames)) host::LoadFrameMap(frames);
    host::InstallFrameResolver();

    // 先放一个空场景，Lua 会 replaceScene
    director->runWithScene(CCScene::create());

    host::EnvInfo& env = g_env;
    env.resPath = host::PlatformResPath();
    env.docPath = doc;
    env.patchPath = doc + "patch/";
    env.operatorPath = kOperatorPath;
    env.version = kPackageVersion;
    env.versionName = kVersionName;
    env.deviceName = host::PlatformDeviceName();
    env.packageName = "com.twmobile.ttaxy";
    const CCSize win = director->getWinSize();
    env.screenWidth = static_cast<int>(win.width);
    env.screenHeight = static_cast<int>(win.height);

    g_ticker = new Ticker();
    director->getScheduler()->scheduleUpdateForTarget(g_ticker, 0, false);
    return true;
}

void AppDelegate::applicationDidEnterBackground() {
    CCDirector::sharedDirector()->stopAnimation();
    SimpleAudioEngine::sharedEngine()->pauseBackgroundMusic();
    host::CallGlobal("OnEnterBackground");
}

void AppDelegate::applicationWillEnterForeground() {
    CCDirector::sharedDirector()->startAnimation();
    SimpleAudioEngine::sharedEngine()->resumeBackgroundMusic();
    host::CallGlobal("OnEnterForeground");
}
