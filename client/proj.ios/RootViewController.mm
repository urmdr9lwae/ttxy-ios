#import "RootViewController.h"
#import "EAGLView.h"
#include "cocos2d.h"

@implementation RootViewController

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    if (self.view.subviews.count == 0) return;
    UIView* gl = [self.view.subviews objectAtIndex:0];
    // 960 画面按原比例放进整屏。缩进安全区会把最上的木梁和最下的木地板切掉。
    CGRect full = self.view.bounds;
    if (!CGRectEqualToRect(gl.frame, full)) {
        gl.frame = full;
    }
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
