// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
******************************************************************************
*   Copyright (C) 1997-2014, International Business Machines
*   Corporation and others.  All Rights Reserved.
******************************************************************************
*   Date        Name        Description
*   03/28/00    aliu        Creation.
******************************************************************************
*/

#ifndef HASH_H
#define HASH_H

#include "unicode/unistr.h"
#include "unicode/uobject.h"
#include "cmemory.h"
#include "uhash.h"

U_NAMESPACE_BEGIN

/**
 * Hashtable is a thin C++ wrapper around UHashtable, a general-purpose void*
 * hashtable implemented in C.  Hashtable is designed to be idiomatic and
 * easy-to-use in C++.
 *
 * Hashtable is an INTERNAL CLASS.
 */
class U_COMMON_API Hashtable : public UMemory {
    UHashtable* hash;
    UHashtable hashObj;

    inline void init(UHashFunction *keyHash, UKeyComparator *keyComp, UValueComparator *valueComp, UErrorCode& status);

    inline void initSize(UHashFunction *keyHash, UKeyComparator *keyComp, UValueComparator *valueComp, int32_t size, UErrorCode& status);

public:
    /**
     * Construct a hashtable
     * @param ignoreKeyCase If true, keys are case insensitive.
     * @param status Error code
    */
    inline Hashtable(UBool ignoreKeyCase, UErrorCode& status);

    /**
     * Construct a hashtable
     * @param ignoreKeyCase If true, keys are case insensitive.
     * @param size initial size allocation
     * @param status Error code
    */
    inline Hashtable(UBool ignoreKeyCase, int32_t size, UErrorCode& status);

    /**
     * Construct a hashtable
     * @param keyComp Comparator for comparing the keys
     * @param valueComp Comparator for comparing the values
     * @param status Error code
    */
    inline Hashtable(UKeyComparator *keyComp, UValueComparator *valueComp, UErrorCode& status);

    /**
     * Construct a hashtable
     * @param status Error code
    */
    inline Hashtable(UErrorCode& status);

    /**
     * Construct a hashtable, _disregarding any error_.  Use this constructor
     * with caution.
     */
    inline Hashtable();

    /**
     * Non-virtual destructor; make this virtual if Hashtable is subclassed
     * in the future.
     */
    inline ~Hashtable();

    inline UObjectDeleter *setValueDeleter(UObjectDeleter *fn);

    inline int32_t count() const;

    inline void* put(const UnicodeString& key, void* value, UErrorCode& status);

    inline int32_t puti(const UnicodeString& key, int32_t value, UErrorCode& status);

    inline int32_t putiAllowZero(const UnicodeString& key, int32_t value, UErrorCode& status);

    inline void* get(const UnicodeString& key) const;

    inline int32_t geti(const UnicodeString& key) const;

    inline int32_t getiAndFound(const UnicodeString& key, UBool &found) const;

    inline void* remove(const UnicodeString& key);

    inline int32_t removei(const UnicodeString& key);

    inline void removeAll();

    inline UBool containsKey(const UnicodeString& key) const;

    inline const UHashElement* find(const UnicodeString& key) const;

    /**
     * @param pos - must be UHASH_FIRST on first call, and untouched afterwards.
     * @see uhash_nextElement
     */
    inline const UHashElement* nextElement(int32_t& pos) const;

    inline UKeyComparator* setKeyComparator(UKeyComparator*keyComp);

    inline UValueComparator* setValueComparator(UValueComparator* valueComp);

    inline UBool equals(const Hashtable& that) const;
private:
    Hashtable(const Hashtable &other) = delete; // forbid copying of this class
    Hashtable &operator=(const Hashtable &other) = delete; // forbid copying of this class
};

/*********************************************************************
 * Implementation
 ********************************************************************/

inline void Hashtable::init(UHashFunction *keyHash, UKeyComparator *keyComp,
                            UValueComparator *valueComp, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    uhash_init(&hashObj, keyHash, keyComp, valueComp, &status);
    if (U_SUCCESS(status)) {
        hash = &hashObj;
        uhash_setKeyDeleter(hash, uprv_deleteUObject);
    }
}

