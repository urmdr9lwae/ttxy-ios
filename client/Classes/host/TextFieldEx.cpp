#include "TextFieldEx.h"

#include <string>

#include "cocos2d.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "tolua++.h"
}

USING_NS_CC;

namespace host {

namespace {

const int kLimitTag = 0x7e11;  // 挂在输入框下面的限长节点

// 按 UTF-8 字符计长度；bDBCSAs2 时非 ASCII 字符算 2
int TextLen(const char* s, bool dbcsAs2) {
    int n = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p; ++p) {
        if ((*p & 0xC0) == 0x80) continue;  // UTF-8 后续字节
        n += (*p >= 0x80 && dbcsAs2) ? 2 : 1;
    }
    return n;
}

// 作为输入框的子节点存在（生命周期跟着输入框），同时充当它的 CCTextFieldDelegate
class LimitNode : public CCNode, public CCTextFieldDelegate {
public:
    static LimitNode* Get(CCTextFieldTTF* field, bool create) {
        LimitNode* n = dynamic_cast<LimitNode*>(field->getChildByTag(kLimitTag));
        if (!n && create) {
            n = new LimitNode();
            n->autorelease();
            n->setVisible(false);
            field->addChild(n, 0, kLimitTag);
            field->setDelegate(n);
        }
        return n;
    }

    int maxLen = -1;
    bool dbcsAs2 = true;

    bool onTextFieldInsertText(CCTextFieldTTF* sender, const char* text, int nLen) override {
        if (maxLen < 0 || (nLen == 1 && text[0] == '\n')) return false;
        const std::string ins(text, nLen);
        // 返回 true 表示拒绝插入
        return TextLen(sender->getString(), dbcsAs2) + TextLen(ins.c_str(), dbcsAs2) > maxLen;
    }
};

CCTextFieldTTF* Self(lua_State* L) {
    tolua_Error err;
    if (!tolua_isusertype(L, 1, "CCTextFieldTTF", 0, &err)) {
        tolua_error(L, "#ferror in function 'CCTextFieldTTF'", &err);
        return nullptr;
    }
    return static_cast<CCTextFieldTTF*>(tolua_tousertype(L, 1, nullptr));
}

// setMaxLens(maxLen, bDBCSCharAs2 = true)，maxLen < 0 表示不限
int l_SetMaxLens(lua_State* L) {
    CCTextFieldTTF* f = Self(L);
    if (!f) return 0;
    LimitNode* n = LimitNode::Get(f, true);
    n->maxLen = static_cast<int>(luaL_optnumber(L, 2, -1));
    n->dbcsAs2 = lua_isnoneornil(L, 3) ? true : lua_toboolean(L, 3) != 0;
    return 0;
}

int l_GetMaxLens(lua_State* L) {
    CCTextFieldTTF* f = Self(L);
    if (!f) return 0;
    LimitNode* n = LimitNode::Get(f, false);
    lua_pushnumber(L, n ? n->maxLen : -1);
    return 1;
}

int l_SetPasswordMode(lua_State* L) {
    CCTextFieldTTF* f = Self(L);
    if (!f) return 0;
    f->setSecureTextEntry(lua_toboolean(L, 2) != 0);
    return 0;
}

}  // namespace

void RegisterTextFieldEx(lua_State* L) {
    tolua_open(L);
    tolua_usertype(L, "CCTextFieldTTF");
    tolua_module(L, nullptr, 0);
    tolua_beginmodule(L, nullptr);
    tolua_beginmodule(L, "CCTextFieldTTF");
    tolua_function(L, "setMaxLens", l_SetMaxLens);
    tolua_function(L, "getMaxLens", l_GetMaxLens);
    tolua_function(L, "setPasswordMode", l_SetPasswordMode);
    tolua_endmodule(L);
    tolua_endmodule(L);
}

}  // namespace host
