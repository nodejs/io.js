// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2012-2014, International Business Machines
* Corporation and others.  All Rights Reserved.
*******************************************************************************
* collationroot.cpp
*
* created on: 2012dec17
* created by: Markus W. Scherer
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_COLLATION

#include "unicode/coll.h"
#include "unicode/udata.h"
#include "collation.h"
#include "collationdata.h"
#include "collationdatareader.h"
#include "collationroot.h"
#include "collationsettings.h"
#include "collationtailoring.h"
#include "normalizer2impl.h"
#include "ucln_in.h"
#include "udatamem.h"
#include "umutex.h"
#include "umapfile.h"

U_NAMESPACE_BEGIN

namespace {

const CollationCacheEntry *rootSingleton = nullptr;
UInitOnce initOnce{};

}  // namespace

U_CDECL_BEGIN

static UBool U_CALLCONV uprv_collation_root_cleanup() {
    SharedObject::clearPtr(rootSingleton);
    initOnce.reset();
    return true;
}

U_CDECL_END

UDataMemory*
CollationRoot::loadFromFile(const char* ucadataPath, UErrorCode &errorCode) {
    UDataMemory dataMemory;
    UDataMemory  *rDataMem = nullptr;
    if (U_FAILURE(errorCode)) {
        return nullptr;
    }
    if (uprv_mapFile(&dataMemory, ucadataPath, &errorCode)) {
        if (dataMemory.pHeader->dataHeader.magic1 == 0xda &&
            dataMemory.pHeader->dataHeader.magic2 == 0x27 &&
            CollationDataReader::isAcceptable(nullptr, "icu", "ucadata", &dataMemory.pHeader->info)) {
            rDataMem = UDataMemory_createNewInstance(&errorCode);
            if (U_FAILURE(errorCode)) {
                return nullptr;
            }
            rDataMem->pHeader = dataMemory.pHeader;
            rDataMem->mapAddr = dataMemory.mapAddr;
            rDataMem->map = dataMemory.map;
            return rDataMem;
        }
        errorCode = U_INVALID_FORMAT_ERROR;
        return nullptr;
    }
    errorCode = U_MISSING_RESOURCE_ERROR;
    return nullptr;
}

void U_CALLCONV
CollationRoot::load(const char* ucadataPath, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    LocalPointer<CollationTailoring> t(new CollationTailoring(nullptr));
    if(t.isNull() || t->isBogus()) {
        errorCode = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    t->memory = ucadataPath ? CollationRoot::loadFromFile(ucadataPath, errorCode) :
                              udata_openChoice(U_ICUDATA_NAME U_TREE_SEPARATOR_STRING "coll",
                                               "icu", "ucadata",
                                               CollationDataReader::isAcceptable,
                                               t->version, &errorCode);
    if(U_FAILURE(errorCode)) { return; }
    const uint8_t *inBytes = static_cast<const uint8_t *>(udata_getMemory(t->memory));
    CollationDataReader::read(nullptr, inBytes, udata_getLength(t->memory), *t, errorCode);
    if(U_FAILURE(errorCode)) { return; }
    ucln_i18n_registerCleanup(UCLN_I18N_COLLATION_ROOT, uprv_collation_root_cleanup);
    CollationCacheEntry *entry = new CollationCacheEntry(Locale::getRoot(), t.getAlias());
    if(entry != nullptr) {
        t.orphan();  // The rootSingleton took ownership of the tailoring.
        entry->addRef();
        rootSingleton = entry;
    }
}

const CollationCacheEntry *
CollationRoot::getRootCacheEntry(UErrorCode &errorCode) {
    umtx_initOnce(initOnce, CollationRoot::load, static_cast<const char*>(nullptr), errorCode);
    if(U_FAILURE(errorCode)) { return nullptr; }
    return rootSingleton;
}

const CollationTailoring *
CollationRoot::getRoot(UErrorCode &errorCode) {
    umtx_initOnce(initOnce, CollationRoot::load, static_cast<const char*>(nullptr), errorCode);
    if(U_FAILURE(errorCode)) { return nullptr; }
    return rootSingleton->tailoring;
}

const CollationData *
CollationRoot::getData(UErrorCode &errorCode) {
    const CollationTailoring *root = getRoot(errorCode);
    if(U_FAILURE(errorCode)) { return nullptr; }
    return root->data;
}

const CollationSettings *
CollationRoot::getSettings(UErrorCode &errorCode) {
    const CollationTailoring *root = getRoot(errorCode);
    if(U_FAILURE(errorCode)) { return nullptr; }
    return root->settings;
}

void
CollationRoot::forceLoadFromFile(const char* ucadataPath, UErrorCode &errorCode) {
    umtx_initOnce(initOnce, CollationRoot::load, ucadataPath, errorCode);
}


U_NAMESPACE_END

#endif  // !UCONFIG_NO_COLLATION
