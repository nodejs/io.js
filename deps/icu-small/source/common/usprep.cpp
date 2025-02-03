// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 *******************************************************************************
 *
 *   Copyright (C) 2003-2016, International Business Machines
 *   Corporation and others.  All Rights Reserved.
 *
 *******************************************************************************
 *   file name:  usprep.cpp
 *   encoding:   UTF-8
 *   tab size:   8 (not used)
 *   indentation:4
 *
 *   created on: 2003jul2
 *   created by: Ram Viswanadha
 */

#include "unicode/utypes.h"

#if !UCONFIG_NO_IDNA

#include "unicode/usprep.h"

#include "unicode/normalizer2.h"
#include "unicode/ustring.h"
#include "unicode/uchar.h"
#include "unicode/uversion.h"
#include "umutex.h"
#include "cmemory.h"
#include "sprpimpl.h"
#include "ustr_imp.h"
#include "uhash.h"
#include "cstring.h"
#include "udataswp.h"
#include "ucln_cmn.h"
#include "ubidi_props.h"
#include "uprops.h"

U_NAMESPACE_USE

U_CDECL_BEGIN

/*
Static cache for already opened StringPrep profiles
*/
static UHashtable *SHARED_DATA_HASHTABLE = nullptr;
static icu::UInitOnce gSharedDataInitOnce {};

static UMutex usprepMutex;
/* format version of spp file */
//static uint8_t formatVersion[4]={ 0, 0, 0, 0 };

/* the Unicode version of the sprep data */
static UVersionInfo dataVersion={ 0, 0, 0, 0 };

/* Profile names must be aligned to UStringPrepProfileType */
static const char * const PROFILE_NAMES[] = {
    "rfc3491",      /* USPREP_RFC3491_NAMEPREP */
    "rfc3530cs",    /* USPREP_RFC3530_NFS4_CS_PREP */
    "rfc3530csci",  /* USPREP_RFC3530_NFS4_CS_PREP_CI */
    "rfc3491",      /* USPREP_RFC3530_NSF4_CIS_PREP */
    "rfc3530mixp",  /* USPREP_RFC3530_NSF4_MIXED_PREP_PREFIX */
    "rfc3491",      /* USPREP_RFC3530_NSF4_MIXED_PREP_SUFFIX */
    "rfc3722",      /* USPREP_RFC3722_ISCSI */
    "rfc3920node",  /* USPREP_RFC3920_NODEPREP */
    "rfc3920res",   /* USPREP_RFC3920_RESOURCEPREP */
    "rfc4011",      /* USPREP_RFC4011_MIB */
    "rfc4013",      /* USPREP_RFC4013_SASLPREP */
    "rfc4505",      /* USPREP_RFC4505_TRACE */
    "rfc4518",      /* USPREP_RFC4518_LDAP */
    "rfc4518ci",    /* USPREP_RFC4518_LDAP_CI */
};

static UBool U_CALLCONV
isSPrepAcceptable(void * /* context */,
             const char * /* type */, 
             const char * /* name */,
             const UDataInfo *pInfo) {
    if(
        pInfo->size>=20 &&
        pInfo->isBigEndian==U_IS_BIG_ENDIAN &&
        pInfo->charsetFamily==U_CHARSET_FAMILY &&
        pInfo->dataFormat[0]==0x53 &&   /* dataFormat="SPRP" */
        pInfo->dataFormat[1]==0x50 &&
        pInfo->dataFormat[2]==0x52 &&
        pInfo->dataFormat[3]==0x50 &&
        pInfo->formatVersion[0]==3 &&
        pInfo->formatVersion[2]==UTRIE_SHIFT &&
        pInfo->formatVersion[3]==UTRIE_INDEX_SHIFT
    ) {
        //uprv_memcpy(formatVersion, pInfo->formatVersion, 4);
        uprv_memcpy(dataVersion, pInfo->dataVersion, 4);
        return true;
    } else {
        return false;
    }
}

