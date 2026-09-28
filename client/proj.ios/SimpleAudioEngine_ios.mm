// CocosDenshion::SimpleAudioEngine 的 iOS 实现（AVFoundation）。
// 引擎自带的 iOS 版基于 OpenAL + AudioSession C API，这两个在新 SDK 里已废弃，这里改用 AVAudioPlayer。
// 游戏的音乐和音效都是 mp3。
#import <AVFoundation/AVFoundation.h>

#include <map>
#include <string>

#include "SimpleAudioEngine.h"
#include "platform/CCFileUtils.h"

using namespace CocosDenshion;

namespace {

NSString* FullPath(const char* path) {
    if (!path || !*path) return nil;
    std::string full = cocos2d::CCFileUtils::sharedFileUtils()->fullPathForFilename(path);
    return [NSString stringWithUTF8String:full.c_str()];
}

AVAudioPlayer* g_music = nil;
std::string g_musicPath;
float g_musicVolume = 1.0f;
float g_effectsVolume = 1.0f;

// 预加载的音效数据（路径 -> NSData）
NSMutableDictionary* g_effectData = nil;
// 正在播放的音效（id -> AVAudioPlayer）
NSMutableDictionary* g_effects = nil;
unsigned int g_nextEffectId = 1;

NSData* EffectData(NSString* full) {
    if (!full) return nil;
    if (!g_effectData) g_effectData = [[NSMutableDictionary alloc] init];
    NSData* d = [g_effectData objectForKey:full];
    if (!d) {
        d = [NSData dataWithContentsOfFile:full];
        if (d) [g_effectData setObject:d forKey:full];
    }
    return d;
}

// 回收播完的音效播放器
void PruneEffects() {
    if (!g_effects) return;
    NSMutableArray* done = [NSMutableArray array];
    for (NSNumber* k in g_effects) {
        AVAudioPlayer* p = [g_effects objectForKey:k];
        if (!p.playing && p.numberOfLoops == 0 && p.currentTime == 0) [done addObject:k];
    }
    [g_effects removeObjectsForKeys:done];
}

}  // namespace

SimpleAudioEngine::SimpleAudioEngine() {}
SimpleAudioEngine::~SimpleAudioEngine() {}

SimpleAudioEngine* SimpleAudioEngine::sharedEngine() {
    static SimpleAudioEngine s_engine;
    return &s_engine;
}

void SimpleAudioEngine::end() {
    [g_music stop];
    [g_music release];
    g_music = nil;
    g_musicPath.clear();
    for (NSNumber* k in g_effects) [[g_effects objectForKey:k] stop];
    [g_effects release];
    g_effects = nil;
    [g_effectData release];
    g_effectData = nil;
}

void SimpleAudioEngine::preloadBackgroundMusic(const char* pszFilePath) {
    NSString* full = FullPath(pszFilePath);
    if (!full) return;
    if (g_music && g_musicPath == pszFilePath) return;
    AVAudioPlayer* p = [[AVAudioPlayer alloc] initWithContentsOfURL:[NSURL fileURLWithPath:full] error:nil];
    if (!p) return;
    [g_music stop];
    [g_music release];
    g_music = p;
    g_musicPath = pszFilePath;
    g_music.volume = g_musicVolume;
    [g_music prepareToPlay];
}

void SimpleAudioEngine::playBackgroundMusic(const char* pszFilePath, bool bLoop) {
    preloadBackgroundMusic(pszFilePath);
    if (!g_music) return;
    g_music.numberOfLoops = bLoop ? -1 : 0;
    g_music.currentTime = 0;
    g_music.volume = g_musicVolume;
    [g_music play];
}

void SimpleAudioEngine::stopBackgroundMusic(bool bReleaseData) {
    [g_music stop];
    g_music.currentTime = 0;
    if (bReleaseData) {
        [g_music release];
        g_music = nil;
        g_musicPath.clear();
    }
}

void SimpleAudioEngine::pauseBackgroundMusic() { [g_music pause]; }

void SimpleAudioEngine::resumeBackgroundMusic() {
    // 只恢复“暂停”的音乐（停止后 currentTime 为 0，不自动重播）
    if (g_music && !g_music.playing && g_music.currentTime > 0) [g_music play];
}

void SimpleAudioEngine::rewindBackgroundMusic() {
    if (!g_music) return;
    g_music.currentTime = 0;
    [g_music play];
}

bool SimpleAudioEngine::willPlayBackgroundMusic() { return g_music != nil; }

bool SimpleAudioEngine::isBackgroundMusicPlaying() { return g_music && g_music.playing; }

float SimpleAudioEngine::getBackgroundMusicVolume() { return g_musicVolume; }

void SimpleAudioEngine::setBackgroundMusicVolume(float volume) {
    g_musicVolume = volume < 0 ? 0 : (volume > 1 ? 1 : volume);
    g_music.volume = g_musicVolume;
}

float SimpleAudioEngine::getEffectsVolume() { return g_effectsVolume; }

void SimpleAudioEngine::setEffectsVolume(float volume) {
    g_effectsVolume = volume < 0 ? 0 : (volume > 1 ? 1 : volume);
    for (NSNumber* k in g_effects) [[g_effects objectForKey:k] setVolume:g_effectsVolume];
}

unsigned int SimpleAudioEngine::playEffect(const char* pszFilePath, bool bLoop) {
    NSData* d = EffectData(FullPath(pszFilePath));
    if (!d) return 0;
    AVAudioPlayer* p = [[AVAudioPlayer alloc] initWithData:d error:nil];
    if (!p) return 0;
    if (!g_effects) g_effects = [[NSMutableDictionary alloc] init];
    PruneEffects();
    p.numberOfLoops = bLoop ? -1 : 0;
    p.volume = g_effectsVolume;
    [p play];
    const unsigned int sid = g_nextEffectId++;
    [g_effects setObject:p forKey:[NSNumber numberWithUnsignedInt:sid]];
    [p release];
    return sid;
}

void SimpleAudioEngine::pauseEffect(unsigned int nSoundId) {
    [[g_effects objectForKey:[NSNumber numberWithUnsignedInt:nSoundId]] pause];
}

void SimpleAudioEngine::pauseAllEffects() {
    for (NSNumber* k in g_effects) [[g_effects objectForKey:k] pause];
}

void SimpleAudioEngine::resumeEffect(unsigned int nSoundId) {
    [[g_effects objectForKey:[NSNumber numberWithUnsignedInt:nSoundId]] play];
}

void SimpleAudioEngine::resumeAllEffects() {
    for (NSNumber* k in g_effects) {
        AVAudioPlayer* p = [g_effects objectForKey:k];
        if (!p.playing && p.currentTime > 0) [p play];
    }
}

void SimpleAudioEngine::stopEffect(unsigned int nSoundId) {
    NSNumber* k = [NSNumber numberWithUnsignedInt:nSoundId];
    [[g_effects objectForKey:k] stop];
    [g_effects removeObjectForKey:k];
}

void SimpleAudioEngine::stopAllEffects() {
    for (NSNumber* k in g_effects) [[g_effects objectForKey:k] stop];
    [g_effects removeAllObjects];
}

void SimpleAudioEngine::preloadEffect(const char* pszFilePath) { EffectData(FullPath(pszFilePath)); }

void SimpleAudioEngine::unloadEffect(const char* pszFilePath) {
    NSString* full = FullPath(pszFilePath);
    if (full) [g_effectData removeObjectForKey:full];
}
