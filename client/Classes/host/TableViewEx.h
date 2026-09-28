// 原版 LuaCCTableViewProxy.cpp / CCTableViewEx / CCScrollViewEx 的重写（按 Lua 用法实现所需功能）
#pragma once
#include "cocos2d.h"
#include "cocos-ext.h"
#include "CCBProxy.h"

struct lua_State;

namespace host {

class CCTableViewEx;

// 数据源 + 代理，全部转给 Lua 钩子：hook(function(name, table, arg1, arg2) ... end)
class CCTableViewProxy : public cocos2d::CCObject,
                         public cocos2d::extension::CCTableViewDataSource,
                         public cocos2d::extension::CCTableViewDelegate {
public:
    static CCTableViewProxy* create();
    void hook(int handler) { hook_.Hook(handler); }
    void unhook() { hook_.Unhook(); }

    cocos2d::CCSize cellSizeForTable(cocos2d::extension::CCTableView* table) override;
    cocos2d::extension::CCTableViewCell* tableCellAtIndex(cocos2d::extension::CCTableView* table,
                                                          unsigned int idx) override;
    unsigned int numberOfCellsInTableView(cocos2d::extension::CCTableView* table) override;
    void tableCellTouched(cocos2d::extension::CCTableView* table, cocos2d::extension::CCTableViewCell* cell) override;
    void scrollViewDidScroll(cocos2d::extension::CCScrollView*) override {}
    void scrollViewDidZoom(cocos2d::extension::CCScrollView*) override {}

    int tablePageTurn(cocos2d::extension::CCTableView* table, int dir);
    void uiActionCallBack(cocos2d::extension::CCTableView* table, cocos2d::extension::CCTableViewCell* cell, bool open);

private:
    HookHolder hook_;
    cocos2d::CCSize lastSize_;
};

class CCTableViewCellEx : public cocos2d::extension::CCTableViewCell {
public:
    static CCTableViewCellEx* create();
};

// 竖直/水平滚动条（两张九宫格图：滑块和底槽）
class ScrollBar {
public:
    void Set(cocos2d::CCNode* owner, cocos2d::extension::CCScale9Sprite* bar, cocos2d::extension::CCScale9Sprite* bg);
    void Clear();
    void Update(cocos2d::extension::CCScrollView* view);
    float offset = 0;

private:
    cocos2d::extension::CCScale9Sprite* bar_ = nullptr;
    cocos2d::extension::CCScale9Sprite* bg_ = nullptr;
};

class CCTableViewEx : public cocos2d::extension::CCTableView {
public:
    static CCTableViewEx* create(CCTableViewProxy* proxy, cocos2d::CCSize size, cocos2d::CCNode* container = nullptr);
    ~CCTableViewEx();

    void setDirection(cocos2d::extension::CCScrollViewDirection dir) override;
    void reloadDataEx(bool animate);
    void reloadDataAndResetOffset();
    void resetOffsetPositon();
    void refreshData();
    void clearData(bool cleanup);
    void runUIAnimat(bool open);
    void setScrollBar(cocos2d::extension::CCScale9Sprite* bar, cocos2d::extension::CCScale9Sprite* bg);
    void setScrollOffset(float off) { scrollBar_.offset = off; }
    void setTurnPageThresholdSpeed(float v) { turnSpeed_ = v; }
    void setTurnPageThresholdDistance(float v) { turnDistance_ = v; }
    void setTurnPageAngle(float v) { turnAngle_ = v; }
    void setLastItemSize(const cocos2d::CCSize& s) { lastItemSize_ = s; }
    void addSpecialCell(cocos2d::CCNode* node, bool atEnd);
    void clearSpecialCell(bool cleanup);
    void setTableViewOffset(int idx);
    void setUIAnimatBaseTime(float t) { animBase_ = t; }
    void setUIAnimatDeltaTime(float t) { animDelta_ = t; }
    float getUIAnimatBaseTime() const { return animBase_; }
    float getUIAnimatDeltaTime() const { return animDelta_; }
    void stopScrolling();

    void scrollViewDidScroll(cocos2d::extension::CCScrollView* view) override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

private:
    void OnCellAnimDone(cocos2d::CCNode* cell);
    CCTableViewProxy* proxy_ = nullptr;
    ScrollBar scrollBar_;
    bool pageTurn_ = false;  // setDirection(Both)：横向滑动翻页，内容只竖向滚动
    cocos2d::CCPoint touchStart_;
    float turnSpeed_ = 0, turnDistance_ = 80, turnAngle_ = 30;
    float animBase_ = 0.15f, animDelta_ = 0.05f;
    bool animOpen_ = true;
    cocos2d::CCSize lastItemSize_;
    cocos2d::CCArray* specialCells_ = nullptr;
};

class CCScrollViewEx : public cocos2d::extension::CCScrollView, public cocos2d::extension::CCScrollViewDelegate {
public:
    void scrollViewDidScroll(cocos2d::extension::CCScrollView*) override { UpdateBar(); }
    void scrollViewDidZoom(cocos2d::extension::CCScrollView*) override {}
    static CCScrollViewEx* create(cocos2d::CCSize size, cocos2d::CCNode* container = nullptr);
    static CCScrollViewEx* create();
    void setScrollBar(cocos2d::extension::CCScale9Sprite* bar, cocos2d::extension::CCScale9Sprite* bg);
    void setScrollOffset(float off) { scrollBar_.offset = off; }
    void UpdateBar() { scrollBar_.Update(this); }

private:
    ScrollBar scrollBar_;
};

void RegisterTableViewEx(lua_State* L);

}  // namespace host
