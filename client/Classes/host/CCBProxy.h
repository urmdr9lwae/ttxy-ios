// CocosBuilder 的 Lua 代理类，对应原版 LuaCCBReaderProxy.cpp：
//   CCNodeLoaderLibraryProxy：遇到未注册的自定义类名时回调 Lua（library, className），由 Lua 注册加载器
//   CCBLayerLoaderProxy：    创建 CCBLayerProxy 后回调 Lua（layer, reader）
//   CCBLayerProxy：          ccbi 根节点；选择器解析、成员变量赋值、按钮/菜单回调都转发给 Lua 钩子
//   CCBAnimationManagerDelegateProxy：动画序列播放完成时回调 Lua
#pragma once
#include "cocos2d.h"
#include "cocos-ext.h"

struct lua_State;

namespace host {

class HookHolder {
public:
    ~HookHolder();
    void Hook(int handler);
    void Unhook();
    int Handler() const { return handler_; }

private:
    int handler_ = 0;
};

class CCBLayerProxy : public cocos2d::CCLayer,
                      public cocos2d::extension::CCBSelectorResolver,
                      public cocos2d::extension::CCBMemberVariableAssigner,
                      public cocos2d::extension::CCNodeLoaderListener {
public:
    static const int kSlots = 64;
    CREATE_FUNC(CCBLayerProxy);
    ~CCBLayerProxy();

    void hook(int handler) { hook_.Hook(handler); }
    void unhook() { hook_.Unhook(); }

    cocos2d::SEL_MenuHandler onResolveCCBCCMenuItemSelector(cocos2d::CCObject* target, const char* name) override;
    cocos2d::extension::SEL_CCControlHandler onResolveCCBCCControlSelector(cocos2d::CCObject* target,
                                                                           const char* name) override;
    bool onAssignCCBMemberVariable(cocos2d::CCObject* target, const char* name, cocos2d::CCNode* node) override;
    void onNodeLoaded(cocos2d::CCNode* node, cocos2d::extension::CCNodeLoader* loader) override;

    void OnMenuSlot(int slot, cocos2d::CCObject* sender);
    void OnControlSlot(int slot, cocos2d::CCObject* sender, unsigned int event);

#define HOST_DECL_SLOT(n) \
    void menuItemCallback##n(cocos2d::CCObject* s) { OnMenuSlot(n, s); } \
    void controlCallback##n(cocos2d::CCObject* s, unsigned int e) { OnControlSlot(n, s, e); }
    HOST_DECL_SLOT(0) HOST_DECL_SLOT(1) HOST_DECL_SLOT(2) HOST_DECL_SLOT(3) HOST_DECL_SLOT(4) HOST_DECL_SLOT(5)
    HOST_DECL_SLOT(6) HOST_DECL_SLOT(7) HOST_DECL_SLOT(8) HOST_DECL_SLOT(9) HOST_DECL_SLOT(10) HOST_DECL_SLOT(11)
    HOST_DECL_SLOT(12) HOST_DECL_SLOT(13) HOST_DECL_SLOT(14) HOST_DECL_SLOT(15) HOST_DECL_SLOT(16) HOST_DECL_SLOT(17)
    HOST_DECL_SLOT(18) HOST_DECL_SLOT(19) HOST_DECL_SLOT(20) HOST_DECL_SLOT(21) HOST_DECL_SLOT(22) HOST_DECL_SLOT(23)
    HOST_DECL_SLOT(24) HOST_DECL_SLOT(25) HOST_DECL_SLOT(26) HOST_DECL_SLOT(27) HOST_DECL_SLOT(28) HOST_DECL_SLOT(29)
    HOST_DECL_SLOT(30) HOST_DECL_SLOT(31) HOST_DECL_SLOT(32) HOST_DECL_SLOT(33) HOST_DECL_SLOT(34) HOST_DECL_SLOT(35)
    HOST_DECL_SLOT(36) HOST_DECL_SLOT(37) HOST_DECL_SLOT(38) HOST_DECL_SLOT(39) HOST_DECL_SLOT(40) HOST_DECL_SLOT(41)
    HOST_DECL_SLOT(42) HOST_DECL_SLOT(43) HOST_DECL_SLOT(44) HOST_DECL_SLOT(45) HOST_DECL_SLOT(46) HOST_DECL_SLOT(47)
    HOST_DECL_SLOT(48) HOST_DECL_SLOT(49) HOST_DECL_SLOT(50) HOST_DECL_SLOT(51) HOST_DECL_SLOT(52) HOST_DECL_SLOT(53)
    HOST_DECL_SLOT(54) HOST_DECL_SLOT(55) HOST_DECL_SLOT(56) HOST_DECL_SLOT(57) HOST_DECL_SLOT(58) HOST_DECL_SLOT(59)
    HOST_DECL_SLOT(60) HOST_DECL_SLOT(61) HOST_DECL_SLOT(62) HOST_DECL_SLOT(63)
#undef HOST_DECL_SLOT

private:
    HookHolder hook_;
    int menuSlots_ = 0;
    int controlSlots_ = 0;
};

class CCBLayerLoaderProxy : public cocos2d::extension::CCLayerLoader {
public:
    static CCBLayerLoaderProxy* loader();
    void hook(int handler) { hook_.Hook(handler); }
    void unhook() { hook_.Unhook(); }

protected:
    cocos2d::CCLayer* createCCNode(cocos2d::CCNode* parent, cocos2d::extension::CCBReader* reader) override;

private:
    HookHolder hook_;
};

class CCNodeLoaderLibraryProxy : public cocos2d::extension::CCNodeLoaderLibrary {
public:
    static CCNodeLoaderLibraryProxy* newDefaultCCNodeLoaderLibrary();
    void hook(int handler) { hook_.Hook(handler); }
    void unhook() { hook_.Unhook(); }
    cocos2d::extension::CCNodeLoader* getCCNodeLoader(const char* className) override;

private:
    HookHolder hook_;
    bool inHook_ = false;
};

// 继承 CCObject：CCBAnimationManager::setDelegate 会 retain 它，生命周期跟随动画管理器
class CCBAnimationManagerDelegateProxy : public cocos2d::CCObject,
                                         public cocos2d::extension::CCBAnimationManagerDelegate {
public:
    ~CCBAnimationManagerDelegateProxy() { hook_.Unhook(); }
    void hook(int handler) { hook_.Hook(handler); }
    void unhook() { hook_.Unhook(); }
    void completedAnimationSequenceNamed(const char* name) override;

private:
    HookHolder hook_;
};

void RegisterCCBProxy(lua_State* L);

}  // namespace host