static int32_t U_CALLCONV
getSPrepFoldingOffset(uint32_t data) {

    return (int32_t)data;

}

/* hashes an entry  */
static int32_t U_CALLCONV 
hashEntry(const UHashTok parm) {
    UStringPrepKey *b = (UStringPrepKey *)parm.pointer;
    UHashTok namekey, pathkey;
    namekey.pointer = b->name;
    pathkey.pointer = b->path;
    uint32_t unsignedHash = static_cast<uint32_t>(uhash_hashChars(namekey)) +
            37u * static_cast<uint32_t>(uhash_hashChars(pathkey));
    return static_cast<int32_t>(unsignedHash);
}

/* compares two entries */
static UBool U_CALLCONV 
compareEntries(const UHashTok p1, const UHashTok p2) {
    UStringPrepKey *b1 = (UStringPrepKey *)p1.pointer;
    UStringPrepKey *b2 = (UStringPrepKey *)p2.pointer;
    UHashTok name1, name2, path1, path2;
    name1.pointer = b1->name;
    name2.pointer = b2->name;
    path1.pointer = b1->path;
    path2.pointer = b2->path;
    return uhash_compareChars(name1, name2) & uhash_compareChars(path1, path2);
}

static void 
usprep_unload(UStringPrepProfile* data){
    udata_close(data->sprepData);
}

static int32_t 
usprep_internal_flushCache(UBool noRefCount){
    UStringPrepProfile *profile = nullptr;
    UStringPrepKey  *key  = nullptr;
    int32_t pos = UHASH_FIRST;
    int32_t deletedNum = 0;
    const UHashElement *e;

    /*
     * if shared data hasn't even been lazy evaluated yet
     * return 0
     */
    umtx_lock(&usprepMutex);
    if (SHARED_DATA_HASHTABLE == nullptr) {
        umtx_unlock(&usprepMutex);
        return 0;
    }

    /*creates an enumeration to iterate through every element in the table */
    while ((e = uhash_nextElement(SHARED_DATA_HASHTABLE, &pos)) != nullptr)
    {
        profile = (UStringPrepProfile *) e->value.pointer;
        key  = (UStringPrepKey *) e->key.pointer;

        if ((noRefCount== false && profile->refCount == 0) || 
             noRefCount) {
            deletedNum++;
            uhash_removeElement(SHARED_DATA_HASHTABLE, e);

            /* unload the data */
            usprep_unload(profile);

            if(key->name != nullptr) {
                uprv_free(key->name);
                key->name=nullptr;
            }
            if(key->path != nullptr) {
                uprv_free(key->path);
                key->path=nullptr;
            }
            uprv_free(profile);
            uprv_free(key);
        }
       
    }
    umtx_unlock(&usprepMutex);

    return deletedNum;
}

/* Works just like ucnv_flushCache() 
static int32_t 
usprep_flushCache(){
    return usprep_internal_flushCache(false);
}
*/

static UBool U_CALLCONV usprep_cleanup(){
    if (SHARED_DATA_HASHTABLE != nullptr) {
        usprep_internal_flushCache(true);
        if (SHARED_DATA_HASHTABLE != nullptr && uhash_count(SHARED_DATA_HASHTABLE) == 0) {
            uhash_close(SHARED_DATA_HASHTABLE);
            SHARED_DATA_HASHTABLE = nullptr;
        }
    }
    gSharedDataInitOnce.reset();
    return (SHARED_DATA_HASHTABLE == nullptr);
}
U_CDECL_END


/** Initializes the cache for resources */
static void U_CALLCONV
createCache(UErrorCode &status) {
    SHARED_DATA_HASHTABLE = uhash_open(hashEntry, compareEntries, nullptr, &status);
    if (U_FAILURE(status)) {
        SHARED_DATA_HASHTABLE = nullptr;
    }
    ucln_common_registerCleanup(UCLN_COMMON_USPREP, usprep_cleanup);
}

