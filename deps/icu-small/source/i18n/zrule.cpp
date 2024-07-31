// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2009-2011, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*/

/**
 * \file 
 * \brief C API: Time zone rule classes
 */

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/uobject.h"
#include "zrule.h"
#include "unicode/tzrule.h"
#include "cmemory.h"
#include "unicode/ustring.h"
#include "unicode/parsepos.h"

U_NAMESPACE_USE

/*********************************************************************
 * ZRule API
 *********************************************************************/

U_CAPI void U_EXPORT2
zrule_close(ZRule* rule) {
    delete (TimeZoneRule*)rule;
}

U_CAPI UBool U_EXPORT2
zrule_equals(const ZRule* rule1, const ZRule* rule2) {
    return *(const TimeZoneRule*)rule1 == *(const TimeZoneRule*)rule2;
}

U_CAPI void U_EXPORT2
zrule_getName(ZRule* rule, char16_t* name, int32_t nameLength) {
    UnicodeString s(nameLength==-1, name, nameLength);
    s = ((TimeZoneRule*)rule)->TimeZoneRule::getName(s);
    nameLength = s.length();
    memcpy(name, s.getBuffer(), nameLength);
}

U_CAPI int32_t U_EXPORT2
zrule_getRawOffset(ZRule* rule) {
    return ((TimeZoneRule*)rule)->TimeZoneRule::getRawOffset();
}

U_CAPI int32_t U_EXPORT2
zrule_getDSTSavings(ZRule* rule) {
    return ((TimeZoneRule*)rule)->TimeZoneRule::getDSTSavings();
}

U_CAPI UBool U_EXPORT2
zrule_isEquivalentTo(ZRule* rule1,  ZRule* rule2) {
    return ((TimeZoneRule*)rule1)->TimeZoneRule::isEquivalentTo(*(TimeZoneRule*)rule2);
}

/*********************************************************************
 * IZRule API
 *********************************************************************/

U_CAPI IZRule* U_EXPORT2
izrule_open(const char16_t* name, int32_t nameLength, int32_t rawOffset, int32_t dstSavings) {
    UnicodeString s(nameLength==-1, name, nameLength);
    return (IZRule*) new InitialTimeZoneRule(s, rawOffset, dstSavings);
}

U_CAPI void U_EXPORT2
izrule_close(IZRule* rule) {
    delete (InitialTimeZoneRule*)rule;
}

U_CAPI IZRule* U_EXPORT2
izrule_clone(IZRule *rule) {
    return (IZRule*) (((InitialTimeZoneRule*)rule)->InitialTimeZoneRule::clone());
}

U_CAPI UBool U_EXPORT2
izrule_equals(const IZRule* rule1, const IZRule* rule2) {
    return *(const InitialTimeZoneRule*)rule1 == *(const InitialTimeZoneRule*)rule2;
}

U_CAPI void U_EXPORT2
izrule_getName(IZRule* rule, char16_t* & name, int32_t & nameLength) {
    // UnicodeString s(nameLength==-1, name, nameLength);
    UnicodeString s;
    ((InitialTimeZoneRule*)rule)->InitialTimeZoneRule::getName(s);
    nameLength = s.length();
    name = (char16_t*)uprv_malloc(nameLength);
    memcpy(name, s.getBuffer(), nameLength);
}

U_CAPI int32_t U_EXPORT2
izrule_getRawOffset(IZRule* rule) {
    return ((InitialTimeZoneRule*)rule)->InitialTimeZoneRule::getRawOffset();
}

U_CAPI int32_t U_EXPORT2
izrule_getDSTSavings(IZRule* rule) {
    return ((InitialTimeZoneRule*)rule)->InitialTimeZoneRule::getDSTSavings();
}

U_CAPI UBool U_EXPORT2
izrule_isEquivalentTo(IZRule* rule1,  IZRule* rule2) {
    return ((InitialTimeZoneRule*)rule1)->InitialTimeZoneRule::isEquivalentTo(*(InitialTimeZoneRule*)rule2);
}

U_CAPI UBool U_EXPORT2
izrule_getFirstStart(IZRule* rule, int32_t prevRawOffset, int32_t prevDSTSavings, 
                    UDate& result) {
    return ((const InitialTimeZoneRule*)rule)->InitialTimeZoneRule::getFirstStart(prevRawOffset, prevDSTSavings, result);
}

U_CAPI UBool U_EXPORT2
izrule_getFinalStart(IZRule* rule, int32_t prevRawOffset, int32_t prevDSTSavings, 
                    UDate& result) {
    return ((InitialTimeZoneRule*)rule)->InitialTimeZoneRule::getFinalStart(prevRawOffset, prevDSTSavings, result);
}

U_CAPI UBool U_EXPORT2
izrule_getNextStart(IZRule* rule, UDate base, int32_t prevRawOffset, 
                   int32_t prevDSTSavings, UBool inclusive, UDate& result) {
    return ((InitialTimeZoneRule*)rule)->InitialTimeZoneRule::getNextStart(base, prevRawOffset, prevDSTSavings, inclusive, result);
}

U_CAPI UBool U_EXPORT2
izrule_getPreviousStart(IZRule* rule, UDate base, int32_t prevRawOffset, 
                       int32_t prevDSTSavings, UBool inclusive, UDate& result) {
    return ((InitialTimeZoneRule*)rule)->InitialTimeZoneRule::getPreviousStart(base, prevRawOffset, prevDSTSavings, inclusive, result);
}

U_CAPI UClassID U_EXPORT2
izrule_getStaticClassID(IZRule* rule) {
    return ((InitialTimeZoneRule*)rule)->InitialTimeZoneRule::getStaticClassID();
}

U_CAPI UClassID U_EXPORT2
izrule_getDynamicClassID(IZRule* rule) {
    return ((InitialTimeZoneRule*)rule)->InitialTimeZoneRule::getDynamicClassID();
}

#endif
