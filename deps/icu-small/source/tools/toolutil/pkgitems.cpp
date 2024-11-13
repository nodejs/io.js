// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*
*   Copyright (C) 2003-2015, International Business Machines
*   Corporation and others.  All Rights Reserved.
*
*******************************************************************************
*   file name:  pkgitems.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2005sep18
*   created by: Markus W. Scherer
*
*   Companion file to package.cpp. Deals with details of ICU data item formats.
*   Used for item dependencies.
*   Contains adapted code from ucnv_bld.c (swapper code from 2003).
*/

#include "unicode/utypes.h"
#include "unicode/ures.h"
#include "unicode/putil.h"
#include "unicode/udata.h"
#include "cstring.h"
#include "uinvchar.h"
#include "ucmndata.h"
#include "udataswp.h"
#include "swapimpl.h"
#include "toolutil.h"
#include "package.h"
#include "pkg_imp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* item formats in common */

#include "uresdata.h"
#include "ucnv_bld.h"
#include "ucnv_io.h"

// general definitions ----------------------------------------------------- ***

U_CDECL_BEGIN

static void U_CALLCONV
printError(void *context, const char *fmt, va_list args) {
    vfprintf((FILE *)context, fmt, args);
}

U_CDECL_END

// a data item in native-platform form ------------------------------------- ***

U_NAMESPACE_BEGIN

class NativeItem {
public:
    NativeItem() : pItem(nullptr), pInfo(nullptr), bytes(nullptr), swapped(nullptr), length(0) {}
    NativeItem(const Item *item, UDataSwapFn *swap) : swapped(nullptr) {
        setItem(item, swap);
    }
    ~NativeItem() {
        delete [] swapped;
    }
    const UDataInfo *getDataInfo() const {
        return pInfo;
    }
    const uint8_t *getBytes() const {
        return bytes;
    }
    int32_t getLength() const {
        return length;
    }

    void setItem(const Item *item, UDataSwapFn *swap) {
        pItem=item;
        int32_t infoLength, itemHeaderLength;
        UErrorCode errorCode=U_ZERO_ERROR;
        pInfo=::getDataInfo(pItem->data, pItem->length, infoLength, itemHeaderLength, &errorCode);
        if(U_FAILURE(errorCode)) {
            exit(errorCode); // should succeed because readFile() checks headers
        }
        length=pItem->length-itemHeaderLength;

        if(pInfo->isBigEndian==U_IS_BIG_ENDIAN && pInfo->charsetFamily==U_CHARSET_FAMILY) {
            bytes=pItem->data+itemHeaderLength;
        } else {
            UDataSwapper* ds = udata_openSwapper(static_cast<UBool>(pInfo->isBigEndian), pInfo->charsetFamily, U_IS_BIG_ENDIAN, U_CHARSET_FAMILY, &errorCode);
            if(U_FAILURE(errorCode)) {
                fprintf(stderr, "icupkg: udata_openSwapper(\"%s\") failed - %s\n",
                        pItem->name, u_errorName(errorCode));
                exit(errorCode);
            }

            ds->printError=printError;
            ds->printErrorContext=stderr;

            swapped=new uint8_t[pItem->length];
            if(swapped==nullptr) {
                fprintf(stderr, "icupkg: unable to allocate memory for swapping \"%s\"\n", pItem->name);
                exit(U_MEMORY_ALLOCATION_ERROR);
            }
            swap(ds, pItem->data, pItem->length, swapped, &errorCode);
            pInfo=::getDataInfo(swapped, pItem->length, infoLength, itemHeaderLength, &errorCode);
            bytes=swapped+itemHeaderLength;
            udata_closeSwapper(ds);
        }
    }

private:
    const Item *pItem;
    const UDataInfo *pInfo;
    const uint8_t *bytes;
    uint8_t *swapped;
    int32_t length;
};

// check a dependency ------------------------------------------------------ ***

/*
 * assemble the target item name from the source item name, an ID
 * and a suffix
 */