static void 
initCache(UErrorCode *status) {
    umtx_initOnce(gSharedDataInitOnce, &createCache, *status);
}

static UBool U_CALLCONV
loadData(UStringPrepProfile* profile, 
         const char* path, 
         const char* name, 
         const char* type, 
         UErrorCode* errorCode) {
    /* load Unicode SPREP data from file */    
    UTrie _sprepTrie = {nullptr, nullptr, nullptr, 0, 0, 0, 0};
    UDataMemory *dataMemory;
    const int32_t *p=nullptr;
    const uint8_t *pb;
    UVersionInfo normUnicodeVersion;
    int32_t normUniVer, sprepUniVer, normCorrVer;

    if(errorCode==nullptr || U_FAILURE(*errorCode)) {
        return 0;
    }

    /* open the data outside the mutex block */
    //TODO: change the path
    dataMemory=udata_openChoice(path, type, name, isSPrepAcceptable, nullptr, errorCode);
    if(U_FAILURE(*errorCode)) {
        return false;
    }

    p = static_cast<const int32_t*>(udata_getMemory(dataMemory));
    pb = reinterpret_cast<const uint8_t*>(p + _SPREP_INDEX_TOP);
    utrie_unserialize(&_sprepTrie, pb, p[_SPREP_INDEX_TRIE_SIZE], errorCode);
    _sprepTrie.getFoldingOffset=getSPrepFoldingOffset;


    if(U_FAILURE(*errorCode)) {
        udata_close(dataMemory);
        return false;
    }

    /* in the mutex block, set the data for this process */
    umtx_lock(&usprepMutex);
    if(profile->sprepData==nullptr) {
        profile->sprepData=dataMemory;
        dataMemory=nullptr;
        uprv_memcpy(&profile->indexes, p, sizeof(profile->indexes));
        uprv_memcpy(&profile->sprepTrie, &_sprepTrie, sizeof(UTrie));
    } else {
        p = static_cast<const int32_t*>(udata_getMemory(profile->sprepData));
    }
    umtx_unlock(&usprepMutex);
    /* initialize some variables */
    profile->mappingData = reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(p + _SPREP_INDEX_TOP) + profile->indexes[_SPREP_INDEX_TRIE_SIZE]);
    
    u_getUnicodeVersion(normUnicodeVersion);
    normUniVer = (normUnicodeVersion[0] << 24) + (normUnicodeVersion[1] << 16) + 
                 (normUnicodeVersion[2] << 8 ) + (normUnicodeVersion[3]);
    sprepUniVer = (dataVersion[0] << 24) + (dataVersion[1] << 16) + 
                  (dataVersion[2] << 8 ) + (dataVersion[3]);
    normCorrVer = profile->indexes[_SPREP_NORM_CORRECTNS_LAST_UNI_VERSION];
    
    if(U_FAILURE(*errorCode)){
        udata_close(dataMemory);
        return false;
    }
    if( normUniVer < sprepUniVer && /* the Unicode version of SPREP file must be less than the Unicode Version of the normalization data */
        normUniVer < normCorrVer && /* the Unicode version of the NormalizationCorrections.txt file should be less than the Unicode Version of the normalization data */
        ((profile->indexes[_SPREP_OPTIONS] & _SPREP_NORMALIZATION_ON) > 0) /* normalization turned on*/
      ){
        *errorCode = U_INVALID_FORMAT_ERROR;
        udata_close(dataMemory);
        return false;
    }
    profile->isDataLoaded = true;

    /* if a different thread set it first, then close the extra data */
    if(dataMemory!=nullptr) {
        udata_close(dataMemory); /* nullptr if it was set correctly */
    }


    return profile->isDataLoaded;
}

