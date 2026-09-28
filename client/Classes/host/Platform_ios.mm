// iOS 平台实现（对应 Platform_win32.cpp）
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#include <sys/utsname.h>

#include "Platform.h"

namespace host {

static std::string WithSlash(NSString* s) {
    std::string r = s ? [s UTF8String] : "";
    if (!r.empty() && r.back() != '/') r.push_back('/');
    return r;
}

std::string PlatformResPath() { return WithSlash([[NSBundle mainBundle] resourcePath]); }

std::string PlatformDocPath() {
    // Documents：存档、热更新下载、日志（可在“文件”App 里看到，便于排查）
    NSArray* dirs = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    NSString* doc = [dirs firstObject];
    [[NSFileManager defaultManager] createDirectoryAtPath:doc withIntermediateDirectories:YES attributes:nil error:nil];
    return WithSlash(doc);
}

std::string PlatformDeviceName() {
    struct utsname u;
    uname(&u);
    return u.machine;  // 例如 iPhone17,1
}

std::string PlatformOSVersion() { return [[[UIDevice currentDevice] systemVersion] UTF8String]; }

void PlatformOpenURL(const std::string& url) {
    NSURL* u = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
    if (!u) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        [[UIApplication sharedApplication] openURL:u options:@{} completionHandler:nil];
    });
}

void PlatformKeepScreenOn(bool on) {
    dispatch_async(dispatch_get_main_queue(), ^{
        [UIApplication sharedApplication].idleTimerDisabled = on ? YES : NO;
    });
}

// ini/font.ini：ios=YaHei|20（字体通过 Info.plist 的 UIAppFonts 注册，按 PostScript 名 “YaHei” 引用）
const char* PlatformFontKey() { return "ios"; }

}  // namespace host
