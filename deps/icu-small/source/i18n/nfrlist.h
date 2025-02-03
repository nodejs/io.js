// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
******************************************************************************
*   Copyright (C) 1997-2012, International Business Machines
*   Corporation and others.  All Rights Reserved.
******************************************************************************
*   file name:  nfrlist.h
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
* Modification history
* Date        Name      Comments
* 10/11/2001  Doug      Ported from ICU4J
*/

#ifndef NFRLIST_H
#define NFRLIST_H

#include "unicode/rbnf.h"

#if U_HAVE_RBNF

#include "unicode/uobject.h"
#include "nfrule.h"

#include "cmemory.h"

U_NAMESPACE_BEGIN

// unsafe class for internal use only.  assume memory allocations succeed, indexes are valid.
// should be a template, but we can't use them

class NFRuleList : public UMemory {
protected:
    NFRule** fStuff;
    uint32_t fCount;
    uint32_t fCapacity;
public:
    NFRuleList(uint32_t capacity = 10) 
        : fStuff(capacity ? static_cast<NFRule**>(uprv_malloc(capacity * sizeof(NFRule*))) : nullptr)
        , fCount(0)
        , fCapacity(capacity) {}
    ~NFRuleList() {
        if (fStuff) {
            for(uint32_t i = 0; i < fCount; ++i) {
                delete fStuff[i];
            }
            uprv_free(fStuff);
        }
    }
    NFRule* operator[](uint32_t index) const { return fStuff != nullptr ? fStuff[index] : nullptr; }
    NFRule* remove(uint32_t index) {
    	if (fStuff == nullptr) {
    		return nullptr;
    	}
        NFRule* result = fStuff[index];
        fCount -= 1;
        for (uint32_t i = index; i < fCount; ++i) { // assumes small arrays
            fStuff[i] = fStuff[i+1];
        }
        return result;
    }
    void add(NFRule* thing) {
        if (fCount == fCapacity) {
            fCapacity += 10;
            fStuff = static_cast<NFRule**>(uprv_realloc(fStuff, fCapacity * sizeof(NFRule*))); // assume success
        }
        if (fStuff != nullptr) {
        	fStuff[fCount++] = thing;
        } else {
        	fCapacity = 0;
        	fCount = 0;
        }
    }
    uint32_t size() const { return fCount; }
    NFRule* last() const { return (fCount > 0 && fStuff != nullptr) ? fStuff[fCount-1] : nullptr; }
    NFRule** release() {
        add(nullptr); // ensure null termination
        NFRule** result = fStuff;
        fStuff = nullptr;
        fCount = 0;
        fCapacity = 0;
        return result;
    }
    void deleteAll() {
        NFRule** tmp = nullptr;
        int32_t size = fCount;
        if (size > 0) {
            tmp = release();
            for (int32_t i = 0; i < size; i++) {
                delete tmp[i];
            }
            if (tmp) {
                uprv_free(tmp);
            }
        }
    }

private:
    NFRuleList(const NFRuleList &other); // forbid copying of this class
    NFRuleList &operator=(const NFRuleList &other); // forbid copying of this class
};

U_NAMESPACE_END

/* U_HAVE_RBNF */
#endif

// NFRLIST_H
#endif
