/****************************************************************************
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2011      Zynga Inc.

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
#import <Foundation/Foundation.h>
#import <UIKit/UIDevice.h>

#include <string>
#include <stack>
#include <cstring>
#include <stdint.h>
#include "cocoa/CCString.h"
#include "CCFileUtils.h"
#include "CCDirector.h"
#include "CCSAXParser.h"
#include "CCDictionary.h"
#include "support/zip_support/unzip.h"

#include "CCFileUtilsIOS.h"
#include "Pfdw.h"

NS_CC_BEGIN

static void addValueToCCDict(id key, id value, CCDictionary* pDict);
static void addCCObjectToNSDict(const char*key, CCObject* object, NSMutableDictionary *dict);

static void addItemToCCArray(id item, CCArray *pArray)
{
    // add string value into array
    if ([item isKindOfClass:[NSString class]]) {
        CCString* pValue = new CCString([item UTF8String]);
        
        pArray->addObject(pValue);
        pValue->release();
        return;
    }
    
    // add number value into array(such as int, float, bool and so on)
    if ([item isKindOfClass:[NSNumber class]]) {
        NSString* pStr = [item stringValue];
        CCString* pValue = new CCString([pStr UTF8String]);
        
        pArray->addObject(pValue);
        pValue->release();
        return;
    }
    
    // add dictionary value into array
    if ([item isKindOfClass:[NSDictionary class]]) {
        CCDictionary* pDictItem = new CCDictionary();
        for (id subKey in [item allKeys]) {
            id subValue = [item objectForKey:subKey];
            addValueToCCDict(subKey, subValue, pDictItem);
        }
        pArray->addObject(pDictItem);
        pDictItem->release();
        return;
    }
    
    // add array value into array
    if ([item isKindOfClass:[NSArray class]]) {
        CCArray *pArrayItem = new CCArray();
        pArrayItem->init();
        for (id subItem in item) {
            addItemToCCArray(subItem, pArrayItem);
        }
        pArray->addObject(pArrayItem);
        pArrayItem->release();
        return;
    }
}

static void addCCObjectToNSArray(CCObject *object, NSMutableArray *array)
{
    // add string into array
    if (CCString *ccString = dynamic_cast<CCString *>(object)) {
        NSString *strElement = [NSString stringWithCString:ccString->getCString() encoding:NSUTF8StringEncoding];
        [array addObject:strElement];
        return;
    }
    
    // add array into array
    if (CCArray *ccArray = dynamic_cast<CCArray *>(object)) {
        NSMutableArray *arrElement = [NSMutableArray array];
        CCObject *element = NULL;
        CCARRAY_FOREACH(ccArray, element)
        {
            addCCObjectToNSArray(element, arrElement);
        }
        [array addObject:arrElement];
        return;
    }
    
    // add dictionary value into array
    if (CCDictionary *ccDict = dynamic_cast<CCDictionary *>(object)) {
        NSMutableDictionary *dictElement = [NSMutableDictionary dictionary];
        CCDictElement *element = NULL;
        CCDICT_FOREACH(ccDict, element)
        {
            addCCObjectToNSDict(element->getStrKey(), element->getObject(), dictElement);
        }
        [array addObject:dictElement];
    }

}

static void addValueToCCDict(id key, id value, CCDictionary* pDict)
{
    // the key must be a string
    CCAssert([key isKindOfClass:[NSString class]], "The key should be a string!");
    std::string pKey = [key UTF8String];
    
    // the value is a new dictionary
    if ([value isKindOfClass:[NSDictionary class]]) {
        CCDictionary* pSubDict = new CCDictionary();
        for (id subKey in [value allKeys]) {
            id subValue = [value objectForKey:subKey];
            addValueToCCDict(subKey, subValue, pSubDict);
        }
        pDict->setObject(pSubDict, pKey.c_str());
        pSubDict->release();
        return;
    }
    
    // the value is a string
    if ([value isKindOfClass:[NSString class]]) {
        CCString* pValue = new CCString([value UTF8String]);
        
        pDict->setObject(pValue, pKey.c_str());
        pValue->release();
        return;
    }
    
    // the value is a number
    if ([value isKindOfClass:[NSNumber class]]) {
        NSString* pStr = [value stringValue];
        CCString* pValue = new CCString([pStr UTF8String]);
        
        pDict->setObject(pValue, pKey.c_str());
        pValue->release();
        return;
    }
    
    // the value is a array
    if ([value isKindOfClass:[NSArray class]]) {
        CCArray *pArray = new CCArray();
        pArray->init();
        for (id item in value) {
            addItemToCCArray(item, pArray);
        }
        pDict->setObject(pArray, pKey.c_str());
        pArray->release();
        return;
    }
}

static void addCCObjectToNSDict(const char * key, CCObject* object, NSMutableDictionary *dict)
{
    NSString *NSkey = [NSString stringWithCString:key encoding:NSUTF8StringEncoding];
    
    // the object is a CCDictionary
    if (CCDictionary *ccDict = dynamic_cast<CCDictionary *>(object)) {
        NSMutableDictionary *dictElement = [NSMutableDictionary dictionary];
        CCDictElement *element = NULL;
        CCDICT_FOREACH(ccDict, element)
        {
            addCCObjectToNSDict(element->getStrKey(), element->getObject(), dictElement);
        }
        
        [dict setObject:dictElement forKey:NSkey];
        return;
    }
    
    // the object is a CCString
    if (CCString *element = dynamic_cast<CCString *>(object)) {
        NSString *strElement = [NSString stringWithCString:element->getCString() encoding:NSUTF8StringEncoding];
        [dict setObject:strElement forKey:NSkey];
        return;
    }
    
    // the object is a CCArray
    if (CCArray *ccArray = dynamic_cast<CCArray *>(object)) {
        NSMutableArray *arrElement = [NSMutableArray array];
        CCObject *element = NULL;
        CCARRAY_FOREACH(ccArray, element)
        {
            addCCObjectToNSArray(element, arrElement);
        }
        [dict setObject:arrElement forKey:NSkey];
        return;
    }
}

CCFileUtils* CCFileUtils::sharedFileUtils()
{
    if (s_sharedFileUtils == NULL)
    {
        s_sharedFileUtils = new CCFileUtilsIOS();
        s_sharedFileUtils->init();
    }
    return s_sharedFileUtils;
}


static NSFileManager* s_fileManager = [NSFileManager defaultManager];

std::string CCFileUtilsIOS::getWritablePath()
{
    // 天天爱西游：可写数据放 Library/Application Support/ttaxy（和 host::PlatformDocPath 一致），
    // 不放 Documents——Documents 对“文件”App 开放，玩家能直接改存档或塞入改过的资源
    NSArray *paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
    NSString *dir = [[paths objectAtIndex:0] stringByAppendingPathComponent:@"ttaxy"];
    [s_fileManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    std::string strRet = [dir UTF8String];
    strRet.append("/");
    return strRet;
}

static void EnsurePackRoot()
{
    static bool ready = false;
    if (ready) return;
    ready = true;
    NSString* root = [[NSBundle mainBundle] resourcePath];
    if (root) host::PfdwSetRoot([root UTF8String]);
}

static NSString* BundleFile(const std::string& relative)
{
    if (relative.empty()) return nil;
    NSString* rel = [NSString stringWithUTF8String:relative.c_str()];
    while ([rel hasPrefix:@"./"]) rel = [rel substringFromIndex:2];
    while ([rel hasPrefix:@"/"]) rel = [rel substringFromIndex:1];
    NSString* full = [[[NSBundle mainBundle] resourcePath] stringByAppendingPathComponent:rel];
    return [s_fileManager fileExistsAtPath:full] ? full : nil;
}

static const unsigned char kAxMagic[8] = {'A','X','E','N','C','0','0','1'};
static const unsigned char kAxKey[16] = {
    0xC3, 0x17, 0x9A, 0x4E, 0x62, 0xD8, 0x0B, 0x71,
    0xA5, 0x3F, 0xE2, 0x58, 0x14, 0x96, 0xCB, 0x2D
};

static unsigned char* AxDecryptIfNeeded(unsigned char* data, unsigned long* pSize)
{
    if (!data || !pSize || *pSize < 8) return data;
    if (std::memcmp(data, kAxMagic, 8) != 0) return data;
    const unsigned long n = *pSize - 8;
    unsigned char* plain = new unsigned char[n];
    uint32_t s = 0xA17C0DE1u;
    for (int i = 0; i < 16; ++i) s = s * 16777619u ^ kAxKey[i];
    if (s == 0) s = 0xA17C0DE1u;
    for (unsigned long i = 0; i < n; ++i) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        plain[i] = data[8 + i] ^ static_cast<unsigned char>(s & 0xFF);
    }
    delete[] data;
    *pSize = n;
    return plain;
}

bool CCFileUtilsIOS::isFileExist(const std::string& strFilePath)
{
    if (0 == strFilePath.length())
    {
        return false;
    }

    if (strFilePath[0] == '/')
    {
        return [s_fileManager fileExistsAtPath:[NSString stringWithUTF8String:strFilePath.c_str()]];
    }
    // 直接按包内路径找。没有散文件时，再看高帧安卓包里的 PFDW。
    if (BundleFile(strFilePath) != nil) return true;
    EnsurePackRoot();
    return host::PfdwContains(strFilePath);
}

unsigned char* CCFileUtilsIOS::getFileData(const char* pszFileName, const char* pszMode, unsigned long* pSize)
{
    unsigned char* loose = CCFileUtils::getFileData(pszFileName, pszMode, pSize);
    if (loose) return AxDecryptIfNeeded(loose, pSize);
    if (!pszFileName) return nullptr;
    EnsurePackRoot();
    std::string bytes;
    if (!host::PfdwRead(pszFileName, bytes) || bytes.empty()) return nullptr;
    unsigned char* data = new unsigned char[bytes.size()];
    memcpy(data, bytes.data(), bytes.size());
    if (pSize) *pSize = bytes.size();
    return AxDecryptIfNeeded(data, pSize);
}

std::string CCFileUtilsIOS::getFullPathForDirectoryAndFilename(const std::string& strDirectory, const std::string& strFilename)
{
    if (!strDirectory.empty() && strDirectory[0] == '/')
    {
        std::string fullPath = strDirectory + strFilename;
        if ([s_fileManager fileExistsAtPath:[NSString stringWithUTF8String:fullPath.c_str()]]) {
            return fullPath;
        }
        return "";
    }
    NSString* full = BundleFile(strDirectory + strFilename);
    return full != nil ? [full UTF8String] : "";
}

bool CCFileUtilsIOS::isAbsolutePath(const std::string& strPath)
{
    NSString* path = [NSString stringWithUTF8String:strPath.c_str()];
    return [path isAbsolutePath] ? true : false;
}

static id PlistFromFile(const std::string& filename)
{
    unsigned long size = 0;
    unsigned char* bytes = CCFileUtils::sharedFileUtils()->getFileData(filename.c_str(), "rb", &size);
    if (!bytes || size == 0)
    {
        delete[] bytes;
        return nil;
    }
    NSData* data = [NSData dataWithBytes:bytes length:size];
    delete[] bytes;
    NSError* error = nil;
    id plist = [NSPropertyListSerialization propertyListWithData:data
                                                         options:NSPropertyListImmutable
                                                          format:nil
                                                           error:&error];
    return plist;
}

CCDictionary* CCFileUtilsIOS::createCCDictionaryWithContentsOfFile(const std::string& filename)
{
    id plist = PlistFromFile(filename);
    if (![plist isKindOfClass:[NSDictionary class]]) return NULL;
    CCDictionary* pRet = new CCDictionary();
    for (id key in [plist allKeys]) {
        id value = [plist objectForKey:key];
        addValueToCCDict(key, value, pRet);
    }
    return pRet;
}

bool CCFileUtilsIOS::writeToFile(CCDictionary *dict, const std::string &fullPath)
{
    //CCLOG("iOS||Mac CCDictionary %d write to file %s", dict->m_uID, fullPath.c_str());
    NSMutableDictionary *nsDict = [NSMutableDictionary dictionary];
    
    CCDictElement *element = NULL;
    CCDICT_FOREACH(dict, element)
    {
        addCCObjectToNSDict(element->getStrKey(), element->getObject(), nsDict);
    }
    
    NSString *file = [NSString stringWithUTF8String:fullPath.c_str()];
    // do it atomically
    [nsDict writeToFile:file atomically:YES];
    
    return true;
}

CCArray* CCFileUtilsIOS::createCCArrayWithContentsOfFile(const std::string& filename)
{
    //    NSString* pPath = [NSString stringWithUTF8String:pFileName];
    //    NSString* pathExtension= [pPath pathExtension];
    //    pPath = [pPath stringByDeletingPathExtension];
    //    pPath = [[NSBundle mainBundle] pathForResource:pPath ofType:pathExtension];
    //    fixing cannot read data using CCArray::createWithContentsOfFile
    id plist = PlistFromFile(filename);
    CCArray* pRet = new CCArray();
    if ([plist isKindOfClass:[NSArray class]])
    {
        for (id value in plist) {
            addItemToCCArray(value, pRet);
        }
    }
    return pRet;
}

NS_CC_END

