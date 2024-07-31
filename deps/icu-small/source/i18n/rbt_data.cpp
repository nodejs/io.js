// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (C) 1999-2014, International Business Machines
*   Corporation and others.  All Rights Reserved.
**********************************************************************
*   Date        Name        Description
*   11/17/99    aliu        Creation.
**********************************************************************
*/

#include "unicode/utypes.h"
#include "umutex.h"

#if !UCONFIG_NO_TRANSLITERATION

#include "unicode/unistr.h"
#include "unicode/uniset.h"
#include "rbt_data.h"
#include "hash.h"
#include "cmemory.h"

U_NAMESPACE_BEGIN

TransliterationRuleData::TransliterationRuleData(UErrorCode& status)
 : UMemory(), ruleSet(status), variableNames(status),
    variables(nullptr), variablesAreOwned(true)
{
    if (U_FAILURE(status)) {
        return;
    }
    variableNames.setValueDeleter(uprv_deleteUObject);
    variables = nullptr;
    variablesLength = 0;
}

TransliterationRuleData::TransliterationRuleData(const TransliterationRuleData& other) :
    UMemory(other), ruleSet(other.ruleSet),
    variablesAreOwned(true),
    variablesBase(other.variablesBase),
    variablesLength(other.variablesLength)
{
    UErrorCode status = U_ZERO_ERROR;
    int32_t i = 0;
    variableNames.setValueDeleter(uprv_deleteUObject);
    int32_t pos = UHASH_FIRST;
    const UHashElement *e;
    while ((e = other.variableNames.nextElement(pos)) != nullptr) {
        UnicodeString* value =
            new UnicodeString(*(const UnicodeString*)e->value.pointer);
        // Exit out if value could not be created.
        if (value == nullptr) {
        	return;
        }
        variableNames.put(*(UnicodeString*)e->key.pointer, value, status);
    }

    variables = nullptr;
    if (other.variables != nullptr) {
        variables = (UnicodeFunctor **)uprv_malloc(variablesLength * sizeof(UnicodeFunctor *));
        /* test for nullptr */
        if (variables == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        for (i=0; i<variablesLength; ++i) {
            variables[i] = other.variables[i]->clone();
            if (variables[i] == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                break;
            }
        }
    }
    // Remove the array and exit if memory allocation error occurred.
    if (U_FAILURE(status)) {
        for (int32_t n = i-1; n >= 0; n--) {
            delete variables[n];
        }
        uprv_free(variables);
        variables = nullptr;
        return;
    }

    // Do this last, _after_ setting up variables[].
    ruleSet.setData(this); // ruleSet must already be frozen
}

TransliterationRuleData::~TransliterationRuleData() {
    if (variablesAreOwned && variables != nullptr) {
        for (int32_t i=0; i<variablesLength; ++i) {
            delete variables[i];
        }
    }
    uprv_free(variables);
}

UnicodeFunctor*
TransliterationRuleData::lookup(UChar32 standIn) const {
    int32_t i = standIn - variablesBase;
    return (i >= 0 && i < variablesLength) ? variables[i] : nullptr;
}

UnicodeMatcher*
TransliterationRuleData::lookupMatcher(UChar32 standIn) const {
    UnicodeFunctor *f = lookup(standIn);
    return f != nullptr ? f->toMatcher() : nullptr;
}

UnicodeReplacer*
TransliterationRuleData::lookupReplacer(UChar32 standIn) const {
    UnicodeFunctor *f = lookup(standIn);
    return f != nullptr ? f->toReplacer() : nullptr;
}


U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_TRANSLITERATION */
