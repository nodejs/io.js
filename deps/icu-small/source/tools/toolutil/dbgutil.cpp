// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/********************************************************************
 * COPYRIGHT:
 * Copyright (c) 2007-2012, International Business Machines Corporation and
 * others. All Rights Reserved.
 ********************************************************************/

#include "udbgutil.h"
#include "dbgutil.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/unistr.h"
#include "unicode/ustring.h"
#include "util.h"
#include "ucln.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

U_NAMESPACE_USE

static UnicodeString **strs = nullptr;

static const UnicodeString&  _fieldString(UDebugEnumType type, int32_t field, UnicodeString& fillin) {
    const char *str = udbg_enumName(type, field);
    if(str == nullptr) {
        return fillin.remove();
    } else {
        return fillin = UnicodeString(str, -1, US_INV);
    }
}

U_CDECL_BEGIN
static void udbg_cleanup() {
    if(strs != nullptr) {
        for(int t=0;t<=UDBG_ENUM_COUNT;t++) {
            delete [] strs[t];
        }
        delete[] strs;
        strs = nullptr;
    }
}

static UBool tu_cleanup()
{
    udbg_cleanup();
    return true;
}

static void udbg_register_cleanup() {
   ucln_registerCleanup(UCLN_TOOLUTIL, tu_cleanup);
}
U_CDECL_END

static void udbg_setup() {
    if(strs == nullptr) {
        udbg_register_cleanup();
        //fprintf(stderr,"Initializing string cache..\n");
        //fflush(stderr);
        UnicodeString **newStrs = new UnicodeString*[UDBG_ENUM_COUNT+1];
        for(int t=0;t<UDBG_ENUM_COUNT;t++) {
            int32_t c = udbg_enumCount(static_cast<UDebugEnumType>(t));
            newStrs[t] = new UnicodeString[c+1];
            for(int f=0;f<=c;f++) {
                _fieldString(static_cast<UDebugEnumType>(t), f, newStrs[t][f]);
            }
        }
        newStrs[UDBG_ENUM_COUNT] = new UnicodeString[1]; // empty string

        strs = newStrs;
    }
}



U_TOOLUTIL_API const UnicodeString& U_EXPORT2 udbg_enumString(UDebugEnumType type, int32_t field) {
    if(strs == nullptr ) {
        udbg_setup();
    }
    if(type<0||type>=UDBG_ENUM_COUNT) {
        // use UDBG_ENUM_COUNT,0  to mean an empty string
        //fprintf(stderr, "** returning out of range on %d\n",type);
        //fflush(stderr);
        return strs[UDBG_ENUM_COUNT][0];
    }
    int32_t count = udbg_enumCount(type);
    //fprintf(stderr, "enumString [%d,%d]: typecount %d, fieldcount %d\n", type,field,UDBG_ENUM_COUNT,count);
    //fflush(stderr);
    if(field<0 || field > count) {
        return strs[type][count];
    } else {        return strs[type][field];
    }
}

U_CAPI int32_t  U_EXPORT2 udbg_enumByString(UDebugEnumType type, const UnicodeString& string) {
    if(type<0||type>=UDBG_ENUM_COUNT) {
        return -1;
    }
    // initialize array
    udbg_enumString(type,0);
    // search
   /// printf("type=%d\n", type); fflush(stdout);
    for(int i=0;i<udbg_enumCount(type);i++) {
//    printf("i=%d/%d\n", i, udbg_enumCount(type)); fflush(stdout);
        if(string == (strs[type][i])) {
            return i;
        }
    }
    return -1;
}

// from DataMap::utoi
U_CAPI int32_t 
udbg_stoi(const UnicodeString &s)
{
    char ch[256];
    const char16_t *u = toUCharPtr(s.getBuffer());
    int32_t len = s.length();
    u_UCharsToChars(u, ch, len);
    ch[len] = 0; /* include terminating \0 */
    return atoi(ch);
}


U_CAPI double 
udbg_stod(const UnicodeString &s)
{
    char ch[256];
    const char16_t *u = toUCharPtr(s.getBuffer());
    int32_t len = s.length();
    u_UCharsToChars(u, ch, len);
    ch[len] = 0; /* include terminating \0 */
    return atof(ch);
}

U_CAPI UnicodeString *
udbg_escape(const UnicodeString &src, UnicodeString *dst)
{
    dst->remove();
    for (int32_t i = 0; i < src.length(); ++i) {
        char16_t c = src[i];
        if(ICU_Utility::isUnprintable(c)) {
            *dst += UnicodeString("[");
            ICU_Utility::escapeUnprintable(*dst, c);
            *dst += UnicodeString("]");
        }
        else {
            *dst += c;
        }
    }

    return dst;
}



#endif
