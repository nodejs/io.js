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
 * \brief C API: VTimeZone classes
 */

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/uobject.h"
#include "vzone.h"
#include "unicode/vtzone.h"
#include "cmemory.h"
#include "unicode/ustring.h"
#include "unicode/parsepos.h"

U_NAMESPACE_USE

U_CAPI VZone* U_EXPORT2
vzone_openID(const char16_t* ID, int32_t idLength){
    UnicodeString s(idLength==-1, ID, idLength);
    return (VZone*) (VTimeZone::createVTimeZoneByID(s));
}
    
U_CAPI VZone* U_EXPORT2
vzone_openData(const char16_t* vtzdata, int32_t vtzdataLength, UErrorCode& status) {
    UnicodeString s(vtzdataLength==-1, vtzdata, vtzdataLength);
    return (VZone*) (VTimeZone::createVTimeZone(s,status));
}

U_CAPI void U_EXPORT2
vzone_close(VZone* zone) {
    delete (VTimeZone*)zone;
}

U_CAPI VZone* U_EXPORT2
vzone_clone(const VZone *zone) {
    return (VZone*) (((VTimeZone*)zone)->VTimeZone::clone());
}

U_CAPI UBool U_EXPORT2
vzone_equals(const VZone* zone1, const VZone* zone2) {
    return *(const VTimeZone*)zone1 == *(const VTimeZone*)zone2;
}

U_CAPI UBool U_EXPORT2
vzone_getTZURL(VZone* zone, char16_t* & url, int32_t & urlLength) {
    UnicodeString s;
    UBool b = ((VTimeZone*)zone)->VTimeZone::getTZURL(s);

    urlLength = s.length();
    memcpy(url,s.getBuffer(),urlLength);
    
    return b;
}

U_CAPI void U_EXPORT2
vzone_setTZURL(VZone* zone, char16_t* url, int32_t urlLength) {
    UnicodeString s(urlLength==-1, url, urlLength);
    ((VTimeZone*)zone)->VTimeZone::setTZURL(s);
}

U_CAPI UBool U_EXPORT2
vzone_getLastModified(VZone* zone, UDate& lastModified) {
    return ((VTimeZone*)zone)->VTimeZone::getLastModified(lastModified);
}

U_CAPI void U_EXPORT2
vzone_setLastModified(VZone* zone, UDate lastModified) {
    return ((VTimeZone*)zone)->VTimeZone::setLastModified(lastModified);
}

U_CAPI void U_EXPORT2
vzone_write(VZone* zone, char16_t* & result, int32_t & resultLength, UErrorCode& status) {
    UnicodeString s;
    ((VTimeZone*)zone)->VTimeZone::write(s, status);

    resultLength = s.length();
    result = (char16_t*)uprv_malloc(resultLength);
    memcpy(result,s.getBuffer(),resultLength);
}

U_CAPI void U_EXPORT2
vzone_writeFromStart(VZone* zone, UDate start, char16_t* & result, int32_t & resultLength, UErrorCode& status) {
    UnicodeString s;
    ((VTimeZone*)zone)->VTimeZone::write(start, s, status);

    resultLength = s.length();
    result = (char16_t*)uprv_malloc(resultLength);
    memcpy(result,s.getBuffer(),resultLength);
}

U_CAPI void U_EXPORT2
vzone_writeSimple(VZone* zone, UDate time, char16_t* & result, int32_t & resultLength, UErrorCode& status) {
    UnicodeString s;
    ((VTimeZone*)zone)->VTimeZone::writeSimple(time, s, status);

    resultLength = s.length();
    result = (char16_t*)uprv_malloc(resultLength);
    memcpy(result,s.getBuffer(),resultLength);
}

U_CAPI int32_t U_EXPORT2
vzone_getOffset(VZone* zone, uint8_t era, int32_t year, int32_t month, int32_t day,
                uint8_t dayOfWeek, int32_t millis, UErrorCode& status) {
    return ((VTimeZone*)zone)->VTimeZone::getOffset(era, year, month, day, dayOfWeek, millis, status);
}

U_CAPI int32_t U_EXPORT2
vzone_getOffset2(VZone* zone, uint8_t era, int32_t year, int32_t month, int32_t day,
                uint8_t dayOfWeek, int32_t millis,
                int32_t monthLength, UErrorCode& status) {
    return ((VTimeZone*)zone)->VTimeZone::getOffset(era, year, month, day, dayOfWeek, millis, monthLength, status);
}

U_CAPI void U_EXPORT2
vzone_getOffset3(VZone* zone, UDate date, UBool local, int32_t& rawOffset,
                int32_t& dstOffset, UErrorCode& ec) {
    return ((VTimeZone*)zone)->VTimeZone::getOffset(date, local, rawOffset, dstOffset, ec);
}

U_CAPI void U_EXPORT2
vzone_setRawOffset(VZone* zone, int32_t offsetMillis) {
    return ((VTimeZone*)zone)->VTimeZone::setRawOffset(offsetMillis);
}

U_CAPI int32_t U_EXPORT2
vzone_getRawOffset(VZone* zone) {
    return ((VTimeZone*)zone)->VTimeZone::getRawOffset();
}

U_CAPI UBool U_EXPORT2
vzone_useDaylightTime(VZone* zone) {
    return ((VTimeZone*)zone)->VTimeZone::useDaylightTime();
}

U_CAPI UBool U_EXPORT2
vzone_inDaylightTime(VZone* zone, UDate date, UErrorCode& status) {
    return ((VTimeZone*)zone)->VTimeZone::inDaylightTime(date, status);
}

U_CAPI UBool U_EXPORT2
vzone_hasSameRules(VZone* zone, const VZone* other) {
    return ((VTimeZone*)zone)->VTimeZone::hasSameRules(*(VTimeZone*)other);
}

U_CAPI UBool U_EXPORT2
vzone_getNextTransition(VZone* zone, UDate base, UBool inclusive, ZTrans* result) {
    return ((VTimeZone*)zone)->VTimeZone::getNextTransition(base, inclusive, *(TimeZoneTransition*)result);
}

U_CAPI UBool U_EXPORT2
vzone_getPreviousTransition(VZone* zone, UDate base, UBool inclusive, ZTrans* result) {
    return ((VTimeZone*)zone)->VTimeZone::getPreviousTransition(base, inclusive, *(TimeZoneTransition*)result);
}

U_CAPI int32_t U_EXPORT2
vzone_countTransitionRules(VZone* zone, UErrorCode& status) {
    return ((VTimeZone*)zone)->VTimeZone::countTransitionRules(status);
}

U_CAPI UClassID U_EXPORT2
vzone_getStaticClassID(VZone* zone) {
    return ((VTimeZone*)zone)->VTimeZone::getStaticClassID();
}

U_CAPI UClassID U_EXPORT2
vzone_getDynamicClassID(VZone* zone) {
    return ((VTimeZone*)zone)->VTimeZone::getDynamicClassID();
}

#endif
