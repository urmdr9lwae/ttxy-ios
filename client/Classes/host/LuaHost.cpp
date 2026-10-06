#include "LuaHost.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "cocos2d.h"
#include "CCLuaEngine.h"
#include "CCBProxy.h"
#include "FrameMap.h"
#include "HostFile.h"
#include "HostLog.h"
#include "LuaCall.h"
#include "LuaDataBindings.h"
#include "Net.h"
#include "TableViewEx.h"
#include "TextFieldEx.h"
#include "AccountCrypto.h"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
int luaopen_base64(lua_State* L);
int luaopen_bit(lua_State* L);
int luaopen_cjson(lua_State* L);
int luaopen_LuaXML_lib(lua_State* L);
int luaopen_lpeg(lua_State* L);
int luaopen_mime_core(lua_State* L);
int luaopen_socket_core(lua_State* L);
int luaopen_struct(lua_State* L);
}
#include "Lua_extensions_CCB.h"

USING_NS_CC;

namespace host {
namespace {

// 与原版 CTwLua::Init 相同的搜索顺序；脚本导出为 script/<路径>.dat
const char* kSearch[] = {
    "script/?.lua",           "script/UI/?.lua",         "script/Logic/?.lua",
    "script/Net/?.lua",       "script/Module/?/init.lua", "script/Module/?.lua",
    "script/Assist/?.lua",    "script/common/?.lua",      "script/Stage/?.lua",
    "script/base/?.lua",      "script/base/?/init.lua",   "script/base/UI/?.lua",
    "script/base/dependencies/?.lua", "script/base/dependencies/?/init.lua",
};

std::string ReplaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t p = 0;
    while ((p = s.find(from, p)) != std::string::npos) {
        s.replace(p, from.size(), to);
        p += to.size();
    }
    return s;
}

// package.loaders 里的自定义加载器：按搜索路径在资源里找 .dat（原版字节码）
int ScriptLoader(lua_State* L) {
    const std::string name = luaL_checkstring(L, 1);
    const std::string path = ReplaceAll(name, ".", "/");
    std::string tried;
    for (const char* tpl : kSearch) {
        std::string file = ReplaceAll(tpl, "?", path);
        file = file.substr(0, file.size() - 4) + ".dat";
        std::string data;
        if (ReadResource(file, data)) {
            if (luaL_loadbuffer(L, data.data(), data.size(), ("@" + file).c_str()) != 0) {
                return luaL_error(L, "加载脚本 %s 失败：%s", file.c_str(), lua_tostring(L, -1));
            }
            return 1;
        }
        tried += "\n\tno resource '" + file + "'";
    }
    lua_pushstring(L, tried.c_str());
    return 1;
}

void AddLoader(lua_State* L) {
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "loaders");
    // 插到第 2 位（preload 之后）
    const int n = static_cast<int>(lua_objlen(L, -1));
    for (int i = n; i >= 2; --i) {
        lua_rawgeti(L, -1, i);
        lua_rawseti(L, -2, i + 1);
    }
    lua_pushcfunction(L, ScriptLoader);
    lua_rawseti(L, -2, 2);
    lua_pop(L, 1);
    std::string p;
    for (const char* tpl : kSearch) p += std::string(p.empty() ? "" : ";") + tpl;
    lua_pushstring(L, p.c_str());
    lua_setfield(L, -2, "path");
    lua_pop(L, 1);
}

void Preload(lua_State* L, const char* name, lua_CFunction f) {
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "preload");
    lua_pushcfunction(L, f);
    lua_setfield(L, -2, name);
    lua_pop(L, 2);
}

// 打开库；name 非空时把返回的模块表同时设为全局变量和 package.loaded[name]（lua-cjson 默认不设全局）
void OpenLib(lua_State* L, lua_CFunction f, const char* name = nullptr) {
    lua_pushcfunction(L, f);
    lua_call(L, 0, 1);
    if (name && lua_istable(L, -1)) {
        lua_pushvalue(L, -1);
        lua_setglobal(L, name);
        lua_getglobal(L, "package");
        lua_getfield(L, -1, "loaded");
        lua_pushvalue(L, -3);
        lua_setfield(L, -2, name);
        lua_pop(L, 2);
    }
    lua_pop(L, 1);
}

