#import "RootViewController.h"
#import "EAGLView.h"
#include "cocos2d.h"

@implementation RootViewController

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    if (self.view.subviews.count == 0) return;
    UIView* gl = [self.view.subviews objectAtIndex:0];
    CGRect full = self.view.bounds;
    if (!CGRectEqualToRect(gl.frame, full)) {
        gl.frame = full;
    }
    if (cocos2d::CCDirector::sharedDirector()->getOpenGLView() != nullptr) {
        float w = [(EAGLView*)gl getWidth];
        float h = [(EAGLView*)gl getHeight];
        if (w > 1.0f && h > 1.0f) {
            cocos2d::CCEGLView* view = cocos2d::CCEGLView::sharedOpenGLView();
            view->setFrameSize(w, h);
            view->setDesignResolutionSize(640, 960, kResolutionShowAll);
        }
    }
    [self placeWoodInBlackBars:gl];
}

- (void)placeWoodInBlackBars:(UIView*)gl {
    NSString* path = [[[NSBundle mainBundle] bundlePath] stringByAppendingPathComponent:@"images/Main/edge_bottom.png"];
    UIImage* img = [UIImage imageWithContentsOfFile:path];
    if (img == nil || img.size.width < 1) return;
    CGFloat screenW = self.view.bounds.size.width;
    CGFloat screenH = self.view.bounds.size.height;
    CGFloat fit = MIN(screenW / 640.0, screenH / 960.0);
    CGFloat gameH = 960.0 * fit;
    CGFloat gameW = 640.0 * fit;
    CGFloat bar = (screenH - gameH) / 2.0;
    CGFloat gameX = (screenW - gameW) / 2.0;
    CGFloat woodH = img.size.height * (gameW / img.size.width);
    if (woodH > bar) woodH = bar;
    UIImageView* top = (UIImageView*)[self.view viewWithTag:9101];
    UIImageView* bottom = (UIImageView*)[self.view viewWithTag:9102];
    if (top == nil) {
        top = [[[UIImageView alloc] initWithImage:img] autorelease];
        top.tag = 9101;
        top.contentMode = UIViewContentModeScaleToFill;
        [self.view addSubview:top];
    }
    if (bottom == nil) {
        bottom = [[[UIImageView alloc] initWithImage:img] autorelease];
        bottom.tag = 9102;
        bottom.contentMode = UIViewContentModeScaleToFill;
        [self.view addSubview:bottom];
    }
    if (woodH < 1.0 || bar < 1.0) {
        top.hidden = YES;
        bottom.hidden = YES;
        return;
    }
    top.hidden = NO;
    bottom.hidden = NO;
    top.frame = CGRectMake(gameX, bar - woodH, gameW, woodH);
    bottom.frame = CGRectMake(gameX, screenH - bar, gameW, woodH);
}

// 游戏是竖屏（设计分辨率 640x960）
- (UIInterfaceOrientationMask)supportedInterfaceOrientations {
    return UIInterfaceOrientationMaskPortrait;
}

- (UIInterfaceOrientation)preferredInterfaceOrientationForPresentation {
    return UIInterfaceOrientationPortrait;
}

- (BOOL)shouldAutorotate {
    return YES;
}

- (BOOL)prefersStatusBarHidden {
    return YES;
}

// 全面屏：隐藏底部横条，边缘手势需要划两次，避免误触退出
- (BOOL)prefersHomeIndicatorAutoHidden {
    return YES;
}

- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures {
    return UIRectEdgeBottom;
}

@end
