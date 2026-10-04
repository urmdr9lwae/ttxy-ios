// iOS 入口：创建窗口和 EAGLView，启动 cocos2d-x（基于引擎自带 multi-platform-lua 模板，去掉了已废弃的接口）
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>

#import "AppController.h"
#import "RootViewController.h"
#import "EAGLView.h"

#include "cocos2d.h"
#include "AppDelegate.h"

@implementation AppController

@synthesize window;

// cocos2d 应用实例（CCApplication 是单例，构造时登记自己）
static AppDelegate s_sharedApplication;

- (BOOL)application:(UIApplication*)application didFinishLaunchingWithOptions:(NSDictionary*)launchOptions {
    // 声音：跟随静音键，不打断其它 App 的音乐
    [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryAmbient error:nil];
    [[AVAudioSession sharedInstance] setActive:YES error:nil];

    // 国行 iPhone 第一次联网要弹“允许使用无线数据”授权框；游戏走的是 BSD socket，不一定能触发，
    // 这里先用 NSURLSession 发一个无关紧要的请求（苹果自己的联网检测地址）把授权框弹出来
    NSURL* probe = [NSURL URLWithString:@"http://captive.apple.com/hotspot-detect.html"];
    [[[NSURLSession sharedSession] dataTaskWithURL:probe
                                 completionHandler:^(NSData*, NSURLResponse*, NSError*) {}] resume];

    self.window = [[[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]] autorelease];

    // 深度缓冲给 ccbi 里的 3D 翻转动画（CCOrbitCamera）用
    EAGLView* glView = [EAGLView viewWithFrame:[self.window bounds]
                                   pixelFormat:kEAGLColorFormatRGBA8
                                   depthFormat:GL_DEPTH_COMPONENT16
                            preserveBackbuffer:NO
                                    sharegroup:nil
                                 multiSampling:NO
                               numberOfSamples:0];
    [glView setMultipleTouchEnabled:NO];

    viewController = [[RootViewController alloc] initWithNibName:nil bundle:nil];
    // 控制器铺满屏幕，游戏画面放在安全区内，按钮不会被刘海和底部横条挡住。
    UIView* container = [[[UIView alloc] initWithFrame:[self.window bounds]] autorelease];
    container.backgroundColor = [UIColor blackColor];
    container.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    glView.autoresizingMask = UIViewAutoresizingNone;
    viewController.view = container;
    [container addSubview:glView];
    [self.window setRootViewController:viewController];
    [self.window makeKeyAndVisible];

    cocos2d::CCApplication::sharedApplication()->run();
    return YES;
}

- (void)applicationWillResignActive:(UIApplication*)application {
    cocos2d::CCDirector::sharedDirector()->pause();
}

- (void)applicationDidBecomeActive:(UIApplication*)application {
    [[AVAudioSession sharedInstance] setActive:YES error:nil];
    cocos2d::CCDirector::sharedDirector()->resume();
}

- (void)applicationDidEnterBackground:(UIApplication*)application {
    cocos2d::CCApplication::sharedApplication()->applicationDidEnterBackground();
}

- (void)applicationWillEnterForeground:(UIApplication*)application {
    cocos2d::CCApplication::sharedApplication()->applicationWillEnterForeground();
}

- (void)applicationWillTerminate:(UIApplication*)application {
}

- (void)applicationDidReceiveMemoryWarning:(UIApplication*)application {
    cocos2d::CCDirector::sharedDirector()->purgeCachedData();
}

- (void)dealloc {
    [viewController release];
    [window release];
    [super dealloc];
}

@end