bool RunString(lua_State* L, const char* code, const char* chunk) {
    if (luaL_loadbuffer(L, code, strlen(code), chunk) != 0) {
        Log("兼容脚本编译失败：%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return PCall(L, 0, 0);
}

// 原版引擎（cocos2d-x 2.2.3 改版）有、2.2.6 没有的小接口，用 Lua 补齐
const char* kCompat = R"LUA(
fenv = { getfenv = getfenv, setfenv = setfenv }

local rawExit = os.exit
os.exit = function(...) WriteLog("os.exit 被调用\n" .. debug.traceback()) return rawExit(...) end

local defFont, defSize = "Helvetica", 12
CCLabelTTF.setDefaultFontName = function(_, name) defFont = name end
CCLabelTTF.setDefaultFontSize = function(_, size) defSize = size end
CCLabelTTF.getDefaultFontName = function() return defFont end
CCLabelTTF.getDefaultFontSize = function() return defSize end
local labelCreate = CCLabelTTF.create
CCLabelTTF.create = function(cls, str, font, size, ...)
    if str == nil then return labelCreate(cls, "", defFont, defSize) end
    if font == nil or font == "" then font = defFont end
    return labelCreate(cls, str, font, size or defSize, ...)
end

CCNode.getPositionLua = function(self) return ccp(self:getPositionX(), self:getPositionY()) end

-- 纹理预加载：同步加载即可（预加载器按 textureForKey 判断完成）
if CCTextureCache.addImageAsync == nil then
    CCTextureCache.addImageAsync = function(self, path) return self:addImage(path) end
end

-- 原版引擎给 CCLabelTTF 加的描边/阴影样式
kCCLabelTTFStyleSimple, kCCLabelTTFStyleShadow, kCCLabelTTFStyleOutline = 0, 1, 2
local labelStyles = setmetatable({}, { __mode = "k" })
CCLabelTTF.setStyle = function(self, style, ...)
    labelStyles[self] = style
    if style == kCCLabelTTFStyleOutline and self.enableStroke then
        pcall(self.enableStroke, self, ccc3(0, 0, 0), 1, true)
    elseif style == kCCLabelTTFStyleShadow and self.enableShadow then
        pcall(self.enableShadow, self, CCSizeMake(1, -1), 1, 0, true)
    end
end
CCLabelTTF.getStyle = function(self) return labelStyles[self] or kCCLabelTTFStyleSimple end

-- 原版引擎把触摸事件类型导出成常量；2.2.6 的 CCLuaEngine 传给 Lua 的是字符串，常量取同样的值
CCTOUCHBEGAN, CCTOUCHMOVED, CCTOUCHENDED, CCTOUCHCANCELLED = "began", "moved", "ended", "cancelled"
)LUA";

}  // namespace