static UStringPrepProfile* 
usprep_getProfile(const char* path, 
                  const char* name,
                  UErrorCode *status){

    UStringPrepProfile* profile = nullptr;

    initCache(status);

    if(U_FAILURE(*status)){
        return nullptr;
    }

    UStringPrepKey stackKey;
    /* 
     * const is cast way to save malloc, strcpy and free calls 
     * we use the passed in pointers for fetching the data from the 
     * hash table which is safe
     */
    stackKey.name = const_cast<char*>(name);
    stackKey.path = const_cast<char*>(path);

    /* fetch the data from the cache */
    umtx_lock(&usprepMutex);
    profile = static_cast<UStringPrepProfile*>(uhash_get(SHARED_DATA_HASHTABLE, &stackKey));
    if(profile != nullptr) {
        profile->refCount++;
    }
    umtx_unlock(&usprepMutex);
    
    if(profile == nullptr) {
        /* else load the data and put the data in the cache */
        LocalMemory<UStringPrepProfile> newProfile;
        if(newProfile.allocateInsteadAndReset() == nullptr) {
            *status = U_MEMORY_ALLOCATION_ERROR;
            return nullptr;
        }

        /* load the data */
        if(!loadData(newProfile.getAlias(), path, name, _SPREP_DATA_TYPE, status) || U_FAILURE(*status) ){
            return nullptr;
        }

        /* get the options */
        newProfile->doNFKC = static_cast<UBool>((newProfile->indexes[_SPREP_OPTIONS] & _SPREP_NORMALIZATION_ON) > 0);
        newProfile->checkBiDi = static_cast<UBool>((newProfile->indexes[_SPREP_OPTIONS] & _SPREP_CHECK_BIDI_ON) > 0);

        LocalMemory<UStringPrepKey> key;
        LocalMemory<char> keyName;
        LocalMemory<char> keyPath;
        if( key.allocateInsteadAndReset() == nullptr ||
            keyName.allocateInsteadAndCopy(static_cast<int32_t>(uprv_strlen(name)+1)) == nullptr ||
            (path != nullptr &&
             keyPath.allocateInsteadAndCopy(static_cast<int32_t>(uprv_strlen(path)+1)) == nullptr)
         ) {
            *status = U_MEMORY_ALLOCATION_ERROR;
            usprep_unload(newProfile.getAlias());
            return nullptr;
        }

        umtx_lock(&usprepMutex);
        // If another thread already inserted the same key/value, refcount and cleanup our thread data
        profile = static_cast<UStringPrepProfile*>(uhash_get(SHARED_DATA_HASHTABLE, &stackKey));
        if(profile != nullptr) {
            profile->refCount++;
            usprep_unload(newProfile.getAlias());
        }
        else {
            /* initialize the key members */
            key->name = keyName.orphan();
            uprv_strcpy(key->name, name);
            if(path != nullptr){
                key->path = keyPath.orphan();
                uprv_strcpy(key->path, path);
            }        
            profile = newProfile.orphan();
    
            /* add the data object to the cache */
            profile->refCount = 1;
            uhash_put(SHARED_DATA_HASHTABLE, key.orphan(), profile, status);
        }
        umtx_unlock(&usprepMutex);
    }

    return profile;
}

U_CAPI UStringPrepProfile* U_EXPORT2
usprep_open(const char* path, 
            const char* name,
            UErrorCode* status){

    if(status == nullptr || U_FAILURE(*status)){
        return nullptr;
    }
       
    /* initialize the profile struct members */
    return usprep_getProfile(path,name,status);
}

U_CAPI UStringPrepProfile* U_EXPORT2
usprep_openByType(UStringPrepProfileType type,
				  UErrorCode* status) {
    if(status == nullptr || U_FAILURE(*status)){
        return nullptr;
    }
    int32_t index = (int32_t)type;
    if (index < 0 || index >= UPRV_LENGTHOF(PROFILE_NAMES)) {
        *status = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }
    return usprep_open(nullptr, PROFILE_NAMES[index], status);
}

