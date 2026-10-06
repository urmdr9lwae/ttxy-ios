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
    [self placeOriginalBoards];
}

- (UIImage*)boardImageFlipped:(BOOL)flipped {
    NSString* path = [[[NSBundle mainBundle] bundlePath] stringByAppendingPathComponent:@"cover_h.png"];
    UIImage* image = [UIImage imageWithContentsOfFile:path];
    if (image == nil || !flipped) return image;
    UIGraphicsBeginImageContextWithOptions(image.size, NO, image.scale);
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGContextTranslateCTM(ctx, 0, image.size.height);
    CGContextScaleCTM(ctx, 1.0, -1.0);
    [image drawInRect:CGRectMake(0, 0, image.size.width, image.size.height)];
    UIImage* turned = UIGraphicsGetImageFromCurrentImageContext();
    UIGraphicsEndImageContext();
    return turned;
}

- (void)placeOriginalBoards {
    UIImage* board = [self boardImageFlipped:NO];
    if (board == nil || board.size.width < 1.0) return;
    CGFloat screenW = self.view.bounds.size.width;
    CGFloat screenH = self.view.bounds.size.height;
    CGFloat fit = MIN(screenW / 640.0, screenH / 960.0);
    CGFloat gameW = 640.0 * fit;
    CGFloat gameH = 960.0 * fit;
    CGFloat gameX = (screenW - gameW) / 2.0;
    CGFloat gameY = (screenH - gameH) / 2.0;
    CGFloat boardH = gameW * (board.size.height / board.size.width);
    if (boardH > gameY) boardH = gameY;
    UIImageView* top = (UIImageView*)[self.view viewWithTag:9101];
    UIImageView* bottom = (UIImageView*)[self.view viewWithTag:9102];
    if (top == nil) {
        top = [[[UIImageView alloc] initWithImage:[self boardImageFlipped:YES]] autorelease];
        top.tag = 9101;
        top.contentMode = UIViewContentModeScaleToFill;
        [self.view addSubview:top];
    }
    if (bottom == nil) {
        bottom = [[[UIImageView alloc] initWithImage:board] autorelease];
        bottom.tag = 9102;
        bottom.contentMode = UIViewContentModeScaleToFill;
        [self.view addSubview:bottom];
    }
    top.frame = CGRectMake(gameX, gameY - boardH, gameW, boardH);
    bottom.frame = CGRectMake(gameX, gameY + gameH, gameW, boardH);
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