bool LuaHostStart(const EnvInfo& env) {
    CCLuaEngine* engine = CCLuaEngine::defaultEngine();
    CCScriptEngineManager::sharedManager()->setScriptEngine(engine);
    lua_State* L = engine->getLuaStack()->getLuaState();
    SetLuaState(L);

    // 第三方库（原版 CTwLua::Setup）
    OpenLib(L, luaopen_base64);
    OpenLib(L, luaopen_bit);
    OpenLib(L, luaopen_cjson, "cjson");
    OpenLib(L, luaopen_LuaXML_lib);
    OpenLib(L, luaopen_lpeg);
    OpenLib(L, luaopen_struct);
    Preload(L, "socket.core", luaopen_socket_core);
    Preload(L, "mime.core", luaopen_mime_core);
    lua_settop(L, 0);

    // cocos2d-x 扩展与游戏自定义类
    tolua_extensions_ccb_open(L);
    RegisterCCBProxy(L);
    RegisterTableViewEx(L);
    RegisterTextFieldEx(L);
    // 原生接口
    RegisterDataBindings(L);
    RegisterExportBindings(L, env);
    lua_settop(L, 0);

    AddLoader(L);
    RunString(L, kCompat, "=host_compat");
    RegisterAccountCrypto(L);

    // 原版 Setup 里先 require objectlua
    lua_getglobal(L, "require");
    lua_pushstring(L, "objectlua");
    if (!PCall(L, 1, 0)) return false;
    lua_getglobal(L, "require");
    lua_pushstring(L, "objectlua.Mixin");
    if (!PCall(L, 1, 0)) return false;

    lua_getglobal(L, "require");
    lua_pushstring(L, "HostAdapter");
    if (!PCall(L, 1, 0)) {
        Log("require HostAdapter 失败");
        return false;
    }
    Log("HostAdapter 已加载，调用 OnSysStartup");
    // 高屏上 design/frame 比例不是 1，原脚本会把整层缩向左下角。
    // 分辨率仍是 640x960 铺满，这里只把这一层缩放改回 1。
    RunString(L, R"LUA(
local function nudgeButtons(node, depth)
  if depth > 14 or node == nil or node.getChildren == nil then return end
  local okType, kind = pcall(tolua.type, node)
  if okType and (kind == "CCControlButton" or kind == "CCMenuItem" or kind == "CCMenuItemSprite" or kind == "CCMenuItemImage" or kind == "CCMenuItemLabel") then
    local sz = node:getContentSize()
    local bl = node:convertToWorldSpace(ccp(0, 0))
    local tr = node:convertToWorldSpace(ccp(sz.width, sz.height))
    local minx, maxx = math.min(bl.x, tr.x), math.max(bl.x, tr.x)
    local miny, maxy = math.min(bl.y, tr.y), math.max(bl.y, tr.y)
    local dx, dy, m = 0, 0, 20
    if miny < m and miny > -48 then dy = m - miny end
    if maxy > 940 and maxy < 1008 then dy = 940 - maxy end
    if minx < m and minx > -48 then dx = m - minx end
    if maxx > 620 and maxx < 688 then dx = 620 - maxx end
    if dx ~= 0 or dy ~= 0 then
      local x, y = node:getPosition()
      node:setPosition(ccp(x + dx, y + dy))
    end
  end
  local children = node:getChildren()
  if children and children.count then
    for i = 0, children:count() - 1 do
      nudgeButtons(children:objectAtIndex(i), depth + 1)
    end
  end
end

local mod = package.loaded["Tw.Controller"]
if mod and mod.loadAsScene then
  local rawLoad = mod.loadAsScene
  function mod:loadAsScene(ccb, owner)
    local scene = rawLoad(self, ccb, owner)
    if scene then
      local root = scene:getChildByTag(999)
      if root then
        root:setScale(1)
        local ui = root:getChildByTag(999)
        if ui then pcall(nudgeButtons, ui, 0) end
      end
    end
    return scene
  end
end
-- ccbi 的自定义类名是 BattleShowUnit，不是 UI.BattleShowUnit。
-- 贴卡失败不能抛出去，否则开战函数中断，回合动画不会开始。
if not _G.__cardRequireHook then
  _G.__cardRequireHook = true
  local rawRequire = require
  function require(name, ...)
    local loaded = rawRequire(name, ...)
    if (name == "BattleShowUnit" or name == "UI.BattleShowUnit") and type(loaded) == "table" and loaded.prototype and not loaded.prototype.__cardFace and type(loaded.prototype.SetUnitInfo) == "function" then
      local rawSet = loaded.prototype.SetUnitInfo
      function loaded.prototype:SetUnitInfo(info, bShow)
        pcall(rawSet, self, info, bShow)
        pcall(function()
          if not info or not info.model then return end
          local heroMod = Logic.Hero
          local big = heroMod and heroMod.HEROIMG_SIZE and heroMod.HEROIMG_SIZE.BIG or nil
          local hero = Logic:Get("Hero")
          local facePath = hero:GetHeroImage(info.model, big)
          local bgPath, starPath = hero:GetHeroBgImage(info.model, big)
          self:removeChildByTag(8801, true)
          local bg = bgPath and CCSprite:create(bgPath) or nil
          local face = facePath and CCSprite:create(facePath) or nil
          local base = bg or face
          if not base then return end
          local sz = base:getContentSize()
          if bg and face then
            face:setAnchorPoint(ccp(0.5, 0.5))
            face:setPosition(ccp(sz.width * 0.5, sz.height * 0.5))
            bg:addChild(face, 1)
          end
          if starPath and bg then
            local star = CCSprite:create(starPath)
            if star then
              star:setAnchorPoint(ccp(0, 1))
              star:setPosition(ccp(4, sz.height - 4))
              bg:addChild(star, 2)
            end
          end
          local img = self.mImg
          local x, y = 0, 0
          if img then
            x, y = img:getPosition()
            img:setVisible(false)
          end
          if sz.height > 0 then base:setScale(165 / sz.height) end
          base:setAnchorPoint(ccp(0.5, 0.5))
          base:setPosition(ccp(x, y))
          self:addChild(base, 20, 8801)
          self:setVisible(true)
        end)
      end
      loaded.prototype.__cardFace = true
    end
    if (name == "BattleShow" or name == "UI.BattleShow") and type(loaded) == "table" and loaded.prototype and not loaded.prototype.__battlePlay and type(loaded.prototype.Start) == "function" and type(loaded.prototype.PlayRounds) == "function" then
      local rawStart = loaded.prototype.Start
      function loaded.prototype:Start(...)
        local ok = pcall(rawStart, self, ...)
        if ok then return end
        pcall(function()
          Singleton(Timer):After(0, self:Event("BATTLE_WAIT", function()
            RunInCoroutine(function()
              self:NormalInitAction()
              self:PlayRounds()
              self:PlayEnd()
              if self.listener then self.listener() end
            end)
          end))
        end)
      end
      loaded.prototype.__battlePlay = true
    end
    return loaded
  end
end
)LUA", "=scene_scale");
    const bool ok = CallGlobal("OnSysStartup");
    Log("OnSysStartup 返回 %d", ok ? 1 : 0);
    return ok;
}

