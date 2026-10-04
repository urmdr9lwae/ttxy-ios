#import "RootViewController.h"
#import "EAGLView.h"
#include "cocos2d.h"

@implementation RootViewController

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    if (self.view.subviews.count == 0) return;
    UIView* gl = [self.view.subviews objectAtIndex:0];
    UIEdgeInsets inset = UIEdgeInsetsZero;
    if (@available(iOS 11.0, *)) {
        inset = self.view.safeAreaInsets;
    }
    CGRect safe = UIEdgeInsetsInsetRect(self.view.bounds, inset);
    if (CGRectEqualToRect(gl.frame, safe)) return;
    gl.frame = safe;
    if (cocos2d::CCDirector::sharedDirector()->getOpenGLView() == nullptr) return;
    float w = [(EAGLView*)gl getWidth];
    float h = [(EAGLView*)gl getHeight];
    if (w < 1.0f || h < 1.0f) return;
    cocos2d::CCEGLView* view = cocos2d::CCEGLView::sharedOpenGLView();
    view->setFrameSize(w, h);
    view->setDesignResolutionSize(640, 960, kResolutionShowAll);
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
