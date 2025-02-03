// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (C) 2014, International Business Machines
*   Corporation and others.  All Rights Reserved.
**********************************************************************
*
* scriptset.cpp
*
* created on: 2013 Jan 7
* created by: Andy Heninger
*/

#include "unicode/utypes.h"

#include "unicode/uchar.h"
#include "unicode/unistr.h"

#include "scriptset.h"
#include "uassert.h"
#include "cmemory.h"

U_NAMESPACE_BEGIN

//----------------------------------------------------------------------------
//
//  ScriptSet implementation
//
//----------------------------------------------------------------------------
ScriptSet::ScriptSet() {
    uprv_memset(bits, 0, sizeof(bits));
}

ScriptSet::~ScriptSet() {
}

ScriptSet::ScriptSet(const ScriptSet &other) {
    *this = other;
}

ScriptSet & ScriptSet::operator =(const ScriptSet &other) {
    uprv_memcpy(bits, other.bits, sizeof(bits));
    return *this;
}

bool ScriptSet::operator == (const ScriptSet &other) const {
    for (uint32_t i=0; i<UPRV_LENGTHOF(bits); i++) {
        if (bits[i] != other.bits[i]) {
            return false;
        }
    }
    return true;
}

UBool ScriptSet::test(UScriptCode script, UErrorCode &status) const {
    if (U_FAILURE(status)) {
        return false;
    }
    if (script < 0 || static_cast<int32_t>(script) >= SCRIPT_LIMIT) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return false;
    }
    uint32_t index = script / 32;
    uint32_t bit   = 1 << (script & 31);
    return ((bits[index] & bit) != 0);
}


ScriptSet &ScriptSet::set(UScriptCode script, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return *this;
    }
    if (script < 0 || static_cast<int32_t>(script) >= SCRIPT_LIMIT) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return *this;
    }
    uint32_t index = script / 32;
    uint32_t bit   = 1 << (script & 31);
    bits[index] |= bit;
    return *this;
}

ScriptSet &ScriptSet::reset(UScriptCode script, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return *this;
    }
    if (script < 0 || static_cast<int32_t>(script) >= SCRIPT_LIMIT) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return *this;
    }
    uint32_t index = script / 32;
    uint32_t bit   = 1 << (script & 31);
    bits[index] &= ~bit;
    return *this;
}



ScriptSet &ScriptSet::Union(const ScriptSet &other) {
    for (uint32_t i=0; i<UPRV_LENGTHOF(bits); i++) {
        bits[i] |= other.bits[i];
    }
    return *this;
}

ScriptSet &ScriptSet::intersect(const ScriptSet &other) {
    for (uint32_t i=0; i<UPRV_LENGTHOF(bits); i++) {
        bits[i] &= other.bits[i];
    }
    return *this;
}

ScriptSet &ScriptSet::intersect(UScriptCode script, UErrorCode &status) {
    ScriptSet t;
    t.set(script, status);
    if (U_SUCCESS(status)) {
        this->intersect(t);
    }
    return *this;
}

UBool ScriptSet::intersects(const ScriptSet &other) const {
    for (uint32_t i=0; i<UPRV_LENGTHOF(bits); i++) {
        if ((bits[i] & other.bits[i]) != 0) {
            return true;
        }
    }
    return false;
}

UBool ScriptSet::contains(const ScriptSet &other) const {
    ScriptSet t(*this);
    t.intersect(other);
    return (t == other);
}


ScriptSet &ScriptSet::setAll() {
    for (uint32_t i=0; i<UPRV_LENGTHOF(bits); i++) {
        bits[i] = 0xffffffffu;
    }
    return *this;
}


ScriptSet &ScriptSet::resetAll() {
    uprv_memset(bits, 0, sizeof(bits));
    return *this;
}

int32_t ScriptSet::countMembers() const {
    // This bit counter is good for sparse numbers of '1's, which is
    //  very much the case that we will usually have.
    int32_t count = 0;
    for (uint32_t i=0; i<UPRV_LENGTHOF(bits); i++) {
        uint32_t x = bits[i];
        while (x > 0) {
            count++;
            x &= (x - 1);    // and off the least significant one bit.
        }
    }
    return count;
}

int32_t ScriptSet::hashCode() const {
    int32_t hash = 0;
    for (int32_t i=0; i<UPRV_LENGTHOF(bits); i++) {
        hash ^= bits[i];
    }
    return hash;
}

int32_t ScriptSet::nextSetBit(int32_t fromIndex) const {
    // TODO: Wants a better implementation.
    if (fromIndex < 0) {
        return -1;
    }
    UErrorCode status = U_ZERO_ERROR;
    for (int32_t scriptIndex = fromIndex; scriptIndex < SCRIPT_LIMIT; scriptIndex++) {
        if (test(static_cast<UScriptCode>(scriptIndex), status)) {
            return scriptIndex;
        }
    }
    return -1;
}