static void 
makeTargetName(const char *itemName, const char *id, int32_t idLength, const char *suffix,
               char *target, int32_t capacity,
               UErrorCode *pErrorCode) {
    const char *itemID;
    int32_t treeLength, suffixLength, targetLength;

    // get the item basename
    itemID=strrchr(itemName, '/');
    if(itemID!=nullptr) {
        ++itemID;
    } else {
        itemID=itemName;
    }

    // build the target string
    treeLength = static_cast<int32_t>(itemID - itemName);
    if(idLength<0) {
        idLength = static_cast<int32_t>(strlen(id));
    }
    suffixLength = static_cast<int32_t>(strlen(suffix));
    targetLength=treeLength+idLength+suffixLength;
    if(targetLength>=capacity) {
        fprintf(stderr, "icupkg/makeTargetName(%s) target item name length %ld too long\n",
                        itemName, static_cast<long>(targetLength));
        *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
        return;
    }

    memcpy(target, itemName, treeLength);
    memcpy(target+treeLength, id, idLength);
    memcpy(target+treeLength+idLength, suffix, suffixLength+1); // +1 includes the terminating NUL
}

static void 
checkIDSuffix(const char *itemName, const char *id, int32_t idLength, const char *suffix,
              CheckDependency check, void *context,
              UErrorCode *pErrorCode) {
    char target[200];
    makeTargetName(itemName, id, idLength, suffix, target, static_cast<int32_t>(sizeof(target)), pErrorCode);
    if(U_SUCCESS(*pErrorCode)) {
        check(context, itemName, target);
    }
}

/* assemble the target item name from the item's parent item name */
static void 
checkParent(const char *itemName, CheckDependency check, void *context,
            UErrorCode *pErrorCode) {
    const char *itemID, *parent, *parentLimit, *suffix;
    int32_t parentLength;

    // get the item basename
    itemID=strrchr(itemName, '/');
    if(itemID!=nullptr) {
        ++itemID;
    } else {
        itemID=itemName;
    }

    // get the item suffix
    suffix=strrchr(itemID, '.');
    if(suffix==nullptr) {
        // empty suffix, point to the end of the string
        suffix=strrchr(itemID, 0);
    }

    // get the position of the last '_'
    for(parentLimit=suffix; parentLimit>itemID && *--parentLimit!='_';) {}

    if(parentLimit!=itemID) {
        // get the parent item name by truncating the last part of this item's name */
        parent=itemID;
        parentLength = static_cast<int32_t>(parentLimit - itemID);
    } else {
        // no '_' in the item name: the parent is the root bundle
        parent="root";
        parentLength=4;
        if((suffix-itemID)==parentLength && 0==memcmp(itemID, parent, parentLength)) {
            // the item itself is "root", which does not depend on a parent
            return;
        }
    }
    checkIDSuffix(itemName, parent, parentLength, suffix, check, context, pErrorCode);
}

// get dependencies from resource bundles ---------------------------------- ***

static const char16_t SLASH=0x2f;

/*
 * Check for the alias from the string or alias resource res.
 */
