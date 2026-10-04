#ifdef ANDROID
#include "Platform.h"

#include "cocos2d.h"

#include <sys/system_properties.h>

namespace host {

static std::string WithSlash(std::string s) {
    if (!s.empty() && s.back() != '/') s.push_back('/');
    return s;
}

std::string PlatformResPath() { return "assets/"; }

std::string PlatformDocPath() {
    return WithSlash(cocos2d::CCFileUtils::sharedFileUtils()->getWritablePath());
}

std::string PlatformLogDir() { return PlatformDocPath(); }

static std::string Prop(const char* key) {
    char buf[PROP_VALUE_MAX] = {0};
    __system_property_get(key, buf);
    return buf;
}

std::string PlatformDeviceName() {
    std::string brand = Prop("ro.product.brand");
    std::string model = Prop("ro.product.model");
    if (brand.empty()) return model.empty() ? "Android" : model;
    if (model.empty()) return brand;
    return brand + " " + model;
}

std::string PlatformOSVersion() {
    std::string v = Prop("ro.build.version.release");
    return v.empty() ? "Android" : v;
}

void PlatformOpenURL(const std::string&) {}

void PlatformKeepScreenOn(bool) {}

const char* PlatformFontKey() { return "android"; }

}  // namespace host
#endif
