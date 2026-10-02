#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
在 macOS（Codemagic 云主机）上编译 iOS 版客户端，产出未签名 IPA（之后用 Sideloadly 签名安装）。

不依赖 Xcode 工程：直接用 xcrun clang 按文件并行编译，引擎部分打成静态库（链接时只取用到的目标文件），
再链接成 arm64 可执行文件，拷贝资源、写 Info.plist、编译图标，最后打包 build/ios/TTAXY.ipa。

用法：
  python3 tools/build_ios.py            编译 + 打包
  python3 tools/build_ios.py --list     只列出源文件（Windows 上也能跑，用来核对文件清单）
输出：build/ios/TTAXY.ipa、build/ios/build.log（完整日志）、build/ios/errors.txt（只有错误）
"""
import concurrent.futures
import glob
import os
import plistlib
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENG = os.path.join(ROOT, 'engine', 'cocos2d-x-2.2.6')
CLIENT = os.path.join(ROOT, 'client')
TP = os.path.join(CLIENT, 'thirdparty')
IOS = os.path.join(CLIENT, 'proj.ios')
LUA = os.path.join(ENG, 'scripting', 'lua', 'lua')
LUASUP = os.path.join(ENG, 'scripting', 'lua', 'cocos2dx_support')
OUT = os.path.join(ROOT, 'build', 'ios')
OBJ = os.path.join(OUT, 'obj')

APP = 'TTAXY'                                    # 可执行文件 / .app 名（英文，避免路径问题）
BUNDLE_ID = os.environ.get('TTAXY_BUNDLE_ID', 'com.twmobile.ttaxy')
DISPLAY_NAME = os.environ.get('TTAXY_DISPLAY_NAME', '哈基米西游')
VERSION_NAME = '1.0.8.1'                         # 和 AppDelegate.cpp 的 kVersionName 一致
BUILD_NUMBER = '52'                              # 和 kPackageVersion 一致
ARCH = 'armv7'                                   # 32 位。iOS 11 起的手机装不上
MIN_IOS = '10.0'                                  # 32 位能声明的最后系统版本


def rel(p):
    return os.path.relpath(p, ROOT).replace('\\', '/')


def g(*parts):
    return sorted(glob.glob(os.path.join(*parts)))


# ---------------------------------------------------------------- 源文件清单

def cocos_sources():
    c = os.path.join(ENG, 'cocos2dx')
    files = g(c, '*.cpp') + [os.path.join(c, 'ccFPSImages.c')]
    for d in ('actions', 'base_nodes', 'cocoa', 'draw_nodes', 'effects', 'keypad_dispatcher', 'label_nodes',
              'layers_scenes_transitions_nodes', 'menu_nodes', 'misc_nodes', 'particle_nodes', 'script_support',
              'shaders', 'sprite_nodes', 'text_input_node', 'textures', 'tileMap_parallax_nodes', 'touch_dispatcher'):
        files += [p for p in g(c, d, '*.cpp') if not p.endswith('CCGLBufferedNode.cpp')]  # 引擎 iOS 工程也不编它
    for dp, _, fns in os.walk(os.path.join(c, 'support')):
        files += [os.path.join(dp, f) for f in sorted(fns) if f.endswith(('.cpp', '.mm'))]
    files += g(c, 'kazmath', 'src', '*.c') + g(c, 'kazmath', 'src', 'GL', '*.c')
    files += [os.path.join(c, 'platform', f) for f in
              ('CCEGLViewProtocol.cpp', 'CCFileUtils.cpp', 'CCImageCommonWebp.cpp', 'CCSAXParser.cpp', 'platform.cpp')]
    # iOS 平台层。用了已废弃 API 的几个文件换成 proj.ios 里的实现：
    #   加速计（UIAccelerometer）-> CCAccelerometer_ios.mm；CCCommon（UIAlertView）-> CCCommon_ios.mm
    skip = {'CCAccelerometer.mm', 'AccelerometerDelegateWrapper.mm', 'CCCommon.mm'}
    files += [p for p in g(c, 'platform', 'ios', '*.*')
              if p.endswith(('.mm', '.m', '.cpp')) and os.path.basename(p) not in skip]
    return files


def extension_sources():
    """按 win32 工程（libExtensions.vcxproj）的清单取 CCBReader / GUI / CocoStudio，把 Windows 的输入框换成 iOS 版"""
    import re
    vcx = os.path.join(ENG, 'extensions', 'proj.win32', 'libExtensions.vcxproj')
    txt = open(vcx, encoding='utf-8', errors='replace').read()
    base = os.path.dirname(vcx)
    files = []
    for f in re.findall(r'<ClCompile Include="([^"]+)"', txt):
        p = os.path.normpath(os.path.join(base, f.replace('\\', '/')))
        r = os.path.relpath(p, os.path.join(ENG, 'extensions')).replace('\\', '/')
        if r.startswith(('CCBReader/', 'GUI/', 'CocoStudio/')) and not r.endswith('CCEditBoxImplWin.cpp'):
            files.append(p)
    files.append(os.path.join(ENG, 'extensions', 'GUI', 'CCEditBox', 'CCEditBoxImplIOS.mm'))
    return files


def luabind_sources():
    files = [os.path.join(LUASUP, f) for f in (
        'CCBProxy.cpp', 'CCLuaBridge.cpp', 'CCLuaEngine.cpp', 'CCLuaStack.cpp', 'CCLuaValue.cpp',
        'Cocos2dxLuaLoader.cpp', 'LuaCocos2d.cpp', 'LuaCocoStudio.cpp', 'lua_cocos2dx_cocostudio_manual.cpp',
        'lua_cocos2dx_extensions_manual.cpp', 'lua_cocos2dx_manual.cpp', 'Lua_extensions_CCB.cpp', 'tolua_fix.c')]
    files.append(os.path.join(LUASUP, 'platform', 'ios', 'CCLuaObjcBridge.mm'))
    files += g(ENG, 'scripting', 'lua', 'tolua', '*.c')
    files.append(os.path.join(ENG, 'scripting', 'lua', 'xxtea', 'xxtea.cpp'))
    return files


def lua_sources():
    return [p for p in g(LUA, '*.c') if os.path.basename(p) not in ('lua.c', 'luac.c', 'print.c')]


def thirdparty_sources():
    files = [os.path.join(TP, 'lbase64', 'lbase64.c'), os.path.join(TP, 'luabitop', 'bit.c'),
             os.path.join(TP, 'lua-cjson', 'lua_cjson.c'), os.path.join(TP, 'lua-cjson', 'strbuf.c'),
             os.path.join(TP, 'lua-cjson', 'fpconv.c'), os.path.join(TP, 'struct', 'struct.c'),
             os.path.join(TP, 'luaxml', 'LuaXML_lib.c'), os.path.join(TP, 'quicklz', 'quicklz.c')]
    files += g(TP, 'lpeg', '*.c')
    ls = os.path.join(TP, 'luasocket', 'src')
    files += [os.path.join(ls, f) for f in ('auxiliar.c', 'buffer.c', 'except.c', 'inet.c', 'io.c', 'luasocket.c',
                                            'mime.c', 'options.c', 'select.c', 'tcp.c', 'timeout.c', 'udp.c',
                                            'usocket.c')]
    return files


def mbedtls_sources():
    return g(TP, 'mbedtls', 'library', '*.c')


def app_sources():
    files = [os.path.join(CLIENT, 'Classes', 'AppDelegate.cpp')]
    files += [p for p in g(CLIENT, 'Classes', 'host', '*.cpp') if not p.endswith('Platform_win32.cpp')]
    files += g(CLIENT, 'Classes', 'host', '*.mm')
    files += g(IOS, '*.m') + g(IOS, '*.mm')
    return files


# 静态库（链接时只取被引用的目标文件）和直接链接的目标文件
GROUPS = [
    ('cocos2d', cocos_sources, 'engine'),
    ('extensions', extension_sources, 'engine'),
    ('luabind', luabind_sources, 'engine'),
    ('lua', lua_sources, 'c'),
    ('thirdparty', thirdparty_sources, 'c'),
    ('mbedtls', mbedtls_sources, 'mbedtls'),
    ('app', app_sources, 'app'),
]

INCLUDES = [
    os.path.join(CLIENT, 'Classes'), os.path.join(CLIENT, 'Classes', 'host'), IOS,
    LUA,  # 标准 Lua 5.1 头文件放最前（不用引擎的 LuaJIT）
    os.path.join(ENG, 'scripting', 'lua', 'tolua'), LUASUP, os.path.join(LUASUP, 'platform', 'ios'),
    os.path.join(ENG, 'scripting', 'lua', 'xxtea'),
    os.path.join(ENG, 'cocos2dx'), os.path.join(ENG, 'cocos2dx', 'include'),
    os.path.join(ENG, 'cocos2dx', 'kazmath', 'include'), os.path.join(ENG, 'cocos2dx', 'platform', 'ios'),
    os.path.join(ENG, 'cocos2dx', 'platform', 'ios', 'Simulation'),
    os.path.join(ENG, 'cocos2dx', 'platform', 'third_party', 'ios'),
    os.path.join(ENG, 'cocos2dx', 'platform', 'third_party', 'ios', 'webp'),
    os.path.join(ENG, 'extensions'), ENG, os.path.join(ENG, 'CocosDenshion', 'include'),
    os.path.join(TP, 'quicklz'), os.path.join(TP, 'lua-cjson'), os.path.join(TP, 'lpeg'),
    os.path.join(TP, 'mbedtls', 'include'),
]
DEFINES = ['CC_TARGET_OS_IPHONE', 'USE_FILE32API', 'NDEBUG', 'COCOS2D_DEBUG=0', 'GLES_SILENCE_DEPRECATION']


def headermap_dirs():
    """模拟 Xcode 的 headermap：引擎源码里大量 #include "CCGeometry.h" 这种不带子目录的写法，
    Xcode 会在整个工程的头文件里找。这里把 cocos2dx 下所有含头文件的目录加到 -idirafter（优先级最低，
    不会盖掉前面的正常包含路径）。跳过其它平台和 wp8/winrt 的目录，避免拿到同名的错误头文件。"""
    c = os.path.join(ENG, 'cocos2dx')
    other = {'android', 'win32', 'linux', 'mac', 'blackberry', 'emscripten', 'nacl', 'tizen', 'winrt', 'wp8',
             'wp8-xaml', 'marmalade', 'bada', 'qnx', 'third_party'}
    dirs = []
    for dp, dns, fns in os.walk(c):
        r = os.path.relpath(dp, c).replace('\\', '/')
        parts = r.split('/')
        if parts[0].startswith('proj.') or parts[0] == 'kazmath' or 'precompiled' in parts or \
                (parts[0] == 'platform' and len(parts) > 1 and parts[1] in other):
            dns[:] = []
            continue
        dns.sort()
        if any(f.endswith('.h') for f in fns):
            dirs.append(dp)
    return dirs


HEADERMAP = headermap_dirs()
# 相当于引擎 iOS 工程的 cocos2dx-Prefix.pch（Objective-C 文件默认带上 Foundation）
PREFIX_HEADER = os.path.join(OUT, 'Prefix.pch')
PREFIX_TEXT = '#ifdef __OBJC__\n#import <Foundation/Foundation.h>\n#endif\n'

FRAMEWORKS = ['Foundation', 'UIKit', 'CoreGraphics', 'CoreText', 'QuartzCore', 'OpenGLES', 'AVFoundation',
              'AudioToolbox', 'CoreFoundation', 'Security']


# ---------------------------------------------------------------- 编译

class Tool:
    def __init__(self):
        self.sdk = self.run_out(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'])
        self.sdk_ver = self.run_out(['xcrun', '--sdk', 'iphoneos', '--show-sdk-version'])
        self.cc = self.run_out(['xcrun', '--sdk', 'iphoneos', '-f', 'clang'])
        self.cxx = self.run_out(['xcrun', '--sdk', 'iphoneos', '-f', 'clang++'])
        self.xcode = self.run_out(['xcodebuild', '-version']).replace('\n', ' ')

    @staticmethod
    def run_out(args):
        return subprocess.run(args, capture_output=True, text=True, check=True).stdout.strip()


def obj_path(src):
    return os.path.join(OBJ, rel(src).replace('/', '_').replace('.', '_') + '.o')


def compile_args(tool, src, kind):
    ext = os.path.splitext(src)[1]
    base = ['-arch', ARCH, '-isysroot', tool.sdk, '-miphoneos-version-min=' + MIN_IOS, '-O2', '-c',
            '-fno-objc-arc', '-fmessage-length=0', '-w']
    base += ['-I' + i for i in INCLUDES] + ['-idirafter' + i for i in HEADERMAP] + ['-D' + d for d in DEFINES]
    if ext in ('.m', '.mm'):
        base += ['-include', PREFIX_HEADER]
    if kind == 'mbedtls':
        base.append('-I' + os.path.join(TP, 'mbedtls', 'library'))
    if ext in ('.c', '.m'):
        cc = tool.cc
        # 老 C 代码：新版 clang 默认当错误的几类问题降为警告
        base += ['-std=gnu99', '-Wno-error=implicit-function-declaration', '-Wno-error=int-conversion',
                 '-Wno-error=incompatible-function-pointer-types', '-Wno-error=incompatible-pointer-types']
    else:
        cc = tool.cxx
        # 引擎是 2014 年的 C++03/11 代码；游戏宿主层用 C++17
        std = '-std=gnu++17' if kind == 'app' else '-std=gnu++11'
        base += [std, '-stdlib=libc++', '-Wno-c++11-narrowing', '-Wno-register', '-Wno-reserved-user-defined-literal']
    if ext == '.m':
        base = ['-x', 'objective-c'] + base
    elif ext == '.mm':
        base = ['-x', 'objective-c++'] + base
    return [cc] + base + ['-o', obj_path(src), src]


def ar_members(path):
    """解析 ar 静态库（BSD / GNU 两种成员名格式），返回 [(成员名, 内容)]，跳过符号表"""
    data = open(path, 'rb').read()
    if data[:8] != b'!<arch>\n':
        raise ValueError('不是 ar 归档：' + path)
    pos, out = 8, []
    while pos + 60 <= len(data):
        hdr = data[pos:pos + 60]
        name = hdr[:16].decode('latin-1').strip()
        size = int(hdr[48:58].decode('latin-1').strip())
        body = data[pos + 60:pos + 60 + size]
        pos += 60 + size + (size & 1)
        if name.startswith('#1/'):                      # BSD：长名字放在内容开头
            n = int(name[3:])
            name, body = body[:n].rstrip(b'\0').decode('latin-1'), body[n:]
        elif name.endswith('/') and name not in ('/', '//'):
            name = name[:-1]
        if name.startswith('__.SYMDEF') or name in ('', '/', '//'):
            continue
        out.append((name, body))
    return out


def realigned_lib(tool, src, name):
    """引擎自带的老静态库（2014 年编的 libwebp.a）成员没有 8 字节对齐，新版 ld 直接拒绝。
    取出 32 位目标文件（同名成员加序号区分，避免 ar -x 互相覆盖），再用 libtool 重新打包。"""
    work = os.path.join(OUT, name + '_fix')
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    thin = os.path.join(work, 'thin.a')
    p = subprocess.run(['xcrun', 'lipo', src, '-thin', ARCH, '-output', thin], capture_output=True, text=True)
    if p.returncode != 0:          # 不是多架构库，直接用
        shutil.copy2(src, thin)
    objs = []
    for i, (mname, body) in enumerate(ar_members(thin)):
        if body[:4] != b'\xce\xfa\xed\xfe':               # 只要 32 位 Mach-O
            continue
        o = os.path.join(work, '%03d_%s' % (i, os.path.basename(mname)))
        open(o, 'wb').write(body)
        objs.append(o)
    out = os.path.join(OUT, 'lib%s.a' % name)
    subprocess.run(['xcrun', 'libtool', '-static', '-no_warning_for_no_symbols', '-o', out] + objs,
                   check=True, capture_output=True)
    return out, len(objs)


def compile_one(tool, src, kind):
    o = obj_path(src)
    if os.path.exists(o) and os.path.getmtime(o) >= os.path.getmtime(src):
        return src, 0, ''
    p = subprocess.run(compile_args(tool, src, kind), capture_output=True, text=True)
    return src, p.returncode, (p.stdout + p.stderr)


def main():
    if '--list' in sys.argv:
        for name, fn, kind in GROUPS:
            files = fn()
            missing = [f for f in files if not os.path.exists(f)]
            print('==== %s (%d, 缺失 %d)' % (name, len(files), len(missing)))
            for f in files:
                print(('  ' if f not in missing else '  [缺失] ') + rel(f))
        return 0

    os.makedirs(OBJ, exist_ok=True)
    open(PREFIX_HEADER, 'w').write(PREFIX_TEXT)
    log = open(os.path.join(OUT, 'build.log'), 'w', encoding='utf-8')
    errs = open(os.path.join(OUT, 'errors.txt'), 'w', encoding='utf-8')

    def say(*a):
        s = ' '.join(str(x) for x in a)
        print(s, flush=True)
        log.write(s + '\n')
        log.flush()

    tool = Tool()
    say('Xcode:', tool.xcode, '| iPhoneOS SDK', tool.sdk_ver)
    t0 = time.time()
    failed = []
    libs = []
    app_objs = []
    jobs = max(2, os.cpu_count() or 4)
    for name, fn, kind in GROUPS:
        files = fn()
        missing = [f for f in files if not os.path.exists(f)]
        if missing:
            say('[%s] 缺少源文件：' % name + ', '.join(rel(m) for m in missing))
            failed += missing
            files = [f for f in files if f not in missing]
        with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
            results = list(ex.map(lambda s: compile_one(tool, s, kind), files))
        bad = [r for r in results if r[1] != 0]
        say('[%s] %d 个文件，失败 %d' % (name, len(files), len(bad)))
        for src, rc, out in results:
            if out.strip():
                log.write('---- %s\n%s\n' % (rel(src), out))
        for src, rc, out in bad:
            failed.append(src)
            lines = [l for l in out.splitlines() if ' error:' in l or 'fatal error' in l]
            errs.write('==== %s\n%s\n' % (rel(src), '\n'.join(lines[:30]) or out[-3000:]))
        errs.flush()
        objs = [obj_path(f) for f in files]
        if kind == 'app':
            app_objs = objs
        else:
            lib = os.path.join(OUT, 'lib%s.a' % name)
            if os.path.exists(lib):
                os.remove(lib)
            good = [o for o in objs if os.path.exists(o)]
            subprocess.run(['xcrun', 'libtool', '-static', '-no_warning_for_no_symbols', '-o', lib] + good,
                           check=True, capture_output=True)
            libs.append(lib)
    say('编译用时 %.0fs' % (time.time() - t0))
    if failed:
        say('编译失败 %d 个文件，详见 errors.txt：' % len(failed))
        say(open(os.path.join(OUT, 'errors.txt'), encoding='utf-8').read()[:20000])
        return 1

    # ---------------------------------------------------------------- 链接
    app_dir = os.path.join(OUT, 'Payload', APP + '.app')
    shutil.rmtree(os.path.join(OUT, 'Payload'), ignore_errors=True)
    os.makedirs(app_dir)
    exe = os.path.join(app_dir, APP)
    webp, n = realigned_lib(tool, os.path.join(ENG, 'cocos2dx', 'platform', 'third_party', 'ios', 'libraries',
                                               'libwebp.a'), 'webp')
    say('libwebp.a 重新打包：%d 个 %s 目标文件' % (n, ARCH))
    args = [tool.cxx, '-arch', ARCH, '-isysroot', tool.sdk, '-miphoneos-version-min=' + MIN_IOS,
            '-stdlib=libc++', '-ObjC', '-dead_strip', '-o', exe] + app_objs + libs + [webp, '-lz']
    for f in FRAMEWORKS:
        args += ['-framework', f]
    p = subprocess.run(args, capture_output=True, text=True)
    log.write('---- link\n' + p.stdout + p.stderr + '\n')
    if p.returncode != 0:
        # 新版 ld 的报错格式各不相同，直接保留全部输出（去掉纯警告行）
        lines = [l for l in (p.stdout + p.stderr).splitlines() if l.strip() and not l.startswith('ld: warning')]
        errs.write('==== link\n' + '\n'.join(lines[:200]) + '\n')
        errs.close()
        say('链接失败：')
        say('\n'.join(lines[:200]))
        return 1
    say('链接完成，可执行文件 %.1f MB' % (os.path.getsize(exe) / 1048576))

    # 去掉符号表（函数名、类名），增加逆向难度；保留一份未 strip 的，出崩溃时用来对照地址
    shutil.copy2(exe, os.path.join(OUT, APP + '.unstripped'))
    p = subprocess.run(['xcrun', 'strip', exe], capture_output=True, text=True)
    say('strip rc=%d，剩 %.1f MB %s' % (p.returncode, os.path.getsize(exe) / 1048576, (p.stdout + p.stderr).strip()[:300]))

    # ---------------------------------------------------------------- 资源
    res = os.path.join(CLIENT, 'Resources')
    for name in os.listdir(res):
        s = os.path.join(res, name)
        if name.startswith('_export_report'):
            continue
        if os.path.isdir(s):
            shutil.copytree(s, os.path.join(app_dir, name))
        else:
            shutil.copy2(s, app_dir)
    say('资源拷贝完成')

    info = {
        'CFBundleDevelopmentRegion': 'zh_CN',
        'CFBundleDisplayName': DISPLAY_NAME,
        'CFBundleName': DISPLAY_NAME,
        'CFBundleExecutable': APP,
        'CFBundleIdentifier': BUNDLE_ID,
        'CFBundleInfoDictionaryVersion': '6.0',
        'CFBundlePackageType': 'APPL',
        'CFBundleShortVersionString': VERSION_NAME,
        'CFBundleVersion': BUILD_NUMBER,
        'CFBundleSupportedPlatforms': ['iPhoneOS'],
        'DTPlatformName': 'iphoneos',
        'DTSDKName': 'iphoneos' + tool.sdk_ver,
        'DTPlatformVersion': tool.sdk_ver,
        'LSRequiresIPhoneOS': True,
        'MinimumOSVersion': MIN_IOS,
        'UIDeviceFamily': [1, 2],
        'UIRequiredDeviceCapabilities': [ARCH],
        'UIRequiresFullScreen': True,
        'UIStatusBarHidden': True,
        'UIViewControllerBasedStatusBarAppearance': True,
        'UISupportedInterfaceOrientations': ['UIInterfaceOrientationPortrait'],
        'UISupportedInterfaceOrientations~ipad': ['UIInterfaceOrientationPortrait',
                                                  'UIInterfaceOrientationPortraitUpsideDown'],
        # 有启动屏配置才会按全面屏原生分辨率运行（否则是放大的兼容模式）
        'UILaunchScreen': {},
        'UIAppFonts': ['fonts/YaHei.ttf'],
        # 游戏用 HTTP 访问服务器
        'NSAppTransportSecurity': {'NSAllowsArbitraryLoads': True},
        # Documents（日志 client.log、存档）可以在“文件”App 里看到，方便排查问题
        'UIFileSharingEnabled': True,
        'LSSupportsOpeningDocumentsInPlace': True,
        'ITSAppUsesNonExemptEncryption': False,
    }

    # 图标：优先用 actool 编译 Assets.xcassets；失败时退回 CFBundleIconFiles
    icons_ok = False
    xcassets = os.path.join(IOS, 'Assets.xcassets')
    if os.path.isdir(xcassets):
        partial = os.path.join(OUT, 'assets_partial.plist')
        p = subprocess.run(['xcrun', 'actool', xcassets, '--compile', app_dir, '--platform', 'iphoneos',
                            '--minimum-deployment-target', MIN_IOS, '--app-icon', 'AppIcon',
                            '--target-device', 'iphone', '--target-device', 'ipad',
                            '--output-partial-info-plist', partial, '--output-format', 'human-readable-text'],
                           capture_output=True, text=True)
        log.write('---- actool\n' + p.stdout + p.stderr + '\n')
        if p.returncode == 0 and os.path.exists(partial):
            info.update(plistlib.load(open(partial, 'rb')))
            icons_ok = True
    if not icons_ok:
        names = []
        for f in g(IOS, 'icons', '*.png'):
            shutil.copy2(f, app_dir)
            names.append(os.path.splitext(os.path.basename(f))[0].split('@')[0].split('~')[0])
        info['CFBundleIcons'] = {'CFBundlePrimaryIcon': {'CFBundleIconFiles': sorted(set(names))}}
    say('图标：' + ('actool' if icons_ok else 'CFBundleIconFiles'))

    plistlib.dump(info, open(os.path.join(app_dir, 'Info.plist'), 'wb'), fmt=plistlib.FMT_BINARY)
    open(os.path.join(app_dir, 'PkgInfo'), 'w').write('APPL????')

    # 临时签名（ad-hoc），Sideloadly 安装时会用你的 Apple ID 重新签名
    p = subprocess.run(['codesign', '--force', '--sign', '-', '--timestamp=none', app_dir], capture_output=True, text=True)
    say('ad-hoc 签名 rc=%d %s' % (p.returncode, (p.stdout + p.stderr).strip()[:500]))

    ipa = os.path.join(OUT, APP + '.ipa')
    if os.path.exists(ipa):
        os.remove(ipa)
    subprocess.run(['zip', '-qry', ipa, 'Payload'], cwd=OUT, check=True)
    say('IPA：%s（%.1f MB），总用时 %.0fs' % (rel(ipa), os.path.getsize(ipa) / 1048576, time.time() - t0))
    say('BUILD_IOS_OK')
    log.close()
    errs.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