U_CAPI void U_EXPORT2
usprep_close(UStringPrepProfile* profile){
    if(profile==nullptr){
        return;
    }

    umtx_lock(&usprepMutex);
    /* decrement the ref count*/
    if(profile->refCount > 0){
        profile->refCount--;
    }
    umtx_unlock(&usprepMutex);
    
}

U_CFUNC void 
uprv_syntaxError(const char16_t* rules,
                 int32_t pos,
                 int32_t rulesLen,
                 UParseError* parseError){
    if(parseError == nullptr){
        return;
    }
    parseError->offset = pos;
    parseError->line = 0 ; // we are not using line numbers 
    
    // for pre-context
    int32_t start = (pos < U_PARSE_CONTEXT_LEN)? 0 : (pos - (U_PARSE_CONTEXT_LEN-1));
    int32_t limit = pos;
    
    u_memcpy(parseError->preContext,rules+start,limit-start);
    //null terminate the buffer
    parseError->preContext[limit-start] = 0;
    
    // for post-context; include error rules[pos]
    start = pos;
    limit = start + (U_PARSE_CONTEXT_LEN-1);
    if (limit > rulesLen) {
        limit = rulesLen;
    }
    if (start < rulesLen) {
        u_memcpy(parseError->postContext,rules+start,limit-start);
    }
    //null terminate the buffer
    parseError->postContext[limit-start]= 0;
}


static inline UStringPrepType
getValues(uint16_t trieWord, int16_t& value, UBool& isIndex){

    UStringPrepType type;
    if(trieWord == 0){
        /* 
         * Initial value stored in the mapping table 
         * just return USPREP_TYPE_LIMIT .. so that
         * the source codepoint is copied to the destination
         */
        type = USPREP_TYPE_LIMIT;
        isIndex =false;
        value = 0;
    }else if(trieWord >= _SPREP_TYPE_THRESHOLD){
        type = static_cast<UStringPrepType>(trieWord - _SPREP_TYPE_THRESHOLD);
        isIndex =false;
        value = 0;
    }else{
        /* get the type */
        type = USPREP_MAP;
        /* ascertain if the value is index or delta */
        if(trieWord & 0x02){
            isIndex = true;
            value = trieWord  >> 2; //mask off the lower 2 bits and shift
        }else{
            isIndex = false;
            value = static_cast<int16_t>(trieWord);
            value =  (value >> 2);
        }
 
        if((trieWord>>2) == _SPREP_MAX_INDEX_VALUE){
            type = USPREP_DELETE;
            isIndex =false;
            value = 0;
        }
    }
    return type;
}

