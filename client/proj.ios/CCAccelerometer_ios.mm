// 替换引擎的 CCAccelerometer.mm + AccelerometerDelegateWrapper.mm：
// 那两个文件基于 iOS 5 起废弃的 UIAccelerometer；游戏不用重力感应，这里保留接口、不做任何事。
#include "cocos2d.h"
#include "platform/ios/CCAccelerometer.h"

NS_CC_BEGIN

CCAccelerometer::CCAccelerometer() {}

CCAccelerometer::~CCAccelerometer() {}

void CCAccelerometer::setDelegate(CCAccelerometerDelegate*) {}

void CCAccelerometer::setAccelerometerInterval(float) {}

NS_CC_END