UBool ScriptSet::isEmpty() const {
    for (uint32_t i=0; i<UPRV_LENGTHOF(bits); i++) {
        if (bits[i] != 0) {
            return false;
        }
    }
    return true;
}

UnicodeString &ScriptSet::displayScripts(UnicodeString &dest) const {
    UBool firstTime = true;
    for (int32_t i = nextSetBit(0); i >= 0; i = nextSetBit(i + 1)) {
        if (!firstTime) {
            dest.append(static_cast<char16_t>(0x20));
        }
        firstTime = false;
        const char* scriptName = uscript_getShortName(static_cast<UScriptCode>(i));
        dest.append(UnicodeString(scriptName, -1, US_INV));
    }
    return dest;
}

ScriptSet &ScriptSet::parseScripts(const UnicodeString &scriptString, UErrorCode &status) {
    resetAll();
    if (U_FAILURE(status)) {
        return *this;
    }
    UnicodeString oneScriptName;
    for (int32_t i=0; i<scriptString.length();) {
        UChar32 c = scriptString.char32At(i);
        i = scriptString.moveIndex32(i, 1);
        if (!u_isUWhiteSpace(c)) {
            oneScriptName.append(c);
            if (i < scriptString.length()) {
                continue;
            }
        }
        if (oneScriptName.length() > 0) {
            char buf[40];
            oneScriptName.extract(0, oneScriptName.length(), buf, sizeof(buf)-1, US_INV);
            buf[sizeof(buf)-1] = 0;
            int32_t sc = u_getPropertyValueEnum(UCHAR_SCRIPT, buf);
            if (sc == UCHAR_INVALID_CODE) {
                status = U_ILLEGAL_ARGUMENT_ERROR;
            } else {
                this->set(static_cast<UScriptCode>(sc), status);
            }
            if (U_FAILURE(status)) {
                return *this;
            }
            oneScriptName.remove();
        }
    }
    return *this;
}

void ScriptSet::setScriptExtensions(UChar32 codePoint, UErrorCode& status) {
    if (U_FAILURE(status)) { return; }
    static const int32_t FIRST_GUESS_SCRIPT_CAPACITY = 20;
    MaybeStackArray<UScriptCode,FIRST_GUESS_SCRIPT_CAPACITY> scripts;
    UErrorCode internalStatus = U_ZERO_ERROR;
    int32_t script_count = -1;

    while (true) {
        script_count = uscript_getScriptExtensions(
            codePoint, scripts.getAlias(), scripts.getCapacity(), &internalStatus);
        if (internalStatus == U_BUFFER_OVERFLOW_ERROR) {
            // Need to allocate more space
            if (scripts.resize(script_count) == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                return;
            }
            internalStatus = U_ZERO_ERROR;
        } else {
            break;
        }
    }

    // Check if we failed for some reason other than buffer overflow
    if (U_FAILURE(internalStatus)) {
        status = internalStatus;
        return;
    }

    // Load the scripts into the ScriptSet and return
    for (int32_t i = 0; i < script_count; i++) {
        this->set(scripts[i], status);
        if (U_FAILURE(status)) { return; }
    }
}

U_NAMESPACE_END

U_CAPI UBool U_EXPORT2
uhash_equalsScriptSet(const UElement key1, const UElement key2) {
    icu::ScriptSet *s1 = static_cast<icu::ScriptSet *>(key1.pointer);
    icu::ScriptSet *s2 = static_cast<icu::ScriptSet *>(key2.pointer);
    return (*s1 == *s2);
}

U_CAPI int8_t U_EXPORT2
uhash_compareScriptSet(UElement key0, UElement key1) {
    icu::ScriptSet *s0 = static_cast<icu::ScriptSet *>(key0.pointer);
    icu::ScriptSet *s1 = static_cast<icu::ScriptSet *>(key1.pointer);
    int32_t diff = s0->countMembers() - s1->countMembers();
    if (diff != 0) return static_cast<UBool>(diff);
    int32_t i0 = s0->nextSetBit(0);
    int32_t i1 = s1->nextSetBit(0);
    while ((diff = i0-i1) == 0 && i0 > 0) {
        i0 = s0->nextSetBit(i0+1);
        i1 = s1->nextSetBit(i1+1);
    }
    return (int8_t)diff;
}

U_CAPI int32_t U_EXPORT2
uhash_hashScriptSet(const UElement key) {
    icu::ScriptSet *s = static_cast<icu::ScriptSet *>(key.pointer);
    return s->hashCode();
}

U_CAPI void U_EXPORT2
uhash_deleteScriptSet(void *obj) {
    icu::ScriptSet *s = static_cast<icu::ScriptSet *>(obj);
    delete s;
}
