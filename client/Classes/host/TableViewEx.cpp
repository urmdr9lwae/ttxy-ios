#include "TableViewEx.h"

#include <algorithm>
#include <cmath>

#include "HostLog.h"
#include "LuaCall.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "tolua++.h"
#include "tolua_fix.h"
}

USING_NS_CC;
USING_NS_CC_EXT;

namespace host {
namespace {

bool BeginHook(lua_State*& L, const HookHolder& h) {
    L = GetLuaState();
    return L && PushHandler(L, h.Handler());
}

void PushTable(lua_State* L, CCTableView* t) {
    toluafix_pushusertype_ccobject(L, t->m_uID, &t->m_nLuaID, t, dynamic_cast<CCTableViewEx*>(t) ? "CCTableViewEx" : "CCTableView");
}

}  // namespace

// ---------------------------------------------------------------- CCTableViewProxy

CCTableViewProxy* CCTableViewProxy::create() {
    CCTableViewProxy* p = new CCTableViewProxy();
    p->autorelease();
    return p;
}

CCSize CCTableViewProxy::cellSizeForTable(CCTableView* table) {
    lua_State* L;
    if (!BeginHook(L, hook_)) return lastSize_;
    lua_pushstring(L, "cellSizeForTable");
    PushTable(L, table);
    PCall(L, 2, 1);
    tolua_Error err;
    // tolua 的类型检查要用正数（绝对）索引：内部会继续压栈，负索引会指错位置
    const int top = lua_gettop(L);
    if (tolua_isusertype(L, top, "CCSize", 0, &err)) lastSize_ = *static_cast<CCSize*>(tolua_tousertype(L, top, nullptr));
    lua_pop(L, 1);
    return lastSize_;
}

CCTableViewCell* CCTableViewProxy::tableCellAtIndex(CCTableView* table, unsigned int idx) {
    lua_State* L;
    CCTableViewCell* cell = nullptr;
    if (!BeginHook(L, hook_)) return CCTableViewCellEx::create();
    lua_pushstring(L, "tableCellAtIndex");
    PushTable(L, table);
    lua_pushinteger(L, static_cast<lua_Integer>(idx));
    CCTableViewCell* reuse = table->dequeueCell();
    if (reuse)
        toluafix_pushusertype_ccobject(L, reuse->m_uID, &reuse->m_nLuaID, reuse, "CCTableViewCellEx");
    else
        lua_pushnil(L);
    PCall(L, 4, 1);
    tolua_Error err;
    const int top = lua_gettop(L);  // 绝对索引，见 cellSizeForTable
    if (tolua_isusertype(L, top, "CCTableViewCell", 0, &err))
        cell = static_cast<CCTableViewCell*>(tolua_tousertype(L, top, nullptr));
    lua_pop(L, 1);
    // 返回空时给一个空单元格，避免 CCTableView 崩溃
    return cell ? cell : CCTableViewCellEx::create();
}

unsigned int CCTableViewProxy::numberOfCellsInTableView(CCTableView* table) {
    lua_State* L;
    if (!BeginHook(L, hook_)) return 0;
    lua_pushstring(L, "numberOfCellsInTableView");
    PushTable(L, table);
    PCall(L, 2, 1);
    const lua_Number n = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : 0;
    lua_pop(L, 1);
    return n > 0 ? static_cast<unsigned int>(n) : 0;
}

void CCTableViewProxy::tableCellTouched(CCTableView* table, CCTableViewCell* cell) {
    lua_State* L;
    if (!BeginHook(L, hook_)) return;
    table->retain();
    lua_pushstring(L, "tableCellTouched");
    PushTable(L, table);
    toluafix_pushusertype_ccobject(L, cell->m_uID, &cell->m_nLuaID, cell, "CCTableViewCell");
    PCall(L, 3, 0);
    table->release();
}

int CCTableViewProxy::tablePageTurn(CCTableView* table, int dir) {
    lua_State* L;
    if (!BeginHook(L, hook_)) return 0;
    lua_pushstring(L, "tablePageTurn");
    PushTable(L, table);
    lua_pushinteger(L, dir);
    PCall(L, 3, 1);
    const int r = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return r;
}

void CCTableViewProxy::uiActionCallBack(CCTableView* table, CCTableViewCell* cell, bool open) {
    lua_State* L;
    if (!BeginHook(L, hook_)) return;
    lua_pushstring(L, open ? "actionFinish_Open" : "actionFinish_Close");
    PushTable(L, table);
    if (cell)
        toluafix_pushusertype_ccobject(L, cell->m_uID, &cell->m_nLuaID, cell, "CCTableViewCell");
    else
        lua_pushnil(L);
    PCall(L, 3, 0);
}

// ---------------------------------------------------------------- CCTableViewCellEx

CCTableViewCellEx* CCTableViewCellEx::create() {
    CCTableViewCellEx* c = new CCTableViewCellEx();
    c->init();
    c->autorelease();
    return c;
}

// ---------------------------------------------------------------- 滚动条

void ScrollBar::Set(CCNode* owner, CCScale9Sprite* bar, CCScale9Sprite* bg) {
    Clear();
    bar_ = bar;
    bg_ = bg;
    if (bg_) {
        bg_->retain();
        bg_->setAnchorPoint(ccp(1, 0));
        owner->addChild(bg_, 100);
    }
    if (bar_) {
        bar_->retain();
        bar_->setAnchorPoint(ccp(1, 0));
        owner->addChild(bar_, 101);
    }
}

void ScrollBar::Clear() {
    if (bar_) { bar_->removeFromParent(); bar_->release(); bar_ = nullptr; }
    if (bg_) { bg_->removeFromParent(); bg_->release(); bg_ = nullptr; }
}

void ScrollBar::Update(CCScrollView* view) {
    if (!bar_) return;
    const CCSize vs = view->getViewSize();
    const CCSize cs = view->getContainer()->getContentSize();
    const float x = vs.width - offset;
    if (bg_) {
        bg_->setPreferredSize(CCSizeMake(bg_->getOriginalSize().width, vs.height));
        bg_->setPosition(ccp(x, 0));
    }
    if (cs.height <= vs.height + 1) {
        bar_->setVisible(false);
        if (bg_) bg_->setVisible(false);
        return;
    }
    bar_->setVisible(true);
    if (bg_) bg_->setVisible(true);
    const float h = std::max(vs.height * vs.height / cs.height, bar_->getOriginalSize().height);
    bar_->setPreferredSize(CCSizeMake(bar_->getOriginalSize().width, h));
    // 容器 y 在 [minY, 0] 之间：minY 表示显示最上面
    const float minY = vs.height - cs.height;
    float t = (view->getContentOffset().y - minY) / (0 - minY);  // 0 = 顶部, 1 = 底部
    t = std::min(1.f, std::max(0.f, t));
    bar_->setPosition(ccp(x, (vs.height - h) * (1 - t)));
}

// ---------------------------------------------------------------- CCTableViewEx

CCTableViewEx* CCTableViewEx::create(CCTableViewProxy* proxy, CCSize size, CCNode* container) {
    CCTableViewEx* t = new CCTableViewEx();
    t->initWithViewSize(size, container);
    t->autorelease();
    t->proxy_ = proxy;
    if (proxy) proxy->retain();
    t->setDataSource(proxy);
    t->setDelegate(proxy);
    t->_updateCellPositions();  // 和 CCTableView::create 一样，先算位置再算内容大小
    t->_updateContentSize();
    return t;
}

CCTableViewEx::~CCTableViewEx() {
    scrollBar_.Clear();
    CC_SAFE_RELEASE(specialCells_);
    CC_SAFE_RELEASE(proxy_);
}

void CCTableViewEx::setDirection(CCScrollViewDirection dir) {
    pageTurn_ = dir == kCCScrollViewDirectionBoth;
    CCTableView::setDirection(pageTurn_ ? kCCScrollViewDirectionVertical : dir);
}

void CCTableViewEx::reloadDataEx(bool animate) {
    CCTableView::reloadData();
    scrollBar_.Update(this);
    if (animate) runUIAnimat(true);
}

void CCTableViewEx::resetOffsetPositon() {
    if (getDirection() == kCCScrollViewDirectionHorizontal)
        setContentOffset(ccp(maxContainerOffset().x, 0), false);
    else if (getVerticalFillOrder() == kCCTableViewFillTopDown)
        setContentOffset(ccp(0, minContainerOffset().y), false);
    else
        setContentOffset(ccp(0, maxContainerOffset().y), false);
    scrollBar_.Update(this);
}

void CCTableViewEx::reloadDataAndResetOffset() {
    CCTableView::reloadData();
    resetOffsetPositon();
}

void CCTableViewEx::refreshData() {
    const CCPoint off = getContentOffset();
    CCTableView::reloadData();
    const CCPoint mn = minContainerOffset(), mx = maxContainerOffset();
    setContentOffset(ccp(std::min(mx.x, std::max(mn.x, off.x)), std::min(mx.y, std::max(mn.y, off.y))), false);
    scrollBar_.Update(this);
}

void CCTableViewEx::clearData(bool) {
    if (m_pCellsUsed) {
        while (m_pCellsUsed->count() > 0) {
            CCTableViewCell* c = static_cast<CCTableViewCell*>(m_pCellsUsed->objectAtIndex(0));
            _moveCellOutOfSight(c);
        }
    }
    if (m_pIndices) m_pIndices->clear();
}

void CCTableViewEx::runUIAnimat(bool open) {
    animOpen_ = open;
    CCArray* cells = m_pCellsUsed;
    if (!cells || cells->count() == 0) {
        if (proxy_) proxy_->uiActionCallBack(this, nullptr, open);
        return;
    }
    const float w = getViewSize().width;
    for (unsigned int i = 0; i < cells->count(); ++i) {
        CCTableViewCell* c = static_cast<CCTableViewCell*>(cells->objectAtIndex(i));
        const CCPoint target = c->getPosition();
        c->stopAllActions();
        if (open) c->setPosition(ccp(target.x + w, target.y));
        CCFiniteTimeAction* move =
            CCEaseSineOut::create(CCMoveTo::create(animBase_, open ? target : ccp(target.x - w, target.y)));
        c->runAction(CCSequence::create(CCDelayTime::create(animDelta_ * i), move,
                                        CCCallFuncN::create(this, callfuncN_selector(CCTableViewEx::OnCellAnimDone)),
                                        nullptr));
        if (!open) c->setPosition(target);
    }
}

void CCTableViewEx::OnCellAnimDone(CCNode* cell) {
    if (proxy_) proxy_->uiActionCallBack(this, dynamic_cast<CCTableViewCell*>(cell), animOpen_);
}

void CCTableViewEx::setScrollBar(CCScale9Sprite* bar, CCScale9Sprite* bg) {
    scrollBar_.Set(this, bar, bg);
    scrollBar_.Update(this);
}

void CCTableViewEx::addSpecialCell(CCNode* node, bool) {
    if (!node) return;
    if (!specialCells_) {
        specialCells_ = CCArray::create();
        specialCells_->retain();
    }
    specialCells_->addObject(node);
    getContainer()->addChild(node);
}

void CCTableViewEx::clearSpecialCell(bool cleanup) {
    if (!specialCells_) return;
    for (unsigned int i = 0; i < specialCells_->count(); ++i)
        static_cast<CCNode*>(specialCells_->objectAtIndex(i))->removeFromParentAndCleanup(cleanup);
    specialCells_->removeAllObjects();
}

void CCTableViewEx::setTableViewOffset(int idx) {
    if (idx < 0 || m_vCellsPositions.empty()) return;
    const unsigned int n = static_cast<unsigned int>(m_vCellsPositions.size() - 1);
    if (static_cast<unsigned int>(idx) >= n) idx = static_cast<int>(n) - 1;
    const CCPoint cellPos = _offsetFromIndex(static_cast<unsigned int>(idx));
    const CCSize vs = getViewSize();
    CCPoint off;
    if (getDirection() == kCCScrollViewDirectionHorizontal) {
        off = ccp(-cellPos.x, 0);
    } else {
        const CCSize cellSize = m_pDataSource->tableCellSizeForIndex(this, static_cast<unsigned int>(idx));
        off = ccp(0, -(cellPos.y + cellSize.height - vs.height));
    }
    const CCPoint mn = minContainerOffset(), mx = maxContainerOffset();
    off.x = std::min(mx.x, std::max(mn.x, off.x));
    off.y = std::min(mx.y, std::max(mn.y, off.y));
    setContentOffset(off, false);
}

void CCTableViewEx::stopScrolling() {
    // deaccelerateScrolling 在 2.2.6 里是私有的；CCScrollView 自己只调度这一个惯性滚动定时器
    unscheduleAllSelectors();
    getContainer()->stopAllActions();
}

void CCTableViewEx::scrollViewDidScroll(CCScrollView* view) {
    CCTableView::scrollViewDidScroll(view);
    scrollBar_.Update(this);
}

bool CCTableViewEx::ccTouchBegan(CCTouch* touch, CCEvent* event) {
    touchStart_ = touch->getLocation();
    return CCTableView::ccTouchBegan(touch, event);
}

void CCTableViewEx::ccTouchEnded(CCTouch* touch, CCEvent* event) {
    const CCPoint end = touch->getLocation();
    const float dx = end.x - touchStart_.x, dy = end.y - touchStart_.y;
    const bool turn = pageTurn_ && std::fabs(dx) > turnDistance_ &&
                      std::fabs(dy) < std::fabs(dx) * std::tan(turnAngle_ * 3.14159265f / 180.f);
    retain();
    CCTableView::ccTouchEnded(touch, event);
    if (turn && proxy_) proxy_->tablePageTurn(this, dx < 0 ? 1 : -1);
    release();
}

// ---------------------------------------------------------------- CCScrollViewEx

CCScrollViewEx* CCScrollViewEx::create(CCSize size, CCNode* container) {
    CCScrollViewEx* s = new CCScrollViewEx();
    if (s->initWithViewSize(size, container)) {
        s->autorelease();
        s->setDelegate(s);
        return s;
    }
    delete s;
    return nullptr;
}

CCScrollViewEx* CCScrollViewEx::create() {
    CCScrollViewEx* s = new CCScrollViewEx();
    if (s->init()) {
        s->autorelease();
        s->setDelegate(s);
        return s;
    }
    delete s;
    return nullptr;
}

void CCScrollViewEx::setScrollBar(CCScale9Sprite* bar, CCScale9Sprite* bg) {
    scrollBar_.Set(this, bar, bg);
    scrollBar_.Update(this);
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

template <typename T>
void PushObj(lua_State* L, T* o, const char* type) {
    if (!o) {
        lua_pushnil(L);
        return;
    }
    toluafix_pushusertype_ccobject(L, o->m_uID, &o->m_nLuaID, o, type);
}

CCSize SizeArg(lua_State* L, int idx) {
    tolua_Error err;
    if (tolua_isusertype(L, idx, "CCSize", 0, &err)) return *static_cast<CCSize*>(tolua_tousertype(L, idx, nullptr));
    return CCSizeZero;
}

CCNode* NodeArg(lua_State* L, int idx) {
    if (lua_gettop(L) < idx || lua_isnil(L, idx)) return nullptr;
    return static_cast<CCNode*>(tolua_tousertype(L, idx, nullptr));
}

// CCTableViewProxy
int l_ProxyCreate(lua_State* L) { PushObj(L, CCTableViewProxy::create(), "CCTableViewProxy"); return 1; }
int l_ProxyHook(lua_State* L) {
    luaL_checktype(L, 2, LUA_TFUNCTION);
    Self<CCTableViewProxy>(L, "CCTableViewProxy")->hook(RefFunction(L, 2));
    return 0;
}
int l_ProxyUnhook(lua_State* L) { Self<CCTableViewProxy>(L, "CCTableViewProxy")->unhook(); return 0; }

// CCTableViewCellEx
int l_CellCreate(lua_State* L) { PushObj(L, CCTableViewCellEx::create(), "CCTableViewCellEx"); return 1; }

// CCTableViewEx
#define TV Self<CCTableViewEx>(L, "CCTableViewEx")
int l_TvCreate(lua_State* L) {
    CCTableViewProxy* p = static_cast<CCTableViewProxy*>(tolua_tousertype(L, 2, nullptr));
    PushObj(L, CCTableViewEx::create(p, SizeArg(L, 3), NodeArg(L, 4)), "CCTableViewEx");
    return 1;
}
int l_TvReload(lua_State* L) { TV->reloadDataEx(lua_toboolean(L, 2) != 0); return 0; }
int l_TvReloadReset(lua_State* L) { TV->reloadDataAndResetOffset(); return 0; }
int l_TvResetOffset(lua_State* L) { TV->resetOffsetPositon(); return 0; }
int l_TvRefresh(lua_State* L) { TV->refreshData(); return 0; }
int l_TvClear(lua_State* L) { TV->clearData(lua_toboolean(L, 2) != 0); return 0; }
int l_TvRunAnim(lua_State* L) { TV->runUIAnimat(lua_isnoneornil(L, 2) ? true : lua_toboolean(L, 2) != 0); return 0; }
int l_TvScrollBar(lua_State* L) {
    TV->setScrollBar(static_cast<CCScale9Sprite*>(tolua_tousertype(L, 2, nullptr)),
                     static_cast<CCScale9Sprite*>(tolua_tousertype(L, 3, nullptr)));
    return 0;
}
int l_TvScrollOffset(lua_State* L) { TV->setScrollOffset(static_cast<float>(luaL_checknumber(L, 2))); return 0; }
int l_TvTurnSpeed(lua_State* L) { TV->setTurnPageThresholdSpeed(static_cast<float>(luaL_checknumber(L, 2))); return 0; }
int l_TvTurnDist(lua_State* L) { TV->setTurnPageThresholdDistance(static_cast<float>(luaL_checknumber(L, 2))); return 0; }
int l_TvTurnAngle(lua_State* L) { TV->setTurnPageAngle(static_cast<float>(luaL_checknumber(L, 2))); return 0; }
int l_TvLastItem(lua_State* L) { TV->setLastItemSize(SizeArg(L, 2)); return 0; }
int l_TvAddSpecial(lua_State* L) { TV->addSpecialCell(NodeArg(L, 2), lua_toboolean(L, 3) != 0); return 0; }
int l_TvClearSpecial(lua_State* L) { TV->clearSpecialCell(lua_isnoneornil(L, 2) ? true : lua_toboolean(L, 2) != 0); return 0; }
int l_TvSetOffset(lua_State* L) { TV->setTableViewOffset(static_cast<int>(luaL_checknumber(L, 2))); return 0; }
int l_TvSetBase(lua_State* L) { TV->setUIAnimatBaseTime(static_cast<float>(luaL_checknumber(L, 2))); return 0; }
int l_TvSetDelta(lua_State* L) { TV->setUIAnimatDeltaTime(static_cast<float>(luaL_checknumber(L, 2))); return 0; }
int l_TvGetBase(lua_State* L) { lua_pushnumber(L, TV->getUIAnimatBaseTime()); return 1; }
int l_TvGetDelta(lua_State* L) { lua_pushnumber(L, TV->getUIAnimatDeltaTime()); return 1; }
int l_TvStop(lua_State* L) { TV->stopScrolling(); return 0; }
int l_TvFillOrder(lua_State* L) {
    TV->setVerticalFillOrder(static_cast<CCTableViewVerticalFillOrder>(static_cast<int>(luaL_checknumber(L, 2))));
    return 0;
}
#undef TV

// CCScrollViewEx
int l_SvCreate(lua_State* L) {
    CCScrollViewEx* s = lua_gettop(L) >= 2 ? CCScrollViewEx::create(SizeArg(L, 2), NodeArg(L, 3)) : CCScrollViewEx::create();
    PushObj(L, s, "CCScrollViewEx");
    return 1;
}
int l_SvScrollBar(lua_State* L) {
    Self<CCScrollViewEx>(L, "CCScrollViewEx")
        ->setScrollBar(static_cast<CCScale9Sprite*>(tolua_tousertype(L, 2, nullptr)),
                       static_cast<CCScale9Sprite*>(tolua_tousertype(L, 3, nullptr)));
    return 0;
}
int l_SvScrollOffset(lua_State* L) {
    Self<CCScrollViewEx>(L, "CCScrollViewEx")->setScrollOffset(static_cast<float>(luaL_checknumber(L, 2)));
    return 0;
}

void Fn(lua_State* L, const char* name, lua_CFunction f) { tolua_function(L, name, f); }

}  // namespace

void RegisterTableViewEx(lua_State* L) {
    tolua_open(L);
    tolua_usertype(L, "CCTableViewProxy");
    tolua_usertype(L, "CCTableViewCellEx");
    tolua_usertype(L, "CCTableViewEx");
    tolua_usertype(L, "CCScrollViewEx");
    tolua_module(L, nullptr, 0);
    tolua_beginmodule(L, nullptr);

    tolua_constant(L, "kCCScrollViewExDirectionNone", kCCScrollViewDirectionNone);
    tolua_constant(L, "kCCScrollViewExDirectionHorizontal", kCCScrollViewDirectionHorizontal);
    tolua_constant(L, "kCCScrollViewExDirectionVertical", kCCScrollViewDirectionVertical);
    tolua_constant(L, "kCCScrollViewExDirectionBoth", kCCScrollViewDirectionBoth);
    // 原程序和引擎自带的 Lua 绑定都有这两个常量。不注册的话，副本列表传入 nil 会报错。
    tolua_constant(L, "kCCTableViewFillTopDown", kCCTableViewFillTopDown);
    tolua_constant(L, "kCCTableViewFillBottomUp", kCCTableViewFillBottomUp);

    tolua_cclass(L, "CCTableViewProxy", "CCTableViewProxy", "CCObject", nullptr);
    tolua_beginmodule(L, "CCTableViewProxy");
    Fn(L, "create", l_ProxyCreate);
    Fn(L, "hook", l_ProxyHook);
    Fn(L, "unhook", l_ProxyUnhook);
    tolua_endmodule(L);

    tolua_cclass(L, "CCTableViewCellEx", "CCTableViewCellEx", "CCTableViewCell", nullptr);
    tolua_beginmodule(L, "CCTableViewCellEx");
    Fn(L, "create", l_CellCreate);
    tolua_endmodule(L);

    tolua_cclass(L, "CCTableViewEx", "CCTableViewEx", "CCTableView", nullptr);
    tolua_beginmodule(L, "CCTableViewEx");
    Fn(L, "create", l_TvCreate);
    Fn(L, "reloadData", l_TvReload);
    Fn(L, "reloadDataAndResetOffset", l_TvReloadReset);
    Fn(L, "resetOffsetPositon", l_TvResetOffset);
    Fn(L, "refreshData", l_TvRefresh);
    Fn(L, "clearData", l_TvClear);
    Fn(L, "runUIAnimat", l_TvRunAnim);
    Fn(L, "setScrollBar", l_TvScrollBar);
    Fn(L, "setScrollOffset", l_TvScrollOffset);
    Fn(L, "setTurnPageThreshold_Speed", l_TvTurnSpeed);
    Fn(L, "setTurnPageThreshold_Distance", l_TvTurnDist);
    Fn(L, "setTurnPageAngle", l_TvTurnAngle);
    Fn(L, "setLastItemSize", l_TvLastItem);
    Fn(L, "addSpecialCell", l_TvAddSpecial);
    Fn(L, "clearSpecialCell", l_TvClearSpecial);
    Fn(L, "setTableViewOffset", l_TvSetOffset);
    Fn(L, "setUIAnimatBaseTime", l_TvSetBase);
    Fn(L, "setUIAnimatDeltaTime", l_TvSetDelta);
    Fn(L, "getUIAnimatBaseTime", l_TvGetBase);
    Fn(L, "getUIAnimatDeltaTime", l_TvGetDelta);
    Fn(L, "stopScrolling", l_TvStop);
    Fn(L, "setVerticalFillOrder", l_TvFillOrder);
    tolua_endmodule(L);

    tolua_cclass(L, "CCScrollViewEx", "CCScrollViewEx", "CCScrollView", nullptr);
    tolua_beginmodule(L, "CCScrollViewEx");
    Fn(L, "create", l_SvCreate);
    Fn(L, "setScrollBar", l_SvScrollBar);
    Fn(L, "setScrollOffset", l_SvScrollOffset);
    tolua_endmodule(L);

    tolua_endmodule(L);
}

}  // namespace host