// TODO: change to writing to UnicodeString not char16_t *
static int32_t 
usprep_map(  const UStringPrepProfile* profile, 
             const char16_t* src, int32_t srcLength,
             char16_t* dest, int32_t destCapacity,
             int32_t options,
             UParseError* parseError,
             UErrorCode* status ){
    
    uint16_t result;
    int32_t destIndex=0;
    int32_t srcIndex;
    UBool allowUnassigned = static_cast<UBool>((options & USPREP_ALLOW_UNASSIGNED) > 0);
    UStringPrepType type;
    int16_t value;
    UBool isIndex;
    const int32_t* indexes = profile->indexes;

    // no error checking the caller check for error and arguments
    // no string length check the caller finds out the string length

    for(srcIndex=0;srcIndex<srcLength;){
        UChar32 ch;

        U16_NEXT(src,srcIndex,srcLength,ch);
        
        result=0;

        UTRIE_GET16(&profile->sprepTrie,ch,result);
        
        type = getValues(result, value, isIndex);

        // check if the source codepoint is unassigned
        if(type == USPREP_UNASSIGNED && allowUnassigned == false){

            uprv_syntaxError(src,srcIndex-U16_LENGTH(ch), srcLength,parseError);
            *status = U_STRINGPREP_UNASSIGNED_ERROR;
            return 0;
            
        }else if(type == USPREP_MAP){
            
            int32_t index, length;

            if(isIndex){
                index = value;
                if(index >= indexes[_SPREP_ONE_UCHAR_MAPPING_INDEX_START] &&
                         index < indexes[_SPREP_TWO_UCHARS_MAPPING_INDEX_START]){
                    length = 1;
                }else if(index >= indexes[_SPREP_TWO_UCHARS_MAPPING_INDEX_START] &&
                         index < indexes[_SPREP_THREE_UCHARS_MAPPING_INDEX_START]){
                    length = 2;
                }else if(index >= indexes[_SPREP_THREE_UCHARS_MAPPING_INDEX_START] &&
                         index < indexes[_SPREP_FOUR_UCHARS_MAPPING_INDEX_START]){
                    length = 3;
                }else{
                    length = profile->mappingData[index++];
         
                }

                /* copy mapping to destination */
                for(int32_t i=0; i< length; i++){
                    if(destIndex < destCapacity  ){
                        dest[destIndex] = profile->mappingData[index+i];
                    }
                    destIndex++; /* for pre-flighting */
                }  
                continue;
            }else{
                // subtract the delta to arrive at the code point
                ch -= value;
            }

        }else if(type==USPREP_DELETE){
             // just consume the codepoint and continue
            continue;
        }
        //copy the code point into destination
        if(ch <= 0xFFFF){
            if(destIndex < destCapacity ){
                dest[destIndex] = static_cast<char16_t>(ch);
            }
            destIndex++;
        }else{
            if(destIndex+1 < destCapacity ){
                dest[destIndex]   = U16_LEAD(ch);
                dest[destIndex+1] = U16_TRAIL(ch);
            }
            destIndex +=2;
        }
       
    }
        
    return u_terminateUChars(dest, destCapacity, destIndex, status);
}