static void
checkAlias(const char *itemName,
           Resource res, const char16_t *alias, int32_t length, UBool useResSuffix,
           CheckDependency check, void *context, UErrorCode *pErrorCode) {
    int32_t i;

    if(!uprv_isInvariantUString(alias, length)) {
        fprintf(stderr, "icupkg/ures_enumDependencies(%s res=%08x) alias string contains non-invariant characters\n",
                        itemName, res);
        *pErrorCode=U_INVALID_CHAR_FOUND;
        return;
    }

    // extract the locale ID from alias strings like
    // locale_ID/key1/key2/key3
    // locale_ID

    // search for the first slash
    for(i=0; i<length && alias[i]!=SLASH; ++i) {}

    if(res_getPublicType(res)==URES_ALIAS) {
        // ignore aliases with an initial slash:
        // /ICUDATA/... and /pkgname/... go to a different package
        // /LOCALE/... are for dynamic sideways fallbacks and don't go to a fixed bundle
        if(i==0) {
            return; // initial slash ('/')
        }

        // ignore the intra-bundle path starting from the first slash ('/')
        length=i;
    } else /* URES_STRING */ {
        // the whole string should only consist of a locale ID
        if(i!=length) {
            fprintf(stderr, "icupkg/ures_enumDependencies(%s res=%08x) %%ALIAS contains a '/'\n",
                            itemName, res);
            *pErrorCode=U_UNSUPPORTED_ERROR;
            return;
        }
    }

    // convert the Unicode string to char *
    char localeID[48];
    if (length >= static_cast<int32_t>(sizeof(localeID))) {
        fprintf(stderr, "icupkg/ures_enumDependencies(%s res=%08x) alias locale ID length %ld too long\n",
                        itemName, res, static_cast<long>(length));
        *pErrorCode=U_BUFFER_OVERFLOW_ERROR;
        return;
    }
    u_UCharsToChars(alias, localeID, length);
    localeID[length]=0;

    checkIDSuffix(itemName, localeID, -1, (useResSuffix ? ".res" : ""), check, context, pErrorCode);
}

/*
 * Enumerate one resource item and its children and extract dependencies from
 * aliases.
 */
static UBool
ures_enumDependencies(const char *itemName,
                      const ResourceData *pResData,
                      Resource res, const char *inKey, const char *parentKey, int32_t depth,
                      CheckDependency check, void *context,
                      Package *pkg,
                      UErrorCode *pErrorCode) {
    UBool doCheckParent = true;  // always remains true if depth>1
    switch(res_getPublicType(res)) {
    case URES_STRING:
        if(depth==1 && inKey!=nullptr &&
                (0==strcmp(inKey, "%%ALIAS") || 0==strcmp(inKey, "%%Parent"))) {
            // Top-level %%ALIAS string:
            //   The alias resource bundle will be used instead of this one.
            // Top-level %%Parent string:
            //   We use this bundle as well as the explicit parent bundle.
            // Either way, the truncation parent is ignored.
            doCheckParent = false;
            // No tracing: build tool
            int32_t length;
            const char16_t *alias=res_getStringNoTrace(pResData, res, &length);
            checkAlias(itemName, res, alias, length, /*useResSuffix=*/ true,
                       check, context, pErrorCode);
            // If there is a %%ALIAS, then there should be nothing else in this resource bundle.
        } else if(depth==2 && parentKey!=nullptr && 0==strcmp(parentKey, "%%DEPENDENCY")) {
            // Second-level %%DEPENDENCY string:
            // Explicit declaration of a dependency of this item on that one.
            // No tracing: build tool
            int32_t length;
            const char16_t *alias=res_getStringNoTrace(pResData, res, &length);
            checkAlias(itemName, res, alias, length, /*useResSuffix=*/ false,
                       check, context, pErrorCode);
        }
        // we ignore all other strings
        break;
    case URES_ALIAS:
        {
            int32_t length;
            const char16_t *alias=res_getAlias(pResData, res, &length);
            checkAlias(itemName, res, alias, length, true, check, context, pErrorCode);
        }
        break;
    case URES_TABLE:
        {
            /* recurse */
            int32_t count=res_countArrayItems(pResData, res);
            for(int32_t i=0; i<count; ++i) {
                const char *itemKey;
                Resource item=res_getTableItemByIndex(pResData, res, i, &itemKey);
                // This doCheckParent return value is needed to
                // propagate the possible false value from depth=1 to depth=0.
                doCheckParent &= ures_enumDependencies(
                        itemName, pResData,
                        item, itemKey,
                        inKey, depth+1,
                        check, context,
                        pkg,
                        pErrorCode);
                if(U_FAILURE(*pErrorCode)) {
                    fprintf(stderr, "icupkg/ures_enumDependencies(%s table res=%08x)[%d].recurse(%s: %08x) failed\n",
                                    itemName, res, i, itemKey, item);
                    break;
                }
            }
        }
        break;
    case URES_ARRAY:
        {
            /* recurse */
            int32_t count=res_countArrayItems(pResData, res);
            for(int32_t i=0; i<count; ++i) {
                Resource item=res_getArrayItem(pResData, res, i);
                ures_enumDependencies(
                        itemName, pResData,
                        item, nullptr,
                        inKey, depth+1,
                        check, context,
                        pkg,
                        pErrorCode);
                if(U_FAILURE(*pErrorCode)) {
                    fprintf(stderr, "icupkg/ures_enumDependencies(%s array res=%08x)[%d].recurse(%08x) failed\n",
                                    itemName, res, i, item);
                    break;
                }
            }
        }
        break;
    default:
        break;
    }
    return doCheckParent;
}

