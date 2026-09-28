// 替换引擎的 platform/ios/CCCommon.mm：原文件用的 UIAlertView（iOS 9 起废弃），这里改成 UIAlertController
#import <UIKit/UIKit.h>

#include <stdarg.h>
#include <stdio.h>

#include "platform/CCCommon.h"

NS_CC_BEGIN

void CCLog(const char* pszFormat, ...) {
    char szBuf[kMaxLogLen + 1] = {0};
    va_list ap;
    va_start(ap, pszFormat);
    vsnprintf(szBuf, kMaxLogLen, pszFormat, ap);
    va_end(ap);
    printf("Cocos2d: %s\n", szBuf);
}

void CCMessageBox(const char* pszMsg, const char* pszTitle) {
    NSString* title = pszTitle ? [NSString stringWithUTF8String:pszTitle] : nil;
    NSString* msg = pszMsg ? [NSString stringWithUTF8String:pszMsg] : nil;
    dispatch_async(dispatch_get_main_queue(), ^{
        UIAlertController* ac = [UIAlertController alertControllerWithTitle:title
                                                                     message:msg
                                                              preferredStyle:UIAlertControllerStyleAlert];
        [ac addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
        UIViewController* root = [UIApplication sharedApplication].delegate.window.rootViewController;
        [root presentViewController:ac animated:YES completion:nil];
    });
}

void CCLuaLog(const char* pszFormat) { puts(pszFormat); }

NS_CC_END