/*
   1) Map -- For each character in the input, check if it has a mapping
      and, if so, replace it with its mapping.  

   2) Normalize -- Possibly normalize the result of step 1 using Unicode
      normalization. 

   3) Prohibit -- Check for any characters that are not allowed in the
      output.  If any are found, return an error.  

   4) Check bidi -- Possibly check for right-to-left characters, and if
      any are found, make sure that the whole string satisfies the
      requirements for bidirectional strings.  If the string does not
      satisfy the requirements for bidirectional strings, return an
      error.  
      [Unicode3.2] defines several bidirectional categories; each character
       has one bidirectional category assigned to it.  For the purposes of
       the requirements below, an "RandALCat character" is a character that
       has Unicode bidirectional categories "R" or "AL"; an "LCat character"
       is a character that has Unicode bidirectional category "L".  Note


       that there are many characters which fall in neither of the above
       definitions; Latin digits (<U+0030> through <U+0039>) are examples of
       this because they have bidirectional category "EN".

       In any profile that specifies bidirectional character handling, all
       three of the following requirements MUST be met:

       1) The characters in section 5.8 MUST be prohibited.

       2) If a string contains any RandALCat character, the string MUST NOT
          contain any LCat character.

       3) If a string contains any RandALCat character, a RandALCat
          character MUST be the first character of the string, and a
          RandALCat character MUST be the last character of the string.
*/
U_CAPI int32_t U_EXPORT2
usprep_prepare(   const UStringPrepProfile* profile,
                  const char16_t* src, int32_t srcLength,
                  char16_t* dest, int32_t destCapacity,
                  int32_t options,
                  UParseError* parseError,
                  UErrorCode* status ){

    // check error status
    if(U_FAILURE(*status)){
        return 0;
    }

    //check arguments
    if(profile==nullptr ||
            (src==nullptr ? srcLength!=0 : srcLength<-1) ||
            (dest==nullptr ? destCapacity!=0 : destCapacity<0)) {
        *status=U_ILLEGAL_ARGUMENT_ERROR;
        return 0;
    }

    //get the string length
    if(srcLength < 0){
        srcLength = u_strlen(src);
    }
    // map
    UnicodeString s1;
    char16_t *b1 = s1.getBuffer(srcLength);
    if(b1==nullptr){
        *status = U_MEMORY_ALLOCATION_ERROR;
        return 0;
    }
    int32_t b1Len = usprep_map(profile, src, srcLength,
                               b1, s1.getCapacity(), options, parseError, status);
    s1.releaseBuffer(U_SUCCESS(*status) ? b1Len : 0);

    if(*status == U_BUFFER_OVERFLOW_ERROR){
        // redo processing of string
        /* we do not have enough room so grow the buffer*/
        b1 = s1.getBuffer(b1Len);
        if(b1==nullptr){
            *status = U_MEMORY_ALLOCATION_ERROR;
            return 0;
        }

        *status = U_ZERO_ERROR; // reset error
        b1Len = usprep_map(profile, src, srcLength,
                           b1, s1.getCapacity(), options, parseError, status);
        s1.releaseBuffer(U_SUCCESS(*status) ? b1Len : 0);
    }
    if(U_FAILURE(*status)){
        return 0;
    }

    // normalize
    UnicodeString s2;
    if(profile->doNFKC){
        const Normalizer2 *n2 = Normalizer2::getNFKCInstance(*status);
        FilteredNormalizer2 fn2(*n2, *uniset_getUnicode32Instance(*status));
        if(U_FAILURE(*status)){
            return 0;
        }
        fn2.normalize(s1, s2, *status);
    }else{
        s2.fastCopyFrom(s1);
    }
    if(U_FAILURE(*status)){
        return 0;
    }

    // Prohibit and checkBiDi in one pass
    const char16_t *b2 = s2.getBuffer();
    int32_t b2Len = s2.length();
    UCharDirection direction=U_CHAR_DIRECTION_COUNT, firstCharDir=U_CHAR_DIRECTION_COUNT;
    UBool leftToRight=false, rightToLeft=false;
    int32_t rtlPos =-1, ltrPos =-1;

    for(int32_t b2Index=0; b2Index<b2Len;){
        UChar32 ch = 0;
        U16_NEXT(b2, b2Index, b2Len, ch);

        uint16_t result;
        UTRIE_GET16(&profile->sprepTrie,ch,result);

        int16_t value;
        UBool isIndex;
        UStringPrepType type = getValues(result, value, isIndex);

        if( type == USPREP_PROHIBITED || 
            ((result < _SPREP_TYPE_THRESHOLD) && (result & 0x01) /* first bit says it the code point is prohibited*/)
           ){
            *status = U_STRINGPREP_PROHIBITED_ERROR;
            uprv_syntaxError(b2, b2Index-U16_LENGTH(ch), b2Len, parseError);
            return 0;
        }

        if(profile->checkBiDi) {
            direction = ubidi_getClass(ch);
            if(firstCharDir == U_CHAR_DIRECTION_COUNT){
                firstCharDir = direction;
            }
            if(direction == U_LEFT_TO_RIGHT){
                leftToRight = true;
                ltrPos = b2Index-1;
            }
            if(direction == U_RIGHT_TO_LEFT || direction == U_RIGHT_TO_LEFT_ARABIC){
                rightToLeft = true;
                rtlPos = b2Index-1;
            }
        }
    }
    if(profile->checkBiDi){
        // satisfy 2
        if( leftToRight && rightToLeft){
            *status = U_STRINGPREP_CHECK_BIDI_ERROR;
            uprv_syntaxError(b2,(rtlPos>ltrPos) ? rtlPos : ltrPos, b2Len, parseError);
            return 0;
        }

        //satisfy 3
        if( rightToLeft && 
            !((firstCharDir == U_RIGHT_TO_LEFT || firstCharDir == U_RIGHT_TO_LEFT_ARABIC) &&
              (direction == U_RIGHT_TO_LEFT || direction == U_RIGHT_TO_LEFT_ARABIC))
           ){
            *status = U_STRINGPREP_CHECK_BIDI_ERROR;
            uprv_syntaxError(b2, rtlPos, b2Len, parseError);
            return false;
        }
    }
    return s2.extract(dest, destCapacity, *status);
}