static void
ures_enumDependencies(const char *itemName, const UDataInfo *pInfo,
                      const uint8_t *inBytes, int32_t length,
                      CheckDependency check, void *context,
                      Package *pkg,
                      UErrorCode *pErrorCode) {
    ResourceData resData;

    res_read(&resData, pInfo, inBytes, length, pErrorCode);
    if(U_FAILURE(*pErrorCode)) {
        fprintf(stderr, "icupkg: .res format version %02x.%02x not supported, or bundle malformed\n",
                        pInfo->formatVersion[0], pInfo->formatVersion[1]);
        exit(U_UNSUPPORTED_ERROR);
    }

    icu::NativeItem nativePool;

    if(resData.usesPoolBundle) {
        char poolName[200];
        makeTargetName(itemName, "pool", 4, ".res", poolName, static_cast<int32_t>(sizeof(poolName)), pErrorCode);
        if(U_FAILURE(*pErrorCode)) {
            return;
        }
        check(context, itemName, poolName);
        int32_t index=pkg->findItem(poolName);
        if(index<0) {
            // We cannot work with a bundle if its pool resource is missing.
            // check() already printed a complaint.
            return;
        }
        // TODO: Cache the native version in the Item itself.
        nativePool.setItem(pkg->getItem(index), ures_swap);
        const UDataInfo *poolInfo=nativePool.getDataInfo();
        if(poolInfo->formatVersion[0]<=1) {
            fprintf(stderr, "icupkg: %s is not a pool bundle\n", poolName);
            return;
        }
        const int32_t* poolRoot = reinterpret_cast<const int32_t*>(nativePool.getBytes());
        const int32_t *poolIndexes=poolRoot+1;
        int32_t poolIndexLength=poolIndexes[URES_INDEX_LENGTH]&0xff;
        if(!(poolIndexLength>URES_INDEX_POOL_CHECKSUM &&
             (poolIndexes[URES_INDEX_ATTRIBUTES]&URES_ATT_IS_POOL_BUNDLE))
        ) {
            fprintf(stderr, "icupkg: %s is not a pool bundle\n", poolName);
            return;
        }
        if(resData.pRoot[1+URES_INDEX_POOL_CHECKSUM]==poolIndexes[URES_INDEX_POOL_CHECKSUM]) {
            resData.poolBundleKeys = reinterpret_cast<const char*>(poolIndexes + poolIndexLength);
            resData.poolBundleStrings = reinterpret_cast<const uint16_t*>(poolRoot + poolIndexes[URES_INDEX_KEYS_TOP]);
        } else {
            fprintf(stderr, "icupkg: %s has mismatched checksum for %s\n", poolName, itemName);
            return;
        }
    }

    UBool doCheckParent = ures_enumDependencies(
        itemName, &resData,
        resData.rootRes, nullptr, nullptr, 0,
        check, context,
        pkg,
        pErrorCode);
    if(!doCheckParent) {
        return;
    }

    /*
     * if the bundle attributes are present and the nofallback flag is not set,
     * then add the parent bundle as a dependency
     */
    if(pInfo->formatVersion[0]>1 || (pInfo->formatVersion[0]==1 && pInfo->formatVersion[1]>=1)) {
        if(!resData.noFallback) {
            /* this bundle participates in locale fallback */
            checkParent(itemName, check, context, pErrorCode);
        }
    }
}

// get dependencies from conversion tables --------------------------------- ***