namespace {

// 调试：UserData/cmd.lua 出现时执行一次并改名为 cmd.done.lua（Windows 调试用，iOS 不启用）
void RunDebugCommand(lua_State* L) {
#if defined(_WIN32) && defined(_DEBUG)
    static std::string path;
    if (path.empty()) {
        lua_getglobal(L, "CVariableSystem");
        lua_pop(L, 1);
        char buf[512] = {};
        GetCurrentDirectoryA(sizeof(buf), buf);
        std::string base = buf;
        const size_t p = base.find_last_of("\\/");
        path = (p == std::string::npos ? base : base.substr(0, p)) + "\\UserData\\cmd.lua";
    }
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return;
    std::string code;
    char tmp[4096];
    size_t n;
    while ((n = fread(tmp, 1, sizeof(tmp), f)) > 0) code.append(tmp, n);
    fclose(f);
    const std::string done = path.substr(0, path.size() - 4) + ".done.lua";
    remove(done.c_str());
    rename(path.c_str(), done.c_str());
    Log("执行调试脚本 cmd.lua（%u 字节）", static_cast<unsigned>(code.size()));
    if (luaL_loadbuffer(L, code.data(), code.size(), "=cmd") != 0) {
        Log("cmd.lua 编译失败：%s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return;
    }
    PCall(L, 0, 0);
#else
    (void)L;
#endif
}

// 调试：模拟点击（设计分辨率坐标，左下角为原点）
int l_DebugTap(lua_State* L) {
    const float x = static_cast<float>(luaL_checknumber(L, 1));
    const float y = static_cast<float>(luaL_checknumber(L, 2));
    CCEGLView* view = CCEGLView::sharedOpenGLView();
    // 设计坐标 -> 视图坐标（左上角原点、未缩放的窗口像素）
    const CCRect vp = view->getViewPortRect();
    const float sx = view->getScaleX(), sy = view->getScaleY();
    const CCSize frame = view->getFrameSize();
    float px = (x * sx + vp.origin.x);
    float py = frame.height - (y * sy + vp.origin.y);
    int id = 0;
    view->handleTouchesBegin(1, &id, &px, &py);
    view->handleTouchesEnd(1, &id, &px, &py);
    Log("DebugTap(%.0f, %.0f) -> view(%.0f, %.0f)", x, y, px, py);
    return 0;
}

// 调试：把字符串写入 client.log（游戏的 print 在发布版里不输出）
int l_HostLog(lua_State* L) {
    Log("[DBG] %s", luaL_checkstring(L, 1));
    return 0;
}

}  // namespace

namespace {
volatile long g_heartbeat = 0;

void TracebackHook(lua_State* L, lua_Debug*) {
    lua_sethook(L, nullptr, 0, 0);
    std::string tb = "主线程卡住时的 Lua 调用栈：";
    lua_Debug ar;
    for (int level = 0; level < 40 && lua_getstack(L, level, &ar); ++level) {
        lua_getinfo(L, "Sln", &ar);
        char line[512];
        snprintf(line, sizeof(line), "\n\t%s:%d %s", ar.short_src, ar.currentline, ar.name ? ar.name : "?");
        tb += line;
    }
    Log("%s", tb.c_str());
}
}  // namespace

long LuaHostHeartbeat() { return g_heartbeat; }

void LuaHostRequestTraceback() {
    if (lua_State* L = GetLuaState()) lua_sethook(L, TracebackHook, LUA_MASKCOUNT, 1);
}

void LuaHostTick() {
    ++g_heartbeat;
    static int ticks = 0;
    if (ticks < 3) Log("tick %d", ticks);
    if (ticks == 0) {
        lua_register(GetLuaState(), "DebugTap", l_DebugTap);
        lua_register(GetLuaState(), "HostLog", l_HostLog);
    }
    ++ticks;
    NetPoll();
    LuaExportTick(GetLuaState());
    CallGlobal("OnProcess");
    if (ticks % 30 == 0) RunDebugCommand(GetLuaState());
}

void LuaHostShutdown() {
    CallGlobal("OnSysShutdown");
    SaveSystemVariables();
    NetShutdown();
}

}  // namespace host