/* data swapping ------------------------------------------------------------ */

U_CAPI int32_t U_EXPORT2
usprep_swap(const UDataSwapper *ds,
            const void *inData, int32_t length, void *outData,
            UErrorCode *pErrorCode) {
    const UDataInfo *pInfo;
    int32_t headerSize;

    const uint8_t *inBytes;
    uint8_t *outBytes;

    const int32_t *inIndexes;
    int32_t indexes[16];

    int32_t i, offset, count, size;

    /* udata_swapDataHeader checks the arguments */
    headerSize=udata_swapDataHeader(ds, inData, length, outData, pErrorCode);
    if(pErrorCode==nullptr || U_FAILURE(*pErrorCode)) {
        return 0;
    }

    /* check data format and format version */
    pInfo=(const UDataInfo *)((const char *)inData+4);
    if(!(
        pInfo->dataFormat[0]==0x53 &&   /* dataFormat="SPRP" */
        pInfo->dataFormat[1]==0x50 &&
        pInfo->dataFormat[2]==0x52 &&
        pInfo->dataFormat[3]==0x50 &&
        pInfo->formatVersion[0]==3
    )) {
        udata_printError(ds, "usprep_swap(): data format %02x.%02x.%02x.%02x (format version %02x) is not recognized as StringPrep .spp data\n",
                         pInfo->dataFormat[0], pInfo->dataFormat[1],
                         pInfo->dataFormat[2], pInfo->dataFormat[3],
                         pInfo->formatVersion[0]);
        *pErrorCode=U_UNSUPPORTED_ERROR;
        return 0;
    }

    inBytes=(const uint8_t *)inData+headerSize;
    outBytes= (outData == nullptr ) ? nullptr : (uint8_t *)outData+headerSize;

    inIndexes=(const int32_t *)inBytes;

    if(length>=0) {
        length-=headerSize;
        if(length<16*4) {
            udata_printError(ds, "usprep_swap(): too few bytes (%d after header) for StringPrep .spp data\n",
                             length);
            *pErrorCode=U_INDEX_OUTOFBOUNDS_ERROR;
            return 0;
        }
    }

    /* read the first 16 indexes (ICU 2.8/format version 3: _SPREP_INDEX_TOP==16, might grow) */
    for(i=0; i<16; ++i) {
        indexes[i]=udata_readInt32(ds, inIndexes[i]);
    }

    /* calculate the total length of the data */
    size=
        16*4+ /* size of indexes[] */
        indexes[_SPREP_INDEX_TRIE_SIZE]+
        indexes[_SPREP_INDEX_MAPPING_DATA_SIZE];

    if(length>=0) {
        if(length<size) {
            udata_printError(ds, "usprep_swap(): too few bytes (%d after header) for all of StringPrep .spp data\n",
                             length);
            *pErrorCode=U_INDEX_OUTOFBOUNDS_ERROR;
            return 0;
        }

        /* copy the data for inaccessible bytes */
        if(inBytes!=outBytes) {
            uprv_memcpy(outBytes, inBytes, size);
        }

        offset=0;

        /* swap the int32_t indexes[] */
        count=16*4;
        ds->swapArray32(ds, inBytes, count, outBytes, pErrorCode);
        offset+=count;

        /* swap the UTrie */
        count=indexes[_SPREP_INDEX_TRIE_SIZE];
        utrie_swap(ds, inBytes+offset, count, outBytes+offset, pErrorCode);
        offset+=count;

        /* swap the uint16_t mappingTable[] */
        count=indexes[_SPREP_INDEX_MAPPING_DATA_SIZE];
        ds->swapArray16(ds, inBytes+offset, count, outBytes+offset, pErrorCode);
        //offset+=count;
    }

    return headerSize+size;
}

#endif /* #if !UCONFIG_NO_IDNA */