#if !UCONFIG_NO_CONVERSION
/* code adapted from ucnv_swap() */
static void
ucnv_enumDependencies(const UDataSwapper *ds,
                      const char *itemName, const UDataInfo *pInfo,
                      const uint8_t *inBytes, int32_t length,
                      CheckDependency check, void *context,
                      UErrorCode *pErrorCode) {
    uint32_t staticDataSize;

    const UConverterStaticData *inStaticData;

    const _MBCSHeader *inMBCSHeader;
    uint8_t outputType;

    /* check format version */
    if(!(
        pInfo->formatVersion[0]==6 &&
        pInfo->formatVersion[1]>=2
    )) {
        fprintf(stderr, "icupkg/ucnv_enumDependencies(): .cnv format version %02x.%02x not supported\n",
                        pInfo->formatVersion[0], pInfo->formatVersion[1]);
        exit(U_UNSUPPORTED_ERROR);
    }

    /* read the initial UConverterStaticData structure after the UDataInfo header */
    inStaticData = reinterpret_cast<const UConverterStaticData*>(inBytes);

    if (length < static_cast<int32_t>(sizeof(UConverterStaticData)) ||
        static_cast<uint32_t>(length) < (staticDataSize = ds->readUInt32(inStaticData->structSize))
    ) {
        udata_printError(ds, "icupkg/ucnv_enumDependencies(): too few bytes (%d after header) for an ICU .cnv conversion table\n",
                            length);
        *pErrorCode=U_INDEX_OUTOFBOUNDS_ERROR;
        return;
    }

    inBytes+=staticDataSize;
    length -= static_cast<int32_t>(staticDataSize);

    /* check for supported conversionType values */
    if(inStaticData->conversionType==UCNV_MBCS) {
        /* MBCS data */
        uint32_t mbcsHeaderLength, mbcsHeaderFlags, mbcsHeaderOptions;
        int32_t extOffset;

        inMBCSHeader = reinterpret_cast<const _MBCSHeader*>(inBytes);

        if (length < static_cast<int32_t>(sizeof(_MBCSHeader))) {
            udata_printError(ds, "icupkg/ucnv_enumDependencies(): too few bytes (%d after headers) for an ICU MBCS .cnv conversion table\n",
                                length);
            *pErrorCode=U_INDEX_OUTOFBOUNDS_ERROR;
            return;
        }
        if(inMBCSHeader->version[0]==4 && inMBCSHeader->version[1]>=1) {
            mbcsHeaderLength=MBCS_HEADER_V4_LENGTH;
        } else if(inMBCSHeader->version[0]==5 && inMBCSHeader->version[1]>=3 &&
                  ((mbcsHeaderOptions=ds->readUInt32(inMBCSHeader->options))&
                   MBCS_OPT_UNKNOWN_INCOMPATIBLE_MASK)==0
        ) {
            mbcsHeaderLength=mbcsHeaderOptions&MBCS_OPT_LENGTH_MASK;
        } else {
            udata_printError(ds, "icupkg/ucnv_enumDependencies(): unsupported _MBCSHeader.version %d.%d\n",
                             inMBCSHeader->version[0], inMBCSHeader->version[1]);
            *pErrorCode=U_UNSUPPORTED_ERROR;
            return;
        }

        mbcsHeaderFlags=ds->readUInt32(inMBCSHeader->flags);
        extOffset = static_cast<int32_t>(mbcsHeaderFlags >> 8);
        outputType = static_cast<uint8_t>(mbcsHeaderFlags);

        if(outputType==MBCS_OUTPUT_EXT_ONLY) {
            /*
             * extension-only file,
             * contains a base name instead of normal base table data
             */
            char baseName[32];
            int32_t baseNameLength;

            /* there is extension data after the base data, see ucnv_ext.h */
            if(length<(extOffset+UCNV_EXT_INDEXES_MIN_LENGTH*4)) {
                udata_printError(ds, "icupkg/ucnv_enumDependencies(): too few bytes (%d after headers) for an ICU MBCS .cnv conversion table with extension data\n",
                                 length);
                *pErrorCode=U_INDEX_OUTOFBOUNDS_ERROR;
                return;
            }

            /* swap the base name, between the header and the extension data */
            const char* inBaseName = reinterpret_cast<const char*>(inBytes) + mbcsHeaderLength * 4;
            baseNameLength = static_cast<int32_t>(strlen(inBaseName));
            if (baseNameLength >= static_cast<int32_t>(sizeof(baseName))) {
                udata_printError(ds, "icupkg/ucnv_enumDependencies(%s): base name length %ld too long\n",
                                 itemName, baseNameLength);
                *pErrorCode=U_UNSUPPORTED_ERROR;
                return;
            }
            ds->swapInvChars(ds, inBaseName, baseNameLength+1, baseName, pErrorCode);

            checkIDSuffix(itemName, baseName, -1, ".cnv", check, context, pErrorCode);
        }
    }
}

