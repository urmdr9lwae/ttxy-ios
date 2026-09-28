/****************************************************************************
 Copyright (c) 2013 cocos2d-x.org
 
 http://www.cocos2d-x.org
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/

#include "CCTextureETC.h"
#include "platform/CCPlatformConfig.h"
#include "platform/CCFileUtils.h"
#include "CCGL.h"
#include "shaders/ccGLStateCache.h"
#include <string.h>

#if (CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID)
#include "platform/android/jni/JniHelper.h"
#endif


NS_CC_BEGIN

CCTextureETC::CCTextureETC()
: _name(0)
, _width(0)
, _height(0)
{}

CCTextureETC::~CCTextureETC()
{
}

#if (CC_TARGET_PLATFORM != CC_PLATFORM_ANDROID)
// TwMobile: platforms without GL_OES_compressed_ETC1_RGB8_texture (iOS / Windows) decode PKM (ETC1) to RGB888 in software
namespace {

const int kEtc1Modifiers[8][4] = {
    {2, 8, -2, -8},       {5, 17, -5, -17},     {9, 29, -9, -29},     {13, 42, -13, -42},
    {18, 60, -18, -60},   {24, 80, -24, -80},   {33, 106, -33, -106}, {47, 183, -47, -183}};

inline unsigned char Clamp255(int v) { return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v)); }
inline int Ext4(int v) { return (v << 4) | v; }
inline int Ext5(int v) { return (v << 3) | (v >> 2); }

// Decode one 4x4 block (8 bytes, big endian) into out (RGB888, row stride in bytes)
void DecodeEtc1Block(const unsigned char* b, unsigned char* out, int stride, int maxW, int maxH)
{
    const unsigned int low = ((unsigned int)b[4] << 24) | ((unsigned int)b[5] << 16) | ((unsigned int)b[6] << 8) | b[7];
    const bool diff = (b[3] & 2) != 0;
    const bool flip = (b[3] & 1) != 0;
    int c1[3], c2[3];
    for (int i = 0; i < 3; ++i)
    {
        if (diff)
        {
            const int base = b[i] >> 3;
            int d = b[i] & 7;
            if (d >= 4) d -= 8;
            c1[i] = Ext5(base);
            c2[i] = Ext5((base + d) & 31);
        }
        else
        {
            c1[i] = Ext4(b[i] >> 4);
            c2[i] = Ext4(b[i] & 15);
        }
    }
    const int* t1 = kEtc1Modifiers[b[3] >> 5];
    const int* t2 = kEtc1Modifiers[(b[3] >> 2) & 7];
    for (int x = 0; x < 4 && x < maxW; ++x)
    {
        for (int y = 0; y < 4 && y < maxH; ++y)
        {
            const int idx = x * 4 + y;  // pixel indices are column-major
            const int sel = (((low >> (idx + 16)) & 1) << 1) | ((low >> idx) & 1);
            const bool second = flip ? (y >= 2) : (x >= 2);
            const int* c = second ? c2 : c1;
            const int m = (second ? t2 : t1)[sel];
            unsigned char* p = out + y * stride + x * 3;
            p[0] = Clamp255(c[0] + m);
            p[1] = Clamp255(c[1] + m);
            p[2] = Clamp255(c[2] + m);
        }
    }
}

}  // namespace
#endif

bool CCTextureETC::initWithFile(const char *file)
{
#if (CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID)
    bool ret = loadTexture(CCFileUtils::sharedFileUtils()->fullPathForFilename(file).c_str());
    return ret;
#else
    unsigned long size = 0;
    unsigned char* data = CCFileUtils::sharedFileUtils()->getFileData(file, "rb", &size);
    if (!data)
    {
        return false;
    }
    bool ok = false;
    // PKM header: 'PKM ' '10' type(2) encodedW(2) encodedH(2) origW(2) origH(2), big endian
    if (size >= 16 && memcmp(data, "PKM ", 4) == 0)
    {
        const unsigned int encW = (data[8] << 8) | data[9];
        const unsigned int encH = (data[10] << 8) | data[11];
        const unsigned int blocksX = (encW + 3) / 4, blocksY = (encH + 3) / 4;
        if (encW > 0 && encH > 0 && encW <= 8192 && encH <= 8192 && size >= 16 + (unsigned long)blocksX * blocksY * 8)
        {
            const int stride = (int)encW * 3;
            unsigned char* rgb = new unsigned char[(size_t)stride * encH];
            const unsigned char* blk = data + 16;
            for (unsigned int by = 0; by < blocksY; ++by)
            {
                for (unsigned int bx = 0; bx < blocksX; ++bx, blk += 8)
                {
                    DecodeEtc1Block(blk, rgb + (by * 4) * stride + bx * 4 * 3, stride, (int)(encW - bx * 4), (int)(encH - by * 4));
                }
            }
            _width = encW;
            _height = encH;
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glGenTextures(1, &_name);
            ccGLBindTexture2D(_name);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, (GLsizei)encW, (GLsizei)encH, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
            delete[] rgb;
            ok = glGetError() == GL_NO_ERROR;
            if (!ok)
            {
                CCLOG("cocos2d: TextureETC: upload after software decode failed %s", file);
            }
        }
    }
    delete[] data;
    return ok;
#endif
}

unsigned int CCTextureETC::getName() const
{
    return _name;
}

unsigned int CCTextureETC::getWidth() const
{
    return _width;
}

unsigned int CCTextureETC::getHeight() const
{
    return _height;
}

// Call back function for java
#if (CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID)
#define  LOG_TAG    "CCTextureETC.cpp"
#define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG,__VA_ARGS__)

static unsigned int sWidth = 0;
static unsigned int sHeight = 0;
static unsigned char *sData = NULL;
static unsigned int sLength = 0;

extern "C"
{
    JNIEXPORT void JNICALL Java_org_cocos2dx_lib_Cocos2dxETCLoader_nativeSetTextureInfo(JNIEnv* env, jobject thiz, jint width, jint height, jbyteArray data, jint dataLength)
    {
        sWidth = (unsigned int)width;
        sHeight = (unsigned int)height;
        sLength = dataLength;
        sData = new unsigned char[sLength];
        env->GetByteArrayRegion(data, 0, sLength, (jbyte*)sData);
    }
}
#endif

bool CCTextureETC::loadTexture(const char* file)
{
#if (CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID)
    JniMethodInfo t;
    if (JniHelper::getStaticMethodInfo(t, "org/cocos2dx/lib/Cocos2dxETCLoader", "loadTexture", "(Ljava/lang/String;)Z"))
    {
        jstring stringArg1 = t.env->NewStringUTF(file);
        jboolean ret = t.env->CallStaticBooleanMethod(t.classID, t.methodID, stringArg1);
        
        t.env->DeleteLocalRef(stringArg1);
        t.env->DeleteLocalRef(t.classID);
        
        if (ret)
        {
            _width = sWidth;
            _height = sHeight;
            
            
            glGenTextures(1, &_name);
            glBindTexture(GL_TEXTURE_2D, _name);
            
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            
            glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, _width, _height, 0, sLength, sData);
            
            glBindTexture(GL_TEXTURE_2D, 0);
            
            delete [] sData;
            sData = NULL;
            
            GLenum err = glGetError();
            if (err != GL_NO_ERROR)
            {
                LOGD("width %d, height %d, lenght %d", _width, _height, sLength);
                LOGD("cocos2d: TextureETC: Error uploading compressed texture %s glError: 0x%04X", file, err);
                return false;
            }
            
            return true;
        }
        else
        {
            return false;
        }
    }
#else
    return false;
#endif
}

NS_CC_END