inline void Hashtable::initSize(UHashFunction *keyHash, UKeyComparator *keyComp,
                                UValueComparator *valueComp, int32_t size, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    uhash_initSize(&hashObj, keyHash, keyComp, valueComp, size, &status);
    if (U_SUCCESS(status)) {
        hash = &hashObj;
        uhash_setKeyDeleter(hash, uprv_deleteUObject);
    }
}

inline Hashtable::Hashtable(UKeyComparator *keyComp, UValueComparator *valueComp,
                 UErrorCode& status) : hash(nullptr) {
    init( uhash_hashUnicodeString, keyComp, valueComp, status);
}

inline Hashtable::Hashtable(UBool ignoreKeyCase, UErrorCode& status)
 : hash(nullptr)
{
    init(ignoreKeyCase ? uhash_hashCaselessUnicodeString
                        : uhash_hashUnicodeString,
            ignoreKeyCase ? uhash_compareCaselessUnicodeString
                        : uhash_compareUnicodeString,
            nullptr,
            status);
}

inline Hashtable::Hashtable(UBool ignoreKeyCase, int32_t size, UErrorCode& status)
 : hash(nullptr)
{
    initSize(ignoreKeyCase ? uhash_hashCaselessUnicodeString
                        : uhash_hashUnicodeString,
            ignoreKeyCase ? uhash_compareCaselessUnicodeString
                        : uhash_compareUnicodeString,
            nullptr, size,
            status);
}

inline Hashtable::Hashtable(UErrorCode& status)
 : hash(nullptr)
{
    init(uhash_hashUnicodeString, uhash_compareUnicodeString, nullptr, status);
}

inline Hashtable::Hashtable()
 : hash(nullptr)
{
    UErrorCode status = U_ZERO_ERROR;
    init(uhash_hashUnicodeString, uhash_compareUnicodeString, nullptr, status);
}

inline Hashtable::~Hashtable() {
    if (hash != nullptr) {
        uhash_close(hash);
    }
}

inline UObjectDeleter *Hashtable::setValueDeleter(UObjectDeleter *fn) {
    return uhash_setValueDeleter(hash, fn);
}

inline int32_t Hashtable::count() const {
    return uhash_count(hash);
}

inline void* Hashtable::put(const UnicodeString& key, void* value, UErrorCode& status) {
    return uhash_put(hash, new UnicodeString(key), value, &status);
}

inline int32_t Hashtable::puti(const UnicodeString& key, int32_t value, UErrorCode& status) {
    return uhash_puti(hash, new UnicodeString(key), value, &status);
}

inline int32_t Hashtable::putiAllowZero(const UnicodeString& key, int32_t value,
                                        UErrorCode& status) {
    return uhash_putiAllowZero(hash, new UnicodeString(key), value, &status);
}

inline void* Hashtable::get(const UnicodeString& key) const {
    return uhash_get(hash, &key);
}

inline int32_t Hashtable::geti(const UnicodeString& key) const {
    return uhash_geti(hash, &key);
}

inline int32_t Hashtable::getiAndFound(const UnicodeString& key, UBool &found) const {
    return uhash_getiAndFound(hash, &key, &found);
}

inline void* Hashtable::remove(const UnicodeString& key) {
    return uhash_remove(hash, &key);
}

inline int32_t Hashtable::removei(const UnicodeString& key) {
    return uhash_removei(hash, &key);
}

inline UBool Hashtable::containsKey(const UnicodeString& key) const {
    return uhash_containsKey(hash, &key);
}

inline const UHashElement* Hashtable::find(const UnicodeString& key) const {
    return uhash_find(hash, &key);
}

inline const UHashElement* Hashtable::nextElement(int32_t& pos) const {
    return uhash_nextElement(hash, &pos);
}

inline void Hashtable::removeAll() {
    uhash_removeAll(hash);
}

inline UKeyComparator* Hashtable::setKeyComparator(UKeyComparator*keyComp){
    return uhash_setKeyComparator(hash, keyComp);
}

inline UValueComparator* Hashtable::setValueComparator(UValueComparator* valueComp){
    return uhash_setValueComparator(hash, valueComp);
}

inline UBool Hashtable::equals(const Hashtable& that)const{
   return uhash_equals(hash, that.hash);
}
U_NAMESPACE_END

#endif

