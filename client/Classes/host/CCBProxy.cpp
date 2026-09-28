#include "CCBProxy.h"

#include "HostLog.h"
#include "LuaCall.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "tolua++.h"
#include "tolua_fix.h"
}
#include "CCLuaEngine.h"

USING_NS_CC;
USING_NS_CC_EXT;

namespace host {

// ---------------------------------------------------------------- HookHolder

HookHolder::~HookHolder() { Unhook(); }

void HookHolder::Hook(int handler) {
    if (handler_) {
        Log("重复 hook，替换旧回调");
        Unhook();
    }
    handler_ = handler;
}

void HookHolder::Unhook() {
    if (handler_ && GetLuaState()) UnrefFunction(GetLuaState(), handler_);
    handler_ = 0;
}

// ---------------------------------------------------------------- CCBLayerProxy

namespace {

typedef void (CCBLayerProxy::*MenuFn)(CCObject*);
typedef void (CCBLayerProxy::*ControlFn)(CCObject*, unsigned int);

#define HOST_M(n) &CCBLayerProxy::menuItemCallback##n
#define HOST_C(n) &CCBLayerProxy::controlCallback##n
const MenuFn kMenu[CCBLayerProxy::kSlots] = {
    HOST_M(0),  HOST_M(1),  HOST_M(2),  HOST_M(3),  HOST_M(4),  HOST_M(5),  HOST_M(6),  HOST_M(7),
    HOST_M(8),  HOST_M(9),  HOST_M(10), HOST_M(11), HOST_M(12), HOST_M(13), HOST_M(14), HOST_M(15),
    HOST_M(16), HOST_M(17), HOST_M(18), HOST_M(19), HOST_M(20), HOST_M(21), HOST_M(22), HOST_M(23),
    HOST_M(24), HOST_M(25), HOST_M(26), HOST_M(27), HOST_M(28), HOST_M(29), HOST_M(30), HOST_M(31),
    HOST_M(32), HOST_M(33), HOST_M(34), HOST_M(35), HOST_M(36), HOST_M(37), HOST_M(38), HOST_M(39),
    HOST_M(40), HOST_M(41), HOST_M(42), HOST_M(43), HOST_M(44), HOST_M(45), HOST_M(46), HOST_M(47),
    HOST_M(48), HOST_M(49), HOST_M(50), HOST_M(51), HOST_M(52), HOST_M(53), HOST_M(54), HOST_M(55),
    HOST_M(56), HOST_M(57), HOST_M(58), HOST_M(59), HOST_M(60), HOST_M(61), HOST_M(62), HOST_M(63)};
const ControlFn kControl[CCBLayerProxy::kSlots] = {
    HOST_C(0),  HOST_C(1),  HOST_C(2),  HOST_C(3),  HOST_C(4),  HOST_C(5),  HOST_C(6),  HOST_C(7),
    HOST_C(8),  HOST_C(9),  HOST_C(10), HOST_C(11), HOST_C(12), HOST_C(13), HOST_C(14), HOST_C(15),
    HOST_C(16), HOST_C(17), HOST_C(18), HOST_C(19), HOST_C(20), HOST_C(21), HOST_C(22), HOST_C(23),
    HOST_C(24), HOST_C(25), HOST_C(26), HOST_C(27), HOST_C(28), HOST_C(29), HOST_C(30), HOST_C(31),
    HOST_C(32), HOST_C(33), HOST_C(34), HOST_C(35), HOST_C(36), HOST_C(37), HOST_C(38), HOST_C(39),
    HOST_C(40), HOST_C(41), HOST_C(42), HOST_C(43), HOST_C(44), HOST_C(45), HOST_C(46), HOST_C(47),
    HOST_C(48), HOST_C(49), HOST_C(50), HOST_C(51), HOST_C(52), HOST_C(53), HOST_C(54), HOST_C(55),
    HOST_C(56), HOST_C(57), HOST_C(58), HOST_C(59), HOST_C(60), HOST_C(61), HOST_C(62), HOST_C(63)};
#undef HOST_M
#undef HOST_C

// 压入钩子函数；没有钩子时返回 false
bool BeginHook(lua_State*& L, const HookHolder& h) {
    L = GetLuaState();
    return L && PushHandler(L, h.Handler());
}

bool ResultTrue(lua_State* L) {
    const bool r = lua_toboolean(L, -1) != 0 || (lua_isnumber(L, -1) && lua_tonumber(L, -1) != 0);
    lua_pop(L, 1);
    return r;
}

}  // namespace

CCBLayerProxy::~CCBLayerProxy() {}

SEL_MenuHandler CCBLayerProxy::onResolveCCBCCMenuItemSelector(CCObject* target, const char* name) {
    lua_State* L;
    if (!BeginHook(L, hook_)) return nullptr;
    if (menuSlots_ >= kSlots) {
        Log("ccb 菜单回调超过 %d 个：%s", kSlots, name);
        lua_pop(L, 1);
        return nullptr;
    }
    lua_pushstring(L, "onResolveCCBCCMenuItemSelector");
    lua_pushinteger(L, menuSlots_);
    lua_pushstring(L, name);
    PCall(L, 3, 1);
    if (!ResultTrue(L)) return nullptr;
    return static_cast<SEL_MenuHandler>(kMenu[menuSlots_++]);
}

SEL_CCControlHandler CCBLayerProxy::onResolveCCBCCControlSelector(CCObject* target, const char* name) {
    lua_State* L;
    if (!BeginHook(L, hook_)) return nullptr;
    if (controlSlots_ >= kSlots) {
        Log("ccb 控件回调超过 %d 个：%s", kSlots, name);
        lua_pop(L, 1);
        return nullptr;
    }
    lua_pushstring(L, "onResolveCCBCCControlSelector");
    lua_pushinteger(L, controlSlots_);
    lua_pushstring(L, name);
    PCall(L, 3, 1);
    if (!ResultTrue(L)) return nullptr;
    return static_cast<SEL_CCControlHandler>(kControl[controlSlots_++]);
}

bool CCBLayerProxy::onAssignCCBMemberVariable(CCObject* target, const char* name, CCNode* node) {
    lua_State* L;
    if (!BeginHook(L, hook_)) return false;
    lua_pushstring(L, "onAssignCCBMemberVariable");
    PushObject(L, target);
    lua_pushstring(L, name);
    PushObject(L, node, "CCNode");
    PCall(L, 4, 1);
    return ResultTrue(L);
}

void CCBLayerProxy::onNodeLoaded(CCNode* node, CCNodeLoader* loader) {
    lua_State* L;
    if (!BeginHook(L, hook_)) return;
    lua_pushstring(L, "onNodeLoaded");
    PushObject(L, node, "CCNode");
    toluafix_pushusertype_ccobject(L, loader->m_uID, &loader->m_nLuaID, loader, "CCNodeLoader");
    PCall(L, 3, 0);
}

void CCBLayerProxy::OnMenuSlot(int slot, CCObject* sender) {
    lua_State* L;
    if (!BeginHook(L, hook_)) return;
    retain();  // 回调里可能移除自己
    lua_pushstring(L, "onMenuItem");
    lua_pushinteger(L, slot);
    PushObject(L, sender);
    PCall(L, 3, 0);
    release();
}

void CCBLayerProxy::OnControlSlot(int slot, CCObject* sender, unsigned int event) {
    lua_State* L;
    if (!BeginHook(L, hook_)) return;
    retain();
    lua_pushstring(L, "onControl");
    lua_pushinteger(L, slot);
    PushObject(L, sender);
    lua_pushinteger(L, static_cast<lua_Integer>(event));
    PCall(L, 4, 0);
    release();
}

// ---------------------------------------------------------------- 加载器 / 库

CCBLayerLoaderProxy* CCBLayerLoaderProxy::loader() {
    CCBLayerLoaderProxy* p = new CCBLayerLoaderProxy();
    p->autorelease();
    return p;
}

CCLayer* CCBLayerLoaderProxy::createCCNode(CCNode*, CCBReader* reader) {
    CCBLayerProxy* layer = CCBLayerProxy::create();
    lua_State* L;
    if (BeginHook(L, hook_)) {
        PushObject(L, layer, "CCBLayerProxy");
        toluafix_pushusertype_ccobject(L, reader->m_uID, &reader->m_nLuaID, reader, "CCBReader");
        PCall(L, 2, 0);
    }
    return layer;
}

CCNodeLoaderLibraryProxy* CCNodeLoaderLibraryProxy::newDefaultCCNodeLoaderLibrary() {
    CCNodeLoaderLibraryProxy* p = new CCNodeLoaderLibraryProxy();
    p->registerDefaultCCNodeLoaders();
    p->autorelease();
    return p;
}

CCNodeLoader* CCNodeLoaderLibraryProxy::getCCNodeLoader(const char* className) {
    if (CCNodeLoader* l = CCNodeLoaderLibrary::getCCNodeLoader(className)) return l;
    lua_State* L;
    if (inHook_ || !BeginHook(L, hook_)) return nullptr;
    inHook_ = true;
    PushObject(L, this, "CCNodeLoaderLibraryProxy");
    lua_pushstring(L, className);
    PCall(L, 2, 0);
    inHook_ = false;
    CCNodeLoader* l = CCNodeLoaderLibrary::getCCNodeLoader(className);
    if (!l) Log("ccb 自定义类 %s 没有加载器", className);
    return l;
}

void CCBAnimationManagerDelegateProxy::completedAnimationSequenceNamed(const char* name) {
    lua_State* L;
    if (!BeginHook(L, hook_)) return;
    lua_pushstring(L, "completedAnimationSequenceNamed");
    lua_pushstring(L, name ? name : "");
    PCall(L, 2, 0);
}

// ---------------------------------------------------------------- tolua 绑定

namespace {

template <typename T>
T* Self(lua_State* L, const char* type) {
    tolua_Error err;
    if (!tolua_isusertype(L, 1, type, 0, &err)) {
        luaL_error(L, "%s: self 类型错误", type);
        return nullptr;
    }
    return static_cast<T*>(tolua_tousertype(L, 1, nullptr));
}

int Handler(lua_State* L, int idx) {
    luaL_checktype(L, idx, LUA_TFUNCTION);
    return RefFunction(L, idx);
}

int l_LayerHook(lua_State* L) {
    Self<CCBLayerProxy>(L, "CCBLayerProxy")->hook(Handler(L, 2));
    return 0;
}
int l_LayerUnhook(lua_State* L) {
    Self<CCBLayerProxy>(L, "CCBLayerProxy")->unhook();
    return 0;
}

int l_LoaderCreate(lua_State* L) {
    CCBLayerLoaderProxy* p = CCBLayerLoaderProxy::loader();
    toluafix_pushusertype_ccobject(L, p->m_uID, &p->m_nLuaID, p, "CCBLayerLoaderProxy");
    return 1;
}
int l_LoaderHook(lua_State* L) {
    Self<CCBLayerLoaderProxy>(L, "CCBLayerLoaderProxy")->hook(Handler(L, 2));
    return 0;
}
int l_LoaderUnhook(lua_State* L) {
    Self<CCBLayerLoaderProxy>(L, "CCBLayerLoaderProxy")->unhook();
    return 0;
}

int l_LibNewDefault(lua_State* L) {
    CCNodeLoaderLibraryProxy* p = CCNodeLoaderLibraryProxy::newDefaultCCNodeLoaderLibrary();
    toluafix_pushusertype_ccobject(L, p->m_uID, &p->m_nLuaID, p, "CCNodeLoaderLibraryProxy");
    return 1;
}
int l_LibHook(lua_State* L) {
    Self<CCNodeLoaderLibraryProxy>(L, "CCNodeLoaderLibraryProxy")->hook(Handler(L, 2));
    return 0;
}
int l_LibUnhook(lua_State* L) {
    Self<CCNodeLoaderLibraryProxy>(L, "CCNodeLoaderLibraryProxy")->unhook();
    return 0;
}

// CCNodeLoaderLibrary:registerCCNodeLoader(name, loader)
int l_LibRegister(lua_State* L) {
    CCNodeLoaderLibrary* lib = Self<CCNodeLoaderLibrary>(L, "CCNodeLoaderLibrary");
    const char* name = luaL_checkstring(L, 2);
    CCNodeLoader* loader = static_cast<CCNodeLoader*>(tolua_tousertype(L, 3, nullptr));
    if (lib && loader) lib->registerCCNodeLoader(name, loader);
    return 0;
}
int l_LibUnregister(lua_State* L) {
    CCNodeLoaderLibrary* lib = Self<CCNodeLoaderLibrary>(L, "CCNodeLoaderLibrary");
    if (lib) lib->unregisterCCNodeLoader(luaL_checkstring(L, 2));
    return 0;
}
int l_LibRegisterDefault(lua_State* L) {
    if (CCNodeLoaderLibrary* lib = Self<CCNodeLoaderLibrary>(L, "CCNodeLoaderLibrary")) lib->registerDefaultCCNodeLoaders();
    return 0;
}
int l_LibLibrary(lua_State* L) {
    CCNodeLoaderLibrary* p = CCNodeLoaderLibrary::library();
    toluafix_pushusertype_ccobject(L, p->m_uID, &p->m_nLuaID, p, "CCNodeLoaderLibrary");
    return 1;
}
int l_LibShared(lua_State* L) {
    CCNodeLoaderLibrary* p = CCNodeLoaderLibrary::sharedCCNodeLoaderLibrary();
    toluafix_pushusertype_ccobject(L, p->m_uID, &p->m_nLuaID, p, "CCNodeLoaderLibrary");
    return 1;
}

// CCBAnimationManagerDelegateProxy() —— 通过类表的 __call（tolua 的 ".call"）创建
int l_DelegateNew(lua_State* L) {
    CCBAnimationManagerDelegateProxy* p = new CCBAnimationManagerDelegateProxy();
    p->autorelease();
    toluafix_pushusertype_ccobject(L, p->m_uID, &p->m_nLuaID, p, "CCBAnimationManagerDelegateProxy");
    return 1;
}
int l_DelegateHook(lua_State* L) {
    Self<CCBAnimationManagerDelegateProxy>(L, "CCBAnimationManagerDelegateProxy")->hook(Handler(L, 2));
    return 0;
}
int l_DelegateUnhook(lua_State* L) {
    Self<CCBAnimationManagerDelegateProxy>(L, "CCBAnimationManagerDelegateProxy")->unhook();
    return 0;
}

// CCBAnimationManager:setDelegate(delegateProxy)
int l_AnimSetDelegate(lua_State* L) {
    CCBAnimationManager* m = Self<CCBAnimationManager>(L, "CCBAnimationManager");
    CCObject* o = static_cast<CCObject*>(tolua_tousertype(L, 2, nullptr));
    if (m) m->setDelegate(dynamic_cast<CCBAnimationManagerDelegate*>(o));
    return 0;
}

// 旧版 API：runAnimations(name[, tween]) / runAnimations(id[, tween])
int l_AnimRunAnimations(lua_State* L) {
    CCBAnimationManager* m = Self<CCBAnimationManager>(L, "CCBAnimationManager");
    if (!m) return 0;
    const float tween = static_cast<float>(luaL_optnumber(L, 3, 0));
    if (lua_type(L, 2) == LUA_TNUMBER)
        m->runAnimationsForSequenceIdTweenDuration(static_cast<int>(lua_tonumber(L, 2)), tween);
    else
        m->runAnimationsForSequenceNamedTweenDuration(luaL_checkstring(L, 2), tween);
    return 0;
}

// CCBReader:new([library]) —— 覆盖 cocos2d-x 自带版本，支持传入加载器库
int l_ReaderNew(lua_State* L) {
    CCNodeLoaderLibrary* lib = nullptr;
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) lib = static_cast<CCNodeLoaderLibrary*>(tolua_tousertype(L, 2, nullptr));
    if (!lib) lib = CCNodeLoaderLibrary::sharedCCNodeLoaderLibrary();
    CCBReader* r = new CCBReader(lib, nullptr, nullptr, nullptr);
    toluafix_pushusertype_ccobject(L, r->m_uID, &r->m_nLuaID, r, "CCBReader");
    return 1;
}

// reader:readNodeGraphFromFile(file[, owner[, size]])
int l_ReaderReadFile(lua_State* L) {
    CCBReader* r = Self<CCBReader>(L, "CCBReader");
    const char* file = luaL_checkstring(L, 2);
    CCObject* owner = lua_gettop(L) >= 3 && !lua_isnil(L, 3) ? static_cast<CCObject*>(tolua_tousertype(L, 3, nullptr)) : nullptr;
    CCNode* node = nullptr;
    if (lua_gettop(L) >= 4 && !lua_isnil(L, 4)) {
        CCSize* size = static_cast<CCSize*>(tolua_tousertype(L, 4, nullptr));
        node = r->readNodeGraphFromFile(file, owner, *size);
    } else {
        node = r->readNodeGraphFromFile(file, owner);
    }
    if (!node) Log("ccb 加载失败：%s", file);
    PushObject(L, node, "CCNode");
    return 1;
}

int l_ReaderGetAnimationManager(lua_State* L) {
    CCBReader* r = Self<CCBReader>(L, "CCBReader");
    CCBAnimationManager* m = r ? r->getAnimationManager() : nullptr;
    if (!m) {
        lua_pushnil(L);
        return 1;
    }
    toluafix_pushusertype_ccobject(L, m->m_uID, &m->m_nLuaID, m, "CCBAnimationManager");
    return 1;
}

void Fn(lua_State* L, const char* name, lua_CFunction f) { tolua_function(L, name, f); }

}  // namespace