// ICU data formats -------------------------------------------------------- ***

static const struct {
    uint8_t dataFormat[4];
} dataFormats[]={
    { { 0x52, 0x65, 0x73, 0x42 } },     /* dataFormat="ResB" */
    { { 0x63, 0x6e, 0x76, 0x74 } },     /* dataFormat="cnvt" */
    { { 0x43, 0x76, 0x41, 0x6c } }      /* dataFormat="CvAl" */
};

enum {
    FMT_RES,
    FMT_CNV,
    FMT_ALIAS,
    FMT_COUNT
};

static int32_t
getDataFormat(const uint8_t dataFormat[4]) {
    int32_t i;

    for(i=0; i<FMT_COUNT; ++i) {
        if(0==memcmp(dataFormats[i].dataFormat, dataFormat, 4)) {
            return i;
        }
    }
    return -1;
}

// enumerate dependencies of a package item -------------------------------- ***

void
Package::enumDependencies(Item *pItem, void *context, CheckDependency check) {
    int32_t infoLength, itemHeaderLength;
    UErrorCode errorCode=U_ZERO_ERROR;
    const UDataInfo *pInfo=getDataInfo(pItem->data, pItem->length, infoLength, itemHeaderLength, &errorCode);
    if(U_FAILURE(errorCode)) {
        return; // should not occur because readFile() checks headers
    }

    // find the data format and call the corresponding function, if any
    int32_t format=getDataFormat(pInfo->dataFormat);
    if(format>=0) {
        switch(format) {
        case FMT_RES:
            {
                /*
                 * Swap the resource bundle (if necessary) so that we can use
                 * the normal runtime uresdata.c code to read it.
                 * We do not want to duplicate that code, especially not together with on-the-fly swapping.
                 */
                NativeItem nrb(pItem, ures_swap);
                ures_enumDependencies(pItem->name, nrb.getDataInfo(), nrb.getBytes(), nrb.getLength(), check, context, this, &errorCode);
                break;
            }
        case FMT_CNV:
            {
                // TODO: share/cache swappers
                UDataSwapper *ds=udata_openSwapper(
                                    static_cast<UBool>(pInfo->isBigEndian), pInfo->charsetFamily,
                                    U_IS_BIG_ENDIAN, U_CHARSET_FAMILY,
                                    &errorCode);
                if(U_FAILURE(errorCode)) {
                    fprintf(stderr, "icupkg: udata_openSwapper(\"%s\") failed - %s\n",
                            pItem->name, u_errorName(errorCode));
                    exit(errorCode);
                }

                ds->printError=printError;
                ds->printErrorContext=stderr;

                const uint8_t *inBytes=pItem->data+itemHeaderLength;
                int32_t length=pItem->length-itemHeaderLength;

                ucnv_enumDependencies(ds, pItem->name, pInfo, inBytes, length, check, context, &errorCode);
                udata_closeSwapper(ds);
                break;
            }
        default:
            break;
        }

        if(U_FAILURE(errorCode)) {
            exit(errorCode);
        }
    }
}
#endif /* UCONFIG_NO_CONVERSION */

U_NAMESPACE_END