void RegisterCCBProxy(lua_State* L) {
    tolua_open(L);
    tolua_usertype(L, "CCNodeLoader");
    tolua_usertype(L, "CCLayerLoader");
    tolua_usertype(L, "CCNodeLoaderLibrary");
    tolua_usertype(L, "CCNodeLoaderLibraryProxy");
    tolua_usertype(L, "CCBLayerLoaderProxy");
    tolua_usertype(L, "CCBLayerProxy");
    tolua_usertype(L, "CCBAnimationManagerDelegateProxy");
    tolua_usertype(L, "CCBReader");
    tolua_usertype(L, "CCBAnimationManager");

    tolua_module(L, nullptr, 0);
    tolua_beginmodule(L, nullptr);

    tolua_cclass(L, "CCNodeLoader", "CCNodeLoader", "CCObject", nullptr);
    tolua_cclass(L, "CCLayerLoader", "CCLayerLoader", "CCNodeLoader", nullptr);
    tolua_cclass(L, "CCBLayerLoaderProxy", "CCBLayerLoaderProxy", "CCLayerLoader", nullptr);
    tolua_beginmodule(L, "CCBLayerLoaderProxy");
    Fn(L, "loader", l_LoaderCreate);
    Fn(L, "hook", l_LoaderHook);
    Fn(L, "unhook", l_LoaderUnhook);
    tolua_endmodule(L);

    tolua_cclass(L, "CCNodeLoaderLibrary", "CCNodeLoaderLibrary", "CCObject", nullptr);
    tolua_beginmodule(L, "CCNodeLoaderLibrary");
    Fn(L, "library", l_LibLibrary);
    Fn(L, "registerDefaultCCNodeLoaders", l_LibRegisterDefault);
    Fn(L, "registerCCNodeLoader", l_LibRegister);
    Fn(L, "unregisterCCNodeLoader", l_LibUnregister);
    Fn(L, "sharedCCNodeLoaderLibrary", l_LibShared);
    Fn(L, "newDefaultCCNodeLoaderLibrary", l_LibNewDefault);
    tolua_endmodule(L);

    tolua_cclass(L, "CCNodeLoaderLibraryProxy", "CCNodeLoaderLibraryProxy", "CCNodeLoaderLibrary", nullptr);
    tolua_beginmodule(L, "CCNodeLoaderLibraryProxy");
    Fn(L, "newDefaultCCNodeLoaderLibrary", l_LibNewDefault);
    Fn(L, "hook", l_LibHook);
    Fn(L, "unhook", l_LibUnhook);
    tolua_endmodule(L);

    tolua_cclass(L, "CCBLayerProxy", "CCBLayerProxy", "CCLayer", nullptr);
    tolua_beginmodule(L, "CCBLayerProxy");
    Fn(L, "hook", l_LayerHook);
    Fn(L, "unhook", l_LayerUnhook);
    tolua_endmodule(L);

    tolua_cclass(L, "CCBAnimationManagerDelegateProxy", "CCBAnimationManagerDelegateProxy", "CCObject", nullptr);
    tolua_beginmodule(L, "CCBAnimationManagerDelegateProxy");
    Fn(L, "new", l_DelegateNew);
    Fn(L, "new_local", l_DelegateNew);
    Fn(L, ".call", l_DelegateNew);
    Fn(L, "hook", l_DelegateHook);
    Fn(L, "unhook", l_DelegateUnhook);
    tolua_endmodule(L);

    // 在 cocos2d-x 自带的 CCBReader / CCBAnimationManager 上补方法（类已由 tolua_extensions_ccb_open 注册）
    tolua_beginmodule(L, "CCBReader");
    Fn(L, "new", l_ReaderNew);
    Fn(L, "readNodeGraphFromFile", l_ReaderReadFile);
    Fn(L, "getAnimationManager", l_ReaderGetAnimationManager);
    tolua_endmodule(L);
    tolua_beginmodule(L, "CCBAnimationManager");
    Fn(L, "setDelegate", l_AnimSetDelegate);
    Fn(L, "runAnimations", l_AnimRunAnimations);
    tolua_endmodule(L);

    tolua_endmodule(L);
}

}  // namespace host
