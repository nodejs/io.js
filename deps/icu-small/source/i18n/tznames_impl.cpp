// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2011-2016, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
* File TZNAMES_IMPL.CPP
*
*******************************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/strenum.h"
#include "unicode/stringpiece.h"
#include "unicode/ustring.h"
#include "unicode/timezone.h"
#include "unicode/utf16.h"

#include "tznames_impl.h"
#include "charstr.h"
#include "cmemory.h"
#include "cstring.h"
#include "uassert.h"
#include "mutex.h"
#include "resource.h"
#include "ulocimp.h"
#include "uresimp.h"
#include "ureslocs.h"
#include "zonemeta.h"
#include "ucln_in.h"
#include "uinvchar.h"
#include "uvector.h"
#include "olsontz.h"

U_NAMESPACE_BEGIN

#define ZID_KEY_MAX  128
#define MZ_PREFIX_LEN 5

static const char gZoneStrings[]        = "zoneStrings";
static const char gMZPrefix[]           = "meta:";

static const char EMPTY[]               = "<empty>";   // place holder for empty ZNames
static const char DUMMY_LOADER[]        = "<dummy>";   // place holder for dummy ZNamesLoader
static const char16_t NO_NAME[]            = { 0 };   // for empty no-fallback time zone names

// stuff for TZDBTimeZoneNames
static const char* TZDBNAMES_KEYS[]               = {"ss", "sd"};
static const int32_t TZDBNAMES_KEYS_SIZE = UPRV_LENGTHOF(TZDBNAMES_KEYS);

static UMutex gDataMutex;

static UHashtable* gTZDBNamesMap = nullptr;
static icu::UInitOnce gTZDBNamesMapInitOnce {};

static TextTrieMap* gTZDBNamesTrie = nullptr;
static icu::UInitOnce gTZDBNamesTrieInitOnce {};

// The order in which strings are stored may be different than the order in the public enum.
enum UTimeZoneNameTypeIndex {
    UTZNM_INDEX_UNKNOWN = -1,
    UTZNM_INDEX_EXEMPLAR_LOCATION,
    UTZNM_INDEX_LONG_GENERIC,
    UTZNM_INDEX_LONG_STANDARD,
    UTZNM_INDEX_LONG_DAYLIGHT,
    UTZNM_INDEX_SHORT_GENERIC,
    UTZNM_INDEX_SHORT_STANDARD,
    UTZNM_INDEX_SHORT_DAYLIGHT,
    UTZNM_INDEX_COUNT
};
static const char16_t* const EMPTY_NAMES[UTZNM_INDEX_COUNT] = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr
};

U_CDECL_BEGIN
static UBool U_CALLCONV tzdbTimeZoneNames_cleanup() {
    if (gTZDBNamesMap != nullptr) {
        uhash_close(gTZDBNamesMap);
        gTZDBNamesMap = nullptr;
    }
    gTZDBNamesMapInitOnce.reset();

    if (gTZDBNamesTrie != nullptr) {
        delete gTZDBNamesTrie;
        gTZDBNamesTrie = nullptr;
    }
    gTZDBNamesTrieInitOnce.reset();

    return true;
}
U_CDECL_END

/**
 * ZNameInfo stores zone name information in the trie
 */
struct ZNameInfo {
    UTimeZoneNameType   type;
    const char16_t*        tzID;
    const char16_t*        mzID;
};

/**
 * ZMatchInfo stores zone name match information used by find method
 */
struct ZMatchInfo {
    const ZNameInfo*    znameInfo;
    int32_t             matchLength;
};

// Helper functions
static void mergeTimeZoneKey(const UnicodeString& mzID, char* result, size_t capacity, UErrorCode& status);

#define DEFAULT_CHARACTERNODE_CAPACITY 1

// ---------------------------------------------------
// CharacterNode class implementation
// ---------------------------------------------------
void CharacterNode::clear() {
    uprv_memset(this, 0, sizeof(*this));
}

void CharacterNode::deleteValues(UObjectDeleter *valueDeleter) {
    if (fValues == nullptr) {
        // Do nothing.
    } else if (!fHasValuesVector) {
        if (valueDeleter) {
            valueDeleter(fValues);
        }
    } else {
        delete static_cast<UVector*>(fValues);
    }
}

void
CharacterNode::addValue(void *value, UObjectDeleter *valueDeleter, UErrorCode &status) {
    if (U_FAILURE(status)) {
        if (valueDeleter) {
            valueDeleter(value);
        }
        return;
    }
    if (fValues == nullptr) {
        fValues = value;
    } else {
        // At least one value already.
        if (!fHasValuesVector) {
            // There is only one value so far, and not in a vector yet.
            // Create a vector and add the old value.
            LocalPointer<UVector> values(
                new UVector(valueDeleter, nullptr, DEFAULT_CHARACTERNODE_CAPACITY, status), status);
            if (U_FAILURE(status)) {
                if (valueDeleter) {
                    valueDeleter(value);
                }
                return;
            }
            if (values->hasDeleter()) {
                values->adoptElement(fValues, status);
            } else {
                values->addElement(fValues, status);
            }
            fValues = values.orphan();
            fHasValuesVector = true;
        }
        // Add the new value.
        UVector* values = static_cast<UVector*>(fValues);
        if (values->hasDeleter()) {
            values->adoptElement(value, status);
        } else {
            values->addElement(value, status);
        }
    }
}

// ---------------------------------------------------
// TextTrieMapSearchResultHandler class implementation
// ---------------------------------------------------
TextTrieMapSearchResultHandler::~TextTrieMapSearchResultHandler(){
}

// ---------------------------------------------------
// TextTrieMap class implementation
// ---------------------------------------------------
TextTrieMap::TextTrieMap(UBool ignoreCase, UObjectDeleter *valueDeleter)
: fIgnoreCase(ignoreCase), fNodes(nullptr), fNodesCapacity(0), fNodesCount(0), 
  fLazyContents(nullptr), fIsEmpty(true), fValueDeleter(valueDeleter) {
}

TextTrieMap::~TextTrieMap() {
    int32_t index;
    for (index = 0; index < fNodesCount; ++index) {
        fNodes[index].deleteValues(fValueDeleter);
    }
    uprv_free(fNodes);
    if (fLazyContents != nullptr) {
        for (int32_t i=0; i<fLazyContents->size(); i+=2) {
            if (fValueDeleter) {
                fValueDeleter(fLazyContents->elementAt(i+1));
            }
        } 
        delete fLazyContents;
    }
}

int32_t TextTrieMap::isEmpty() const {
    // Use a separate field for fIsEmpty because it will remain unchanged once the
    //   Trie is built, while fNodes and fLazyContents change with the lazy init
    //   of the nodes structure.  Trying to test the changing fields has
    //   thread safety complications.
    return fIsEmpty;
}


//  We defer actually building the TextTrieMap node structure until the first time a
//     search is performed.  put() simply saves the parameters in case we do
//     eventually need to build it.
//     
void
TextTrieMap::put(const UnicodeString &key, void *value, ZNStringPool &sp, UErrorCode &status) {
    const char16_t *s = sp.get(key, status);
    put(s, value, status);
}

// This method is designed for a persistent key, such as string key stored in
// resource bundle.
void
TextTrieMap::put(const char16_t *key, void *value, UErrorCode &status) {
    fIsEmpty = false;
    if (fLazyContents == nullptr) {
        LocalPointer<UVector> lpLazyContents(new UVector(status), status);
        fLazyContents = lpLazyContents.orphan();
    }
    if (U_SUCCESS(status)) {
        U_ASSERT(fLazyContents != nullptr);
        char16_t *s = const_cast<char16_t *>(key);
        fLazyContents->addElement(s, status);
        if (U_SUCCESS(status)) {
            fLazyContents->addElement(value, status);
            return;
        }
    }
    if (fValueDeleter) {
        fValueDeleter(value);
    }
}

void
TextTrieMap::putImpl(const UnicodeString &key, void *value, UErrorCode &status) {
    if (fNodes == nullptr) {
        fNodesCapacity = 512;
        fNodes = static_cast<CharacterNode*>(uprv_malloc(fNodesCapacity * sizeof(CharacterNode)));
        if (fNodes == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        fNodes[0].clear();  // Init root node.
        fNodesCount = 1;
    }

    UnicodeString foldedKey;
    const char16_t *keyBuffer;
    int32_t keyLength;
    if (fIgnoreCase) {
        // Ok to use fastCopyFrom() because we discard the copy when we return.
        foldedKey.fastCopyFrom(key).foldCase();
        keyBuffer = foldedKey.getBuffer();
        keyLength = foldedKey.length();
    } else {
        keyBuffer = key.getBuffer();
        keyLength = key.length();
    }

    CharacterNode *node = fNodes;
    int32_t index;
    for (index = 0; index < keyLength; ++index) {
        node = addChildNode(node, keyBuffer[index], status);
    }
    node->addValue(value, fValueDeleter, status);
}

UBool
TextTrieMap::growNodes() {
    if (fNodesCapacity == 0xffff) {
        return false;  // We use 16-bit node indexes.
    }
    int32_t newCapacity = fNodesCapacity + 1000;
    if (newCapacity > 0xffff) {
        newCapacity = 0xffff;
    }
    CharacterNode* newNodes = static_cast<CharacterNode*>(uprv_malloc(newCapacity * sizeof(CharacterNode)));
    if (newNodes == nullptr) {
        return false;
    }
    uprv_memcpy(newNodes, fNodes, fNodesCount * sizeof(CharacterNode));
    uprv_free(fNodes);
    fNodes = newNodes;
    fNodesCapacity = newCapacity;
    return true;
}

CharacterNode*
TextTrieMap::addChildNode(CharacterNode *parent, char16_t c, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    // Linear search of the sorted list of children.
    uint16_t prevIndex = 0;
    uint16_t nodeIndex = parent->fFirstChild;
    while (nodeIndex > 0) {
        CharacterNode *current = fNodes + nodeIndex;
        char16_t childCharacter = current->fCharacter;
        if (childCharacter == c) {
            return current;
        } else if (childCharacter > c) {
            break;
        }
        prevIndex = nodeIndex;
        nodeIndex = current->fNextSibling;
    }

    // Ensure capacity. Grow fNodes[] if needed.
    if (fNodesCount == fNodesCapacity) {
        int32_t parentIndex = static_cast<int32_t>(parent - fNodes);
        if (!growNodes()) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return nullptr;
        }
        parent = fNodes + parentIndex;
    }

    // Insert a new child node with c in sorted order.
    CharacterNode *node = fNodes + fNodesCount;
    node->clear();
    node->fCharacter = c;
    node->fNextSibling = nodeIndex;
    if (prevIndex == 0) {
        parent->fFirstChild = static_cast<uint16_t>(fNodesCount);
    } else {
        fNodes[prevIndex].fNextSibling = static_cast<uint16_t>(fNodesCount);
    }
    ++fNodesCount;
    return node;
}

CharacterNode*
TextTrieMap::getChildNode(CharacterNode *parent, char16_t c) const {
    // Linear search of the sorted list of children.
    uint16_t nodeIndex = parent->fFirstChild;
    while (nodeIndex > 0) {
        CharacterNode *current = fNodes + nodeIndex;
        char16_t childCharacter = current->fCharacter;
        if (childCharacter == c) {
            return current;
        } else if (childCharacter > c) {
            break;
        }
        nodeIndex = current->fNextSibling;
    }
    return nullptr;
}


// buildTrie() - The Trie node structure is needed.  Create it from the data that was
//               saved at the time the ZoneStringFormatter was created.  The Trie is only
//               needed for parsing operations, which are less common than formatting,
//               and the Trie is big, which is why its creation is deferred until first use.
void TextTrieMap::buildTrie(UErrorCode &status) {
    if (fLazyContents != nullptr) {
        for (int32_t i=0; i<fLazyContents->size(); i+=2) {
            const char16_t* key = static_cast<char16_t*>(fLazyContents->elementAt(i));
            void  *val = fLazyContents->elementAt(i+1);
            UnicodeString keyString(true, key, -1);  // Aliasing UnicodeString constructor.
            putImpl(keyString, val, status);
        }
        delete fLazyContents;
        fLazyContents = nullptr; 
    }
}

void
TextTrieMap::search(const UnicodeString &text, int32_t start,
                  TextTrieMapSearchResultHandler *handler, UErrorCode &status) const {
    {
        // TODO: if locking the mutex for each check proves to be a performance problem,
        //       add a flag of type atomic_int32_t to class TextTrieMap, and use only
        //       the ICU atomic safe functions for assigning and testing.
        //       Don't test the pointer fLazyContents.
        //       Don't do unless it's really required.

        // Mutex for protecting the lazy creation of the Trie node structure on the first call to search().
        static UMutex TextTrieMutex;

        Mutex lock(&TextTrieMutex);
        if (fLazyContents != nullptr) {
            TextTrieMap *nonConstThis = const_cast<TextTrieMap *>(this);
            nonConstThis->buildTrie(status);
        }
    }
    if (fNodes == nullptr) {
        return;
    }
    search(fNodes, text, start, start, handler, status);
}

void
TextTrieMap::search(CharacterNode *node, const UnicodeString &text, int32_t start,
                  int32_t index, TextTrieMapSearchResultHandler *handler, UErrorCode &status) const {
    if (U_FAILURE(status)) {
        return;
    }
    if (node->hasValues()) {
        if (!handler->handleMatch(index - start, node, status)) {
            return;
        }
        if (U_FAILURE(status)) {
            return;
        }
    }
    if (fIgnoreCase) {
        // for folding we need to get a complete code point.
        // size of character may grow after fold operation;
        // then we need to get result as UTF16 code units.
        UChar32 c32 = text.char32At(index);
        index += U16_LENGTH(c32);
        UnicodeString tmp(c32);
        tmp.foldCase();
        int32_t tmpidx = 0;
        while (tmpidx < tmp.length()) {
            char16_t c = tmp.charAt(tmpidx++);
            node = getChildNode(node, c);
            if (node == nullptr) {
                break;
            }
        }
    } else {
        // here we just get the next UTF16 code unit
        char16_t c = text.charAt(index++);
        node = getChildNode(node, c);
    }
    if (node != nullptr) {
        search(node, text, start, index, handler, status);
    }
}

// ---------------------------------------------------
// ZNStringPool class implementation
// ---------------------------------------------------
static const int32_t POOL_CHUNK_SIZE = 2000;
struct ZNStringPoolChunk: public UMemory {
    ZNStringPoolChunk    *fNext;                       // Ptr to next pool chunk
    int32_t               fLimit;                       // Index to start of unused area at end of fStrings
    char16_t              fStrings[POOL_CHUNK_SIZE];    //  Strings array
    ZNStringPoolChunk();
};

ZNStringPoolChunk::ZNStringPoolChunk() {
    fNext = nullptr;
    fLimit = 0;
}

ZNStringPool::ZNStringPool(UErrorCode &status) {
    fChunks = nullptr;
    fHash   = nullptr;
    if (U_FAILURE(status)) {
        return;
    }
    fChunks = new ZNStringPoolChunk;
    if (fChunks == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }

    fHash   = uhash_open(uhash_hashUChars      /* keyHash */, 
                         uhash_compareUChars   /* keyComp */, 
                         uhash_compareUChars   /* valueComp */, 
                         &status);
    if (U_FAILURE(status)) {
        return;
    }
}

ZNStringPool::~ZNStringPool() {
    if (fHash != nullptr) {
        uhash_close(fHash);
        fHash = nullptr;
    }

    while (fChunks != nullptr) {
        ZNStringPoolChunk *nextChunk = fChunks->fNext;
        delete fChunks;
        fChunks = nextChunk;
    }
}

static const char16_t EmptyString = 0;

const char16_t *ZNStringPool::get(const char16_t *s, UErrorCode &status) {
    const char16_t *pooledString;
    if (U_FAILURE(status)) {
        return &EmptyString;
    }

    pooledString = static_cast<char16_t *>(uhash_get(fHash, s));
    if (pooledString != nullptr) {
        return pooledString;
    }

    int32_t length = u_strlen(s);
    int32_t remainingLength = POOL_CHUNK_SIZE - fChunks->fLimit;
    if (remainingLength <= length) {
        U_ASSERT(length < POOL_CHUNK_SIZE);
        if (length >= POOL_CHUNK_SIZE) {
            status = U_INTERNAL_PROGRAM_ERROR;
            return &EmptyString;
        }
        ZNStringPoolChunk *oldChunk = fChunks;
        fChunks = new ZNStringPoolChunk;
        if (fChunks == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return &EmptyString;
        }
        fChunks->fNext = oldChunk;
    }
    
    char16_t *destString = &fChunks->fStrings[fChunks->fLimit];
    u_strcpy(destString, s);
    fChunks->fLimit += (length + 1);
    uhash_put(fHash, destString, destString, &status);
    return destString;
}        


//
//  ZNStringPool::adopt()    Put a string into the hash, but do not copy the string data
//                           into the pool's storage.  Used for strings from resource bundles,
//                           which will persist for the life of the zone string formatter, and
//                           therefore can be used directly without copying.
const char16_t *ZNStringPool::adopt(const char16_t * s, UErrorCode &status) {
    const char16_t *pooledString;
    if (U_FAILURE(status)) {
        return &EmptyString;
    }
    if (s != nullptr) {
        pooledString = static_cast<char16_t *>(uhash_get(fHash, s));
        if (pooledString == nullptr) {
            char16_t *ncs = const_cast<char16_t *>(s);
            uhash_put(fHash, ncs, ncs, &status);
        }
    }
    return s;
}

    
const char16_t *ZNStringPool::get(const UnicodeString &s, UErrorCode &status) {
    UnicodeString &nonConstStr = const_cast<UnicodeString &>(s);
    return this->get(nonConstStr.getTerminatedBuffer(), status);
}

/*
 * freeze().   Close the hash table that maps to the pooled strings.
 *             After freezing, the pool can not be searched or added to,
 *             but all existing references to pooled strings remain valid.
 *
 *             The main purpose is to recover the storage used for the hash.
 */
void ZNStringPool::freeze() {
    uhash_close(fHash);
    fHash = nullptr;
}


/**
 * This class stores name data for a meta zone or time zone.
 */
class ZNames : public UMemory {
private:
    friend class TimeZoneNamesImpl;

    static UTimeZoneNameTypeIndex getTZNameTypeIndex(UTimeZoneNameType type) {
        switch(type) {
        case UTZNM_EXEMPLAR_LOCATION: return UTZNM_INDEX_EXEMPLAR_LOCATION;
        case UTZNM_LONG_GENERIC: return UTZNM_INDEX_LONG_GENERIC;
        case UTZNM_LONG_STANDARD: return UTZNM_INDEX_LONG_STANDARD;
        case UTZNM_LONG_DAYLIGHT: return UTZNM_INDEX_LONG_DAYLIGHT;
        case UTZNM_SHORT_GENERIC: return UTZNM_INDEX_SHORT_GENERIC;
        case UTZNM_SHORT_STANDARD: return UTZNM_INDEX_SHORT_STANDARD;
        case UTZNM_SHORT_DAYLIGHT: return UTZNM_INDEX_SHORT_DAYLIGHT;
        default: return UTZNM_INDEX_UNKNOWN;
        }
    }
    static UTimeZoneNameType getTZNameType(UTimeZoneNameTypeIndex index) {
        switch(index) {
        case UTZNM_INDEX_EXEMPLAR_LOCATION: return UTZNM_EXEMPLAR_LOCATION;
        case UTZNM_INDEX_LONG_GENERIC: return UTZNM_LONG_GENERIC;
        case UTZNM_INDEX_LONG_STANDARD: return UTZNM_LONG_STANDARD;
        case UTZNM_INDEX_LONG_DAYLIGHT: return UTZNM_LONG_DAYLIGHT;
        case UTZNM_INDEX_SHORT_GENERIC: return UTZNM_SHORT_GENERIC;
        case UTZNM_INDEX_SHORT_STANDARD: return UTZNM_SHORT_STANDARD;
        case UTZNM_INDEX_SHORT_DAYLIGHT: return UTZNM_SHORT_DAYLIGHT;
        default: return UTZNM_UNKNOWN;
        }
    }

    const char16_t* fNames[UTZNM_INDEX_COUNT];
    UBool fDidAddIntoTrie;

    // Whether we own the location string, if computed rather than loaded from a bundle.
    // A meta zone names instance never has an exemplar location string.
    UBool fOwnsLocationName;

    ZNames(const char16_t* names[], const char16_t* locationName)
            : fDidAddIntoTrie(false) {
        uprv_memcpy(fNames, names, sizeof(fNames));
        if (locationName != nullptr) {
            fOwnsLocationName = true;
            fNames[UTZNM_INDEX_EXEMPLAR_LOCATION] = locationName;
        } else {
            fOwnsLocationName = false;
        }
    }

public:
    ~ZNames() {
        if (fOwnsLocationName) {
            const char16_t* locationName = fNames[UTZNM_INDEX_EXEMPLAR_LOCATION];
            U_ASSERT(locationName != nullptr);
            uprv_free((void*) locationName);
        }
    }

private:
    static void* createMetaZoneAndPutInCache(UHashtable* cache, const char16_t* names[],
            const UnicodeString& mzID, UErrorCode& status) {
        if (U_FAILURE(status)) { return nullptr; }
        U_ASSERT(names != nullptr);

        // Use the persistent ID as the resource key, so we can
        // avoid duplications.
        // TODO: Is there a more efficient way, like intern() in Java?
        void* key = (void*) ZoneMeta::findMetaZoneID(mzID);
        void* value;
        if (uprv_memcmp(names, EMPTY_NAMES, sizeof(EMPTY_NAMES)) == 0) {
            value = (void*) EMPTY;
        } else {
            value = (void*) (new ZNames(names, nullptr));
            if (value == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                return nullptr;
            }
        }
        uhash_put(cache, key, value, &status);
        return value;
    }

    static void* createTimeZoneAndPutInCache(UHashtable* cache, const char16_t* names[],
            const UnicodeString& tzID, UErrorCode& status) {
        if (U_FAILURE(status)) { return nullptr; }
        U_ASSERT(names != nullptr);

        // If necessary, compute the location name from the time zone name.
        char16_t* locationName = nullptr;
        if (names[UTZNM_INDEX_EXEMPLAR_LOCATION] == nullptr) {
            UnicodeString locationNameUniStr;
            TimeZoneNamesImpl::getDefaultExemplarLocationName(tzID, locationNameUniStr);

            // Copy the computed location name to the heap
            if (locationNameUniStr.length() > 0) {
                const char16_t* buff = locationNameUniStr.getTerminatedBuffer();
                int32_t len = sizeof(char16_t) * (locationNameUniStr.length() + 1);
                locationName = static_cast<char16_t*>(uprv_malloc(len));
                if (locationName == nullptr) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                    return nullptr;
                }
                uprv_memcpy(locationName, buff, len);
            }
        }

        // Use the persistent ID as the resource key, so we can
        // avoid duplications.
        // TODO: Is there a more efficient way, like intern() in Java?
        void* key = (void*) ZoneMeta::findTimeZoneID(tzID);
        void* value = (void*) (new ZNames(names, locationName));
        if (value == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return nullptr;
        }
        uhash_put(cache, key, value, &status);
        return value;
    }

    const char16_t* getName(UTimeZoneNameType type) const {
        UTimeZoneNameTypeIndex index = getTZNameTypeIndex(type);
        return index >= 0 ? fNames[index] : nullptr;
    }

    void addAsMetaZoneIntoTrie(const char16_t* mzID, TextTrieMap& trie, UErrorCode& status) {
        addNamesIntoTrie(mzID, nullptr, trie, status);
    }
    void addAsTimeZoneIntoTrie(const char16_t* tzID, TextTrieMap& trie, UErrorCode& status) {
        addNamesIntoTrie(nullptr, tzID, trie, status);
    }

    void addNamesIntoTrie(const char16_t* mzID, const char16_t* tzID, TextTrieMap& trie,
            UErrorCode& status) {
        if (U_FAILURE(status)) { return; }
        if (fDidAddIntoTrie) { return; }
        fDidAddIntoTrie = true;

        for (int32_t i = 0; i < UTZNM_INDEX_COUNT; i++) {
            const char16_t* name = fNames[i];
            if (name != nullptr) {
                ZNameInfo* nameinfo = static_cast<ZNameInfo*>(uprv_malloc(sizeof(ZNameInfo)));
                if (nameinfo == nullptr) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                    return;
                }
                nameinfo->mzID = mzID;
                nameinfo->tzID = tzID;
                nameinfo->type = getTZNameType(static_cast<UTimeZoneNameTypeIndex>(i));
                trie.put(name, nameinfo, status); // trie.put() takes ownership of the key
                if (U_FAILURE(status)) {
                    return;
                }
            }
        }
    }

public:
    struct ZNamesLoader;
};

struct ZNames::ZNamesLoader : public ResourceSink {
    const char16_t *names[UTZNM_INDEX_COUNT];

    ZNamesLoader() {
        clear();
    }
    virtual ~ZNamesLoader();

    /** Reset for loading another set of names. */
    void clear() {
        uprv_memcpy(names, EMPTY_NAMES, sizeof(names));
    }

    void loadMetaZone(const UResourceBundle* zoneStrings, const UnicodeString& mzID, UErrorCode& errorCode) {
        if (U_FAILURE(errorCode)) { return; }

        char key[ZID_KEY_MAX + 1];
        mergeTimeZoneKey(mzID, key, sizeof(key), errorCode);

        loadNames(zoneStrings, key, errorCode);
    }

    void loadTimeZone(const UResourceBundle* zoneStrings, const UnicodeString& tzID, UErrorCode& errorCode) {
        // Replace "/" with ":".
        UnicodeString uKey(tzID);
        for (int32_t i = 0; i < uKey.length(); i++) {
            if (uKey.charAt(i) == static_cast<char16_t>(0x2F)) {
                uKey.setCharAt(i, static_cast<char16_t>(0x3A));
            }
        }

        char key[ZID_KEY_MAX + 1];
        if (uKey.length() > ZID_KEY_MAX) {
            errorCode = U_INTERNAL_PROGRAM_ERROR;
            return;
        }
        uKey.extract(0, uKey.length(), key, sizeof(key), US_INV);

        loadNames(zoneStrings, key, errorCode);
    }

    void loadNames(const UResourceBundle* zoneStrings, const char* key, UErrorCode& errorCode) {
        U_ASSERT(zoneStrings != nullptr);
        U_ASSERT(key != nullptr);
        U_ASSERT(key[0] != '\0');

        UErrorCode localStatus = U_ZERO_ERROR;
        clear();
        ures_getAllItemsWithFallback(zoneStrings, key, *this, localStatus);

        // Ignore errors, but propagate possible warnings.
        if (U_SUCCESS(localStatus)) {
            errorCode = localStatus;
        }
    }

    void setNameIfEmpty(const char* key, const ResourceValue* value, UErrorCode& errorCode) {
        UTimeZoneNameTypeIndex type = nameTypeFromKey(key);
        if (type == UTZNM_INDEX_UNKNOWN) { return; }
        if (names[type] == nullptr) {
            int32_t length;
            // 'NO_NAME' indicates internally that this field should remain empty.  It will be
            // replaced by 'nullptr' in getNames()
            names[type] = (value == nullptr) ? NO_NAME : value->getString(length, errorCode);
        }
    }

    virtual void put(const char* key, ResourceValue& value, UBool /*noFallback*/,
            UErrorCode &errorCode) override {
        ResourceTable namesTable = value.getTable(errorCode);
        if (U_FAILURE(errorCode)) { return; }
        for (int32_t i = 0; namesTable.getKeyAndValue(i, key, value); ++i) {
            if (value.isNoInheritanceMarker()) {
                setNameIfEmpty(key, nullptr, errorCode);
            } else {
                setNameIfEmpty(key, &value, errorCode);
            }
        }
    }

    static UTimeZoneNameTypeIndex nameTypeFromKey(const char *key) {
        char c0, c1;
        if ((c0 = key[0]) == 0 || (c1 = key[1]) == 0 || key[2] != 0) {
            return UTZNM_INDEX_UNKNOWN;
        }
        if (c0 == 'l') {
            return c1 == 'g' ? UTZNM_INDEX_LONG_GENERIC :
                    c1 == 's' ? UTZNM_INDEX_LONG_STANDARD :
                        c1 == 'd' ? UTZNM_INDEX_LONG_DAYLIGHT : UTZNM_INDEX_UNKNOWN;
        } else if (c0 == 's') {
            return c1 == 'g' ? UTZNM_INDEX_SHORT_GENERIC :
                    c1 == 's' ? UTZNM_INDEX_SHORT_STANDARD :
                        c1 == 'd' ? UTZNM_INDEX_SHORT_DAYLIGHT : UTZNM_INDEX_UNKNOWN;
        } else if (c0 == 'e' && c1 == 'c') {
            return UTZNM_INDEX_EXEMPLAR_LOCATION;
        }
        return UTZNM_INDEX_UNKNOWN;
    }

    /**
    * Returns an array of names.  It is the caller's responsibility to copy the data into a
    * permanent location, as the returned array is owned by the loader instance and may be
    * cleared or leave scope.
    *
    * This is different than Java, where the array will no longer be modified and null
    * may be returned.
    */
    const char16_t** getNames() {
        // Remove 'NO_NAME' references in the array and replace with 'nullptr'
        for (int32_t i = 0; i < UTZNM_INDEX_COUNT; ++i) {
            if (names[i] == NO_NAME) {
                names[i] = nullptr;
            }
        }
        return names;
    }
};

ZNames::ZNamesLoader::~ZNamesLoader() {}


// ---------------------------------------------------
// The meta zone ID enumeration class
// ---------------------------------------------------
class MetaZoneIDsEnumeration : public StringEnumeration {
public:
    MetaZoneIDsEnumeration();
    MetaZoneIDsEnumeration(const UVector& mzIDs);
    MetaZoneIDsEnumeration(LocalPointer<UVector> mzIDs);
    virtual ~MetaZoneIDsEnumeration();
    static UClassID U_EXPORT2 getStaticClassID();
    virtual UClassID getDynamicClassID() const override;
    virtual const UnicodeString* snext(UErrorCode& status) override;
    virtual void reset(UErrorCode& status) override;
    virtual int32_t count(UErrorCode& status) const override;
private:
    int32_t fLen;
    int32_t fPos;
    const UVector* fMetaZoneIDs;
    LocalPointer<UVector> fLocalVector;
};

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(MetaZoneIDsEnumeration)

MetaZoneIDsEnumeration::MetaZoneIDsEnumeration() 
: fLen(0), fPos(0), fMetaZoneIDs(nullptr), fLocalVector(nullptr) {
}

MetaZoneIDsEnumeration::MetaZoneIDsEnumeration(const UVector& mzIDs) 
: fPos(0), fMetaZoneIDs(&mzIDs), fLocalVector(nullptr) {
    fLen = fMetaZoneIDs->size();
}

MetaZoneIDsEnumeration::MetaZoneIDsEnumeration(LocalPointer<UVector> mzIDs)
: fLen(0), fPos(0), fMetaZoneIDs(nullptr), fLocalVector(std::move(mzIDs)) {
    fMetaZoneIDs = fLocalVector.getAlias();
    if (fMetaZoneIDs) {
        fLen = fMetaZoneIDs->size();
    }
}

const UnicodeString*
MetaZoneIDsEnumeration::snext(UErrorCode& status) {
    if (U_SUCCESS(status) && fMetaZoneIDs != nullptr && fPos < fLen) {
        unistr.setTo(static_cast<const char16_t*>(fMetaZoneIDs->elementAt(fPos++)), -1);
        return &unistr;
    }
    return nullptr;
}

void
MetaZoneIDsEnumeration::reset(UErrorCode& /*status*/) {
    fPos = 0;
}

int32_t
MetaZoneIDsEnumeration::count(UErrorCode& /*status*/) const {
    return fLen;
}

MetaZoneIDsEnumeration::~MetaZoneIDsEnumeration() {
}


// ---------------------------------------------------
// ZNameSearchHandler
// ---------------------------------------------------
class ZNameSearchHandler : public TextTrieMapSearchResultHandler {
public:
    ZNameSearchHandler(uint32_t types);
    virtual ~ZNameSearchHandler();

    UBool handleMatch(int32_t matchLength, const CharacterNode *node, UErrorCode &status) override;
    TimeZoneNames::MatchInfoCollection* getMatches(int32_t& maxMatchLen);

private:
    uint32_t fTypes;
    int32_t fMaxMatchLen;
    TimeZoneNames::MatchInfoCollection* fResults;
};

ZNameSearchHandler::ZNameSearchHandler(uint32_t types) 
: fTypes(types), fMaxMatchLen(0), fResults(nullptr) {
}

ZNameSearchHandler::~ZNameSearchHandler() {
    delete fResults;
}

UBool
ZNameSearchHandler::handleMatch(int32_t matchLength, const CharacterNode *node, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return false;
    }
    if (node->hasValues()) {
        int32_t valuesCount = node->countValues();
        for (int32_t i = 0; i < valuesCount; i++) {
            ZNameInfo *nameinfo = (ZNameInfo *)node->getValue(i);
            if (nameinfo == nullptr) {
                continue;
            }
            if ((nameinfo->type & fTypes) != 0) {
                // matches a requested type
                if (fResults == nullptr) {
                    fResults = new TimeZoneNames::MatchInfoCollection();
                    if (fResults == nullptr) {
                        status = U_MEMORY_ALLOCATION_ERROR;
                    }
                }
                if (U_SUCCESS(status)) {
                    U_ASSERT(fResults != nullptr);
                    if (nameinfo->tzID) {
                        fResults->addZone(nameinfo->type, matchLength, UnicodeString(nameinfo->tzID, -1), status);
                    } else {
                        U_ASSERT(nameinfo->mzID);
                        fResults->addMetaZone(nameinfo->type, matchLength, UnicodeString(nameinfo->mzID, -1), status);
                    }
                    if (U_SUCCESS(status) && matchLength > fMaxMatchLen) {
                        fMaxMatchLen = matchLength;
                    }
                }
            }
        }
    }
    return true;
}

TimeZoneNames::MatchInfoCollection*
ZNameSearchHandler::getMatches(int32_t& maxMatchLen) {
    // give the ownership to the caller
    TimeZoneNames::MatchInfoCollection* results = fResults;
    maxMatchLen = fMaxMatchLen;

    // reset
    fResults = nullptr;
    fMaxMatchLen = 0;
    return results;
}

// ---------------------------------------------------
// TimeZoneNamesImpl
//
// TimeZoneNames implementation class. This is the main
// part of this module.
// ---------------------------------------------------

U_CDECL_BEGIN
/**
 * Deleter for ZNames
 */
static void U_CALLCONV
deleteZNames(void *obj) {
    if (obj != EMPTY) {
        delete (ZNames*) obj;
    }
}

/**
 * Deleter for ZNameInfo
 */
static void U_CALLCONV
deleteZNameInfo(void *obj) {
    uprv_free(obj);
}

U_CDECL_END

TimeZoneNamesImpl::TimeZoneNamesImpl(const Locale& locale, UErrorCode& status)
: fLocale(locale),
  fZoneStrings(nullptr),
  fTZNamesMap(nullptr),
  fMZNamesMap(nullptr),
  fNamesTrieFullyLoaded(false),
  fNamesFullyLoaded(false),
  fNamesTrie(true, deleteZNameInfo) {
    initialize(locale, status);
}

void
TimeZoneNamesImpl::initialize(const Locale& locale, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }

    // Load zoneStrings bundle
    UErrorCode tmpsts = U_ZERO_ERROR;   // OK with fallback warning..
    fZoneStrings = ures_open(U_ICUDATA_ZONE, locale.getName(), &tmpsts);
    fZoneStrings = ures_getByKeyWithFallback(fZoneStrings, gZoneStrings, fZoneStrings, &tmpsts);
    if (U_FAILURE(tmpsts)) {
        status = tmpsts;
        cleanup();
        return;
    }

    // Initialize hashtables holding time zone/meta zone names
    fMZNamesMap = uhash_open(uhash_hashUChars, uhash_compareUChars, nullptr, &status);
    fTZNamesMap = uhash_open(uhash_hashUChars, uhash_compareUChars, nullptr, &status);
    if (U_FAILURE(status)) {
        cleanup();
        return;
    }

    uhash_setValueDeleter(fMZNamesMap, deleteZNames);
    uhash_setValueDeleter(fTZNamesMap, deleteZNames);
    // no key deleters for name maps

    // preload zone strings for the default zone
    TimeZone *tz = TimeZone::createDefault();
    const char16_t *tzID = ZoneMeta::getCanonicalCLDRID(*tz);
    if (tzID != nullptr) {
        loadStrings(UnicodeString(tzID), status);
    }
    delete tz;
}

/*
 * This method updates the cache and must be called with a lock,
 * except initializer.
 */
void
TimeZoneNamesImpl::loadStrings(const UnicodeString& tzCanonicalID, UErrorCode& status) {
    loadTimeZoneNames(tzCanonicalID, status);
    LocalPointer<StringEnumeration> mzIDs(getAvailableMetaZoneIDs(tzCanonicalID, status));
    if (U_FAILURE(status)) { return; }
    U_ASSERT(!mzIDs.isNull());

    const UnicodeString *mzID;
    while (((mzID = mzIDs->snext(status)) != nullptr) && U_SUCCESS(status)) {
        loadMetaZoneNames(*mzID, status);
    }
}

TimeZoneNamesImpl::~TimeZoneNamesImpl() {
    cleanup();
}

void
TimeZoneNamesImpl::cleanup() {
    if (fZoneStrings != nullptr) {
        ures_close(fZoneStrings);
        fZoneStrings = nullptr;
    }
    if (fMZNamesMap != nullptr) {
        uhash_close(fMZNamesMap);
        fMZNamesMap = nullptr;
    }
    if (fTZNamesMap != nullptr) {
        uhash_close(fTZNamesMap);
        fTZNamesMap = nullptr;
    }
}

bool
TimeZoneNamesImpl::operator==(const TimeZoneNames& other) const {
    if (this == &other) {
        return true;
    }
    // No implementation for now
    return false;
}

TimeZoneNamesImpl*
TimeZoneNamesImpl::clone() const {
    UErrorCode status = U_ZERO_ERROR;
    return new TimeZoneNamesImpl(fLocale, status);
}

StringEnumeration*
TimeZoneNamesImpl::getAvailableMetaZoneIDs(UErrorCode& status) const {
    return TimeZoneNamesImpl::_getAvailableMetaZoneIDs(status);
}

// static implementation of getAvailableMetaZoneIDs(UErrorCode&)
StringEnumeration*
TimeZoneNamesImpl::_getAvailableMetaZoneIDs(UErrorCode& status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    const UVector* mzIDs = ZoneMeta::getAvailableMetazoneIDs();
    if (mzIDs == nullptr) {
        return new MetaZoneIDsEnumeration();
    }
    return new MetaZoneIDsEnumeration(*mzIDs);
}

StringEnumeration*
TimeZoneNamesImpl::getAvailableMetaZoneIDs(const UnicodeString& tzID, UErrorCode& status) const {
    return TimeZoneNamesImpl::_getAvailableMetaZoneIDs(tzID, status);
}

// static implementation of getAvailableMetaZoneIDs(const UnicodeString&, UErrorCode&)
StringEnumeration*
TimeZoneNamesImpl::_getAvailableMetaZoneIDs(const UnicodeString& tzID, UErrorCode& status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    const UVector* mappings = ZoneMeta::getMetazoneMappings(tzID);
    if (mappings == nullptr) {
        return new MetaZoneIDsEnumeration();
    }

    LocalPointer<MetaZoneIDsEnumeration> senum;
    LocalPointer<UVector> mzIDs(new UVector(nullptr, uhash_compareUChars, status), status);
    if (U_SUCCESS(status)) {
        U_ASSERT(mzIDs.isValid());
        for (int32_t i = 0; U_SUCCESS(status) && i < mappings->size(); i++) {

            OlsonToMetaMappingEntry* map = static_cast<OlsonToMetaMappingEntry*>(mappings->elementAt(i));
            const char16_t *mzID = map->mzid;
            if (!mzIDs->contains((void *)mzID)) {
                mzIDs->addElement((void *)mzID, status);
            }
        }
        if (U_SUCCESS(status)) {
            senum.adoptInsteadAndCheckErrorCode(new MetaZoneIDsEnumeration(std::move(mzIDs)), status);
        }
    }
    return U_SUCCESS(status) ? senum.orphan() : nullptr;
}

UnicodeString&
TimeZoneNamesImpl::getMetaZoneID(const UnicodeString& tzID, UDate date, UnicodeString& mzID) const {
    return TimeZoneNamesImpl::_getMetaZoneID(tzID, date, mzID);
}

// static implementation of getMetaZoneID
UnicodeString&
TimeZoneNamesImpl::_getMetaZoneID(const UnicodeString& tzID, UDate date, UnicodeString& mzID) {
    ZoneMeta::getMetazoneID(tzID, date, mzID);
    return mzID;
}

UnicodeString&
TimeZoneNamesImpl::getReferenceZoneID(const UnicodeString& mzID, const char* region, UnicodeString& tzID) const {
    return TimeZoneNamesImpl::_getReferenceZoneID(mzID, region, tzID);
}

// static implementation of getReferenceZoneID
UnicodeString&
TimeZoneNamesImpl::_getReferenceZoneID(const UnicodeString& mzID, const char* region, UnicodeString& tzID) {
    ZoneMeta::getZoneIdByMetazone(mzID, UnicodeString(region, -1, US_INV), tzID);
    return tzID;
}

UnicodeString&
TimeZoneNamesImpl::getMetaZoneDisplayName(const UnicodeString& mzID,
                                          UTimeZoneNameType type,
                                          UnicodeString& name) const {
    name.setToBogus();  // cleanup result.
    if (mzID.isEmpty()) {
        return name;
    }

    ZNames *znames = nullptr;
    TimeZoneNamesImpl *nonConstThis = const_cast<TimeZoneNamesImpl *>(this);

    {
        Mutex lock(&gDataMutex);
        UErrorCode status = U_ZERO_ERROR;
        znames = nonConstThis->loadMetaZoneNames(mzID, status);
        if (U_FAILURE(status)) { return name; }
    }

    if (znames != nullptr) {
        const char16_t* s = znames->getName(type);
        if (s != nullptr) {
            name.setTo(true, s, -1);
        }
    }
    return name;
}

UnicodeString&
TimeZoneNamesImpl::getTimeZoneDisplayName(const UnicodeString& tzID, UTimeZoneNameType type, UnicodeString& name) const {
    name.setToBogus();  // cleanup result.
    if (tzID.isEmpty()) {
        return name;
    }

    ZNames *tznames = nullptr;
    TimeZoneNamesImpl *nonConstThis = const_cast<TimeZoneNamesImpl *>(this);

    {
        Mutex lock(&gDataMutex);
        UErrorCode status = U_ZERO_ERROR;
        tznames = nonConstThis->loadTimeZoneNames(tzID, status);
        if (U_FAILURE(status)) { return name; }
    }

    if (tznames != nullptr) {
        const char16_t *s = tznames->getName(type);
        if (s != nullptr) {
            name.setTo(true, s, -1);
        }
    }
    return name;
}

UnicodeString&
TimeZoneNamesImpl::getExemplarLocationName(const UnicodeString& tzID, UnicodeString& name) const {
    name.setToBogus();  // cleanup result.
    const char16_t* locName = nullptr;
    ZNames *tznames = nullptr;
    TimeZoneNamesImpl *nonConstThis = const_cast<TimeZoneNamesImpl *>(this);

    {
        Mutex lock(&gDataMutex);
        UErrorCode status = U_ZERO_ERROR;
        tznames = nonConstThis->loadTimeZoneNames(tzID, status);
        if (U_FAILURE(status)) { return name; }
    }

    if (tznames != nullptr) {
        locName = tznames->getName(UTZNM_EXEMPLAR_LOCATION);
    }
    if (locName != nullptr) {
        name.setTo(true, locName, -1);
    }

    return name;
}


// Merge the MZ_PREFIX and mzId
static void mergeTimeZoneKey(const UnicodeString& mzID, char* result, size_t capacity,
                             UErrorCode& status) {
    if (U_FAILURE(status)) {
        return;
    }
    if (mzID.isEmpty()) {
        result[0] = '\0';
        return;
    }

    if (MZ_PREFIX_LEN + 1 > capacity) {
        result[0] = '\0';
        status = U_INTERNAL_PROGRAM_ERROR;
        return;
    }
    uprv_memcpy((void *)result, (void *)gMZPrefix, MZ_PREFIX_LEN);
    if (static_cast<size_t>(MZ_PREFIX_LEN +  mzID.length() + 1) > capacity) {
        result[0] = '\0';
        status = U_INTERNAL_PROGRAM_ERROR;
        return;
    }
    int32_t keyLen = mzID.extract(0, mzID.length(), result + MZ_PREFIX_LEN,
                                  static_cast<int32_t>(capacity - MZ_PREFIX_LEN), US_INV);
    result[keyLen + MZ_PREFIX_LEN] = '\0';
}

/*
 * This method updates the cache and must be called with a lock
 */
ZNames*
TimeZoneNamesImpl::loadMetaZoneNames(const UnicodeString& mzID, UErrorCode& status) {
    if (U_FAILURE(status)) { return nullptr; }
    if (mzID.length() > ZID_KEY_MAX - MZ_PREFIX_LEN) {
        status = U_INTERNAL_PROGRAM_ERROR;
        return nullptr;
    }

    char16_t mzIDKey[ZID_KEY_MAX + 1];
    mzID.extract(mzIDKey, ZID_KEY_MAX, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    mzIDKey[mzID.length()] = 0;

    void* mznames = uhash_get(fMZNamesMap, mzIDKey);
    if (mznames == nullptr) {
        ZNames::ZNamesLoader loader;
        loader.loadMetaZone(fZoneStrings, mzID, status);
        mznames = ZNames::createMetaZoneAndPutInCache(fMZNamesMap, loader.getNames(), mzID, status);
        if (U_FAILURE(status)) { return nullptr; }
    }

    if (mznames != EMPTY) {
        return static_cast<ZNames*>(mznames);
    } else {
        return nullptr;
    }
}

/*
 * This method updates the cache and must be called with a lock
 */
ZNames*
TimeZoneNamesImpl::loadTimeZoneNames(const UnicodeString& tzID, UErrorCode& status) {
    if (U_FAILURE(status)) { return nullptr; }
    if (tzID.length() > ZID_KEY_MAX) {
        status = U_INTERNAL_PROGRAM_ERROR;
        return nullptr;
    }

    char16_t tzIDKey[ZID_KEY_MAX + 1];
    int32_t tzIDKeyLen = tzID.extract(tzIDKey, ZID_KEY_MAX, status);
    U_ASSERT(U_SUCCESS(status));   // already checked length above
    tzIDKey[tzIDKeyLen] = 0;

    void *tznames = uhash_get(fTZNamesMap, tzIDKey);
    if (tznames == nullptr) {
        ZNames::ZNamesLoader loader;
        loader.loadTimeZone(fZoneStrings, tzID, status);
        tznames = ZNames::createTimeZoneAndPutInCache(fTZNamesMap, loader.getNames(), tzID, status);
        if (U_FAILURE(status)) { return nullptr; }
    }

    // tznames is never EMPTY
    return static_cast<ZNames*>(tznames);
}

TimeZoneNames::MatchInfoCollection*
TimeZoneNamesImpl::find(const UnicodeString& text, int32_t start, uint32_t types, UErrorCode& status) const {
    ZNameSearchHandler handler(types);
    TimeZoneNames::MatchInfoCollection* matches;
    TimeZoneNamesImpl* nonConstThis = const_cast<TimeZoneNamesImpl*>(this);

    // Synchronize so that data is not loaded multiple times.
    // TODO: Consider more fine-grained synchronization.
    {
        Mutex lock(&gDataMutex);

        // First try of lookup.
        matches = doFind(handler, text, start, status);
        if (U_FAILURE(status)) { return nullptr; }
        if (matches != nullptr) {
            return matches;
        }

        // All names are not yet loaded into the trie.
        // We may have loaded names for formatting several time zones,
        // and might be parsing one of those.
        // Populate the parsing trie from all of the already-loaded names.
        nonConstThis->addAllNamesIntoTrie(status);

        // Second try of lookup.
        matches = doFind(handler, text, start, status);
        if (U_FAILURE(status)) { return nullptr; }
        if (matches != nullptr) {
            return matches;
        }

        // There are still some names we haven't loaded into the trie yet.
        // Load everything now.
        nonConstThis->internalLoadAllDisplayNames(status);
        nonConstThis->addAllNamesIntoTrie(status);
        nonConstThis->fNamesTrieFullyLoaded = true;
        if (U_FAILURE(status)) { return nullptr; }

        // Third try: we must return this one.
        return doFind(handler, text, start, status);
    }
}

TimeZoneNames::MatchInfoCollection*
TimeZoneNamesImpl::doFind(ZNameSearchHandler& handler,
        const UnicodeString& text, int32_t start, UErrorCode& status) const {

    fNamesTrie.search(text, start, (TextTrieMapSearchResultHandler *)&handler, status);
    if (U_FAILURE(status)) { return nullptr; }

    int32_t maxLen = 0;
    TimeZoneNames::MatchInfoCollection* matches = handler.getMatches(maxLen);
    if (matches != nullptr && ((maxLen == (text.length() - start)) || fNamesTrieFullyLoaded)) {
        // perfect match, or no more names available
        return matches;
    }
    delete matches;
    return nullptr;
}

// Caller must synchronize.
void TimeZoneNamesImpl::addAllNamesIntoTrie(UErrorCode& status) {
    if (U_FAILURE(status)) return;
    int32_t pos;
    const UHashElement* element;

    pos = UHASH_FIRST;
    while ((element = uhash_nextElement(fMZNamesMap, &pos)) != nullptr) {
        if (element->value.pointer == EMPTY) { continue; }
        char16_t* mzID = static_cast<char16_t*>(element->key.pointer);
        ZNames* znames = static_cast<ZNames*>(element->value.pointer);
        znames->addAsMetaZoneIntoTrie(mzID, fNamesTrie, status);
        if (U_FAILURE(status)) { return; }
    }

    pos = UHASH_FIRST;
    while ((element = uhash_nextElement(fTZNamesMap, &pos)) != nullptr) {
        if (element->value.pointer == EMPTY) { continue; }
        char16_t* tzID = static_cast<char16_t*>(element->key.pointer);
        ZNames* znames = static_cast<ZNames*>(element->value.pointer);
        znames->addAsTimeZoneIntoTrie(tzID, fNamesTrie, status);
        if (U_FAILURE(status)) { return; }
    }
}

U_CDECL_BEGIN
static void U_CALLCONV
deleteZNamesLoader(void* obj) {
    if (obj == DUMMY_LOADER) { return; }
    const ZNames::ZNamesLoader* loader = (const ZNames::ZNamesLoader*) obj;
    delete loader;
}
U_CDECL_END

struct TimeZoneNamesImpl::ZoneStringsLoader : public ResourceSink {
    TimeZoneNamesImpl& tzn;
    UHashtable* keyToLoader;

    ZoneStringsLoader(TimeZoneNamesImpl& _tzn, UErrorCode& status)
            : tzn(_tzn) {
        keyToLoader = uhash_open(uhash_hashChars, uhash_compareChars, nullptr, &status);
        if (U_FAILURE(status)) { return; }
        uhash_setKeyDeleter(keyToLoader, uprv_free);
        uhash_setValueDeleter(keyToLoader, deleteZNamesLoader);
    }
    virtual ~ZoneStringsLoader();

    void* createKey(const char* key, UErrorCode& status) {
        int32_t len = sizeof(char) * (static_cast<int32_t>(uprv_strlen(key)) + 1);
        char* newKey = static_cast<char*>(uprv_malloc(len));
        if (newKey == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return nullptr;
        }
        uprv_memcpy(newKey, key, len);
        newKey[len-1] = '\0';
        return (void*) newKey;
    }

    UBool isMetaZone(const char* key) {
        return (uprv_strlen(key) >= MZ_PREFIX_LEN && uprv_memcmp(key, gMZPrefix, MZ_PREFIX_LEN) == 0);
    }

    UnicodeString mzIDFromKey(const char* key) {
        return UnicodeString(key + MZ_PREFIX_LEN, static_cast<int32_t>(uprv_strlen(key)) - MZ_PREFIX_LEN, US_INV);
    }

    UnicodeString tzIDFromKey(const char* key) {
        UnicodeString tzID(key, -1, US_INV);
        // Replace all colons ':' with slashes '/'
        for (int i=0; i<tzID.length(); i++) {
            if (tzID.charAt(i) == 0x003A) {
                tzID.setCharAt(i, 0x002F);
            }
        }
        return tzID;
    }

    void load(UErrorCode& status) {
        ures_getAllItemsWithFallback(tzn.fZoneStrings, "", *this, status);
        if (U_FAILURE(status)) { return; }

        int32_t pos = UHASH_FIRST;
        const UHashElement* element;
        while ((element = uhash_nextElement(keyToLoader, &pos)) != nullptr) {
            if (element->value.pointer == DUMMY_LOADER) { continue; }
            ZNames::ZNamesLoader* loader = static_cast<ZNames::ZNamesLoader*>(element->value.pointer);
            char* key = static_cast<char*>(element->key.pointer);

            if (isMetaZone(key)) {
                UnicodeString mzID = mzIDFromKey(key);
                ZNames::createMetaZoneAndPutInCache(tzn.fMZNamesMap, loader->getNames(), mzID, status);
            } else {
                UnicodeString tzID = tzIDFromKey(key);
                ZNames::createTimeZoneAndPutInCache(tzn.fTZNamesMap, loader->getNames(), tzID, status);
            }
            if (U_FAILURE(status)) { return; }
        }
    }

    void consumeNamesTable(const char *key, ResourceValue &value, UBool noFallback,
            UErrorCode &status) {
        if (U_FAILURE(status)) { return; }

        void* loader = uhash_get(keyToLoader, key);
        if (loader == nullptr) {
            if (isMetaZone(key)) {
                UnicodeString mzID = mzIDFromKey(key);
                void* cacheVal = uhash_get(tzn.fMZNamesMap, mzID.getTerminatedBuffer());
                if (cacheVal != nullptr) {
                    // We have already loaded the names for this meta zone.
                    loader = (void*) DUMMY_LOADER;
                } else {
                    loader = (void*) new ZNames::ZNamesLoader();
                    if (loader == nullptr) {
                        status = U_MEMORY_ALLOCATION_ERROR;
                        return;
                    }
                }
            } else {
                UnicodeString tzID = tzIDFromKey(key);
                void* cacheVal = uhash_get(tzn.fTZNamesMap, tzID.getTerminatedBuffer());
                if (cacheVal != nullptr) {
                    // We have already loaded the names for this time zone.
                    loader = (void*) DUMMY_LOADER;
                } else {
                    loader = (void*) new ZNames::ZNamesLoader();
                    if (loader == nullptr) {
                        status = U_MEMORY_ALLOCATION_ERROR;
                        return;
                    }
                }
            }

            void* newKey = createKey(key, status);
            if (U_FAILURE(status)) {
                deleteZNamesLoader(loader);
                return;
            }

            uhash_put(keyToLoader, newKey, loader, &status);
            if (U_FAILURE(status)) { return; }
        }

        if (loader != DUMMY_LOADER) {
            // Let the ZNamesLoader consume the names table.
            static_cast<ZNames::ZNamesLoader*>(loader)->put(key, value, noFallback, status);
        }
    }

    virtual void put(const char *key, ResourceValue &value, UBool noFallback,
            UErrorCode &status) override {
        ResourceTable timeZonesTable = value.getTable(status);
        if (U_FAILURE(status)) { return; }
        for (int32_t i = 0; timeZonesTable.getKeyAndValue(i, key, value); ++i) {
            U_ASSERT(!value.isNoInheritanceMarker());
            if (value.getType() == URES_TABLE) {
                consumeNamesTable(key, value, noFallback, status);
            } else {
                // Ignore fields that aren't tables (e.g., fallbackFormat and regionFormatStandard).
                // All time zone fields are tables.
            }
            if (U_FAILURE(status)) { return; }
        }
    }
};

// Virtual destructors must be defined out of line.
TimeZoneNamesImpl::ZoneStringsLoader::~ZoneStringsLoader() {
    uhash_close(keyToLoader);
}

void TimeZoneNamesImpl::loadAllDisplayNames(UErrorCode& status) {
    if (U_FAILURE(status)) return;

    {
        Mutex lock(&gDataMutex);
        internalLoadAllDisplayNames(status);
    }
}

void TimeZoneNamesImpl::getDisplayNames(const UnicodeString& tzID,
        const UTimeZoneNameType types[], int32_t numTypes,
        UDate date, UnicodeString dest[], UErrorCode& status) const {
    if (U_FAILURE(status)) return;

    if (tzID.isEmpty()) { return; }
    void* tznames = nullptr;
    void* mznames = nullptr;
    TimeZoneNamesImpl *nonConstThis = const_cast<TimeZoneNamesImpl*>(this);

    // Load the time zone strings
    {
        Mutex lock(&gDataMutex);
        tznames = (void*) nonConstThis->loadTimeZoneNames(tzID, status);
        if (U_FAILURE(status)) { return; }
    }
    U_ASSERT(tznames != nullptr);

    // Load the values into the dest array
    for (int i = 0; i < numTypes; i++) {
        UTimeZoneNameType type = types[i];
        const char16_t* name = static_cast<ZNames*>(tznames)->getName(type);
        if (name == nullptr) {
            if (mznames == nullptr) {
                // Load the meta zone name
                UnicodeString mzID;
                getMetaZoneID(tzID, date, mzID);
                if (mzID.isEmpty()) {
                    mznames = (void*) EMPTY;
                } else {
                    // Load the meta zone strings
                    // Mutex is scoped to the "else" statement
                    Mutex lock(&gDataMutex);
                    mznames = (void*) nonConstThis->loadMetaZoneNames(mzID, status);
                    if (U_FAILURE(status)) { return; }
                    // Note: when the metazone doesn't exist, in Java, loadMetaZoneNames returns
                    // a dummy object instead of nullptr.
                    if (mznames == nullptr) {
                        mznames = (void*) EMPTY;
                    }
                }
            }
            U_ASSERT(mznames != nullptr);
            if (mznames != EMPTY) {
                name = static_cast<ZNames*>(mznames)->getName(type);
            }
        }
        if (name != nullptr) {
            dest[i].setTo(true, name, -1);
        } else {
            dest[i].setToBogus();
        }
    }
}

// Caller must synchronize.
void TimeZoneNamesImpl::internalLoadAllDisplayNames(UErrorCode& status) {
    if (!fNamesFullyLoaded) {
        fNamesFullyLoaded = true;

        ZoneStringsLoader loader(*this, status);
        loader.load(status);
        if (U_FAILURE(status)) { return; }

        const UnicodeString *id;

        // load strings for all zones
        StringEnumeration *tzIDs = TimeZone::createTimeZoneIDEnumeration(
            UCAL_ZONE_TYPE_CANONICAL, nullptr, nullptr, status);
        if (U_SUCCESS(status)) {
            while ((id = tzIDs->snext(status)) != nullptr) {
                if (U_FAILURE(status)) {
                    break;
                }
                UnicodeString copy(*id);
                void* value = uhash_get(fTZNamesMap, copy.getTerminatedBuffer());
                if (value == nullptr) {
                    // loadStrings also loads related metazone strings
                    loadStrings(*id, status);
                }
            }
        }
        delete tzIDs;
    }
}



static const char16_t gEtcPrefix[]         = { 0x45, 0x74, 0x63, 0x2F }; // "Etc/"
static const int32_t gEtcPrefixLen      = 4;
static const char16_t gSystemVPrefix[]     = { 0x53, 0x79, 0x73, 0x74, 0x65, 0x6D, 0x56, 0x2F }; // "SystemV/
static const int32_t gSystemVPrefixLen  = 8;
static const char16_t gRiyadh8[]           = { 0x52, 0x69, 0x79, 0x61, 0x64, 0x68, 0x38 }; // "Riyadh8"
static const int32_t gRiyadh8Len       = 7;

UnicodeString& U_EXPORT2
TimeZoneNamesImpl::getDefaultExemplarLocationName(const UnicodeString& tzID, UnicodeString& name) {
    if (tzID.isEmpty() || tzID.startsWith(gEtcPrefix, gEtcPrefixLen)
        || tzID.startsWith(gSystemVPrefix, gSystemVPrefixLen) || tzID.indexOf(gRiyadh8, gRiyadh8Len, 0) > 0) {
        name.setToBogus();
        return name;
    }

    int32_t sep = tzID.lastIndexOf(static_cast<char16_t>(0x2F) /* '/' */);
    if (sep > 0 && sep + 1 < tzID.length()) {
        name.setTo(tzID, sep + 1);
        name.findAndReplace(UnicodeString(static_cast<char16_t>(0x5f) /* _ */),
                            UnicodeString(static_cast<char16_t>(0x20) /* space */));
    } else {
        name.setToBogus();
    }
    return name;
}

// ---------------------------------------------------
// TZDBTimeZoneNames and its supporting classes
//
// TZDBTimeZoneNames is an implementation class of
// TimeZoneNames holding the IANA tz database abbreviations.
// ---------------------------------------------------

class TZDBNames : public UMemory {
public:
    virtual ~TZDBNames();

    static TZDBNames* createInstance(UResourceBundle* rb, const char* key);
    const char16_t* getName(UTimeZoneNameType type) const;
    const char** getParseRegions(int32_t& numRegions) const;

protected:
    TZDBNames(const char16_t** names, char** regions, int32_t numRegions);

private:
    const char16_t** fNames;
    char** fRegions;
    int32_t fNumRegions;
};

TZDBNames::TZDBNames(const char16_t** names, char** regions, int32_t numRegions)
    :   fNames(names),
        fRegions(regions),
        fNumRegions(numRegions) {
}

TZDBNames::~TZDBNames() {
    if (fNames != nullptr) {
        uprv_free(fNames);
    }
    if (fRegions != nullptr) {
        char **p = fRegions;
        for (int32_t i = 0; i < fNumRegions; p++, i++) {
            uprv_free(*p);
        }
        uprv_free(fRegions);
    }
}

TZDBNames*
TZDBNames::createInstance(UResourceBundle* rb, const char* key) {
    if (rb == nullptr || key == nullptr || *key == 0) {
        return nullptr;
    }

    UErrorCode status = U_ZERO_ERROR;

    const char16_t **names = nullptr;
    char** regions = nullptr;
    int32_t numRegions = 0;

    int32_t len = 0;

    UResourceBundle* rbTable = nullptr;
    rbTable = ures_getByKey(rb, key, rbTable, &status);
    if (U_FAILURE(status)) {
        return nullptr;
    }

    names = static_cast<const char16_t**>(uprv_malloc(sizeof(const char16_t*) * TZDBNAMES_KEYS_SIZE));
    UBool isEmpty = true;
    if (names != nullptr) {
        for (int32_t i = 0; i < TZDBNAMES_KEYS_SIZE; i++) {
            status = U_ZERO_ERROR;
            const char16_t *value = ures_getStringByKey(rbTable, TZDBNAMES_KEYS[i], &len, &status);
            if (U_FAILURE(status) || len == 0) {
                names[i] = nullptr;
            } else {
                names[i] = value;
                isEmpty = false;
            }
        }
    }

    if (isEmpty) {
        if (names != nullptr) {
            uprv_free(names);
        }
        return nullptr;
    }

    UResourceBundle *regionsRes = ures_getByKey(rbTable, "parseRegions", nullptr, &status);
    UBool regionError = false;
    if (U_SUCCESS(status)) {
        numRegions = ures_getSize(regionsRes);
        if (numRegions > 0) {
            regions = static_cast<char**>(uprv_malloc(sizeof(char*) * numRegions));
            if (regions != nullptr) {
                char **pRegion = regions;
                for (int32_t i = 0; i < numRegions; i++, pRegion++) {
                    *pRegion = nullptr;
                }
                // filling regions
                pRegion = regions;
                for (int32_t i = 0; i < numRegions; i++, pRegion++) {
                    status = U_ZERO_ERROR;
                    const char16_t *uregion = ures_getStringByIndex(regionsRes, i, &len, &status);
                    if (U_FAILURE(status)) {
                        regionError = true;
                        break;
                    }
                    *pRegion = static_cast<char*>(uprv_malloc(sizeof(char) * (len + 1)));
                    if (*pRegion == nullptr) {
                        regionError = true;
                        break;
                    }
                    u_UCharsToChars(uregion, *pRegion, len);
                    (*pRegion)[len] = 0;
                }
            }
        }
    }
    ures_close(regionsRes);
    ures_close(rbTable);

    if (regionError) {
        if (names != nullptr) {
            uprv_free(names);
        }
        if (regions != nullptr) {
            char **p = regions;
            for (int32_t i = 0; i < numRegions; p++, i++) {
                uprv_free(*p);
            }
            uprv_free(regions);
        }
        return nullptr;
    }

    return new TZDBNames(names, regions, numRegions);
}

const char16_t*
TZDBNames::getName(UTimeZoneNameType type) const {
    if (fNames == nullptr) {
        return nullptr;
    }
    const char16_t *name = nullptr;
    switch(type) {
    case UTZNM_SHORT_STANDARD:
        name = fNames[0];
        break;
    case UTZNM_SHORT_DAYLIGHT:
        name = fNames[1];
        break;
    default:
        name = nullptr;
    }
    return name;
}

const char**
TZDBNames::getParseRegions(int32_t& numRegions) const {
    if (fRegions == nullptr) {
        numRegions = 0;
    } else {
        numRegions = fNumRegions;
    }
    return (const char**)fRegions;
}

U_CDECL_BEGIN
/**
 * TZDBNameInfo stores metazone name information for the IANA abbreviations
 * in the trie
 */
typedef struct TZDBNameInfo {
    const char16_t*        mzID;
    UTimeZoneNameType   type;
    UBool               ambiguousType;
    const char**        parseRegions;
    int32_t             nRegions;
} TZDBNameInfo;
U_CDECL_END


class TZDBNameSearchHandler : public TextTrieMapSearchResultHandler {
public:
    TZDBNameSearchHandler(uint32_t types, StringPiece region);
    virtual ~TZDBNameSearchHandler();

    UBool handleMatch(int32_t matchLength, const CharacterNode *node, UErrorCode &status) override;
    TimeZoneNames::MatchInfoCollection* getMatches(int32_t& maxMatchLen);

private:
    uint32_t fTypes;
    int32_t fMaxMatchLen;
    TimeZoneNames::MatchInfoCollection* fResults;
    StringPiece fRegion;
};

TZDBNameSearchHandler::TZDBNameSearchHandler(uint32_t types, StringPiece region)
: fTypes(types), fMaxMatchLen(0), fResults(nullptr), fRegion(region) {
}

TZDBNameSearchHandler::~TZDBNameSearchHandler() {
    delete fResults;
}

UBool
TZDBNameSearchHandler::handleMatch(int32_t matchLength, const CharacterNode *node, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return false;
    }

    TZDBNameInfo *match = nullptr;
    TZDBNameInfo *defaultRegionMatch = nullptr;

    if (node->hasValues()) {
        int32_t valuesCount = node->countValues();
        for (int32_t i = 0; i < valuesCount; i++) {
            TZDBNameInfo *ninfo = (TZDBNameInfo *)node->getValue(i);
            if (ninfo == nullptr) {
                continue;
            }
            if ((ninfo->type & fTypes) != 0) {
                // Some tz database abbreviations are ambiguous. For example,
                // CST means either Central Standard Time or China Standard Time.
                // Unlike CLDR time zone display names, this implementation
                // does not use unique names. And TimeZoneFormat does not expect
                // multiple results returned for the same time zone type.
                // For this reason, this implementation resolve one among same
                // zone type with a same name at this level.
                if (ninfo->parseRegions == nullptr) {
                    // parseRegions == null means this is the default metazone
                    // mapping for the abbreviation.
                    if (defaultRegionMatch == nullptr) {
                        match = defaultRegionMatch = ninfo;
                    }
                } else {
                    UBool matchRegion = false;
                    // non-default metazone mapping for an abbreviation
                    // comes with applicable regions. For example, the default
                    // metazone mapping for "CST" is America_Central,
                    // but if region is one of CN/MO/TW, "CST" is parsed
                    // as metazone China (China Standard Time).
                    for (int32_t j = 0; j < ninfo->nRegions; j++) {
                        const char *region = ninfo->parseRegions[j];
                        if (fRegion == region) {
                            match = ninfo;
                            matchRegion = true;
                            break;
                        }
                    }
                    if (matchRegion) {
                        break;
                    }
                    if (match == nullptr) {
                        match = ninfo;
                    }
                }
            }
        }

        if (match != nullptr) {
            UTimeZoneNameType ntype = match->type;
            // Note: Workaround for duplicated standard/daylight names
            // The tz database contains a few zones sharing a
            // same name for both standard time and daylight saving
            // time. For example, Australia/Sydney observes DST,
            // but "EST" is used for both standard and daylight.
            // When both SHORT_STANDARD and SHORT_DAYLIGHT are included
            // in the find operation, we cannot tell which one was
            // actually matched.
            // TimeZoneFormat#parse returns a matched name type (standard
            // or daylight) and DateFormat implementation uses the info to
            // to adjust actual time. To avoid false type information,
            // this implementation replaces the name type with SHORT_GENERIC.
            if (match->ambiguousType
                    && (ntype == UTZNM_SHORT_STANDARD || ntype == UTZNM_SHORT_DAYLIGHT)
                    && (fTypes & UTZNM_SHORT_STANDARD) != 0
                    && (fTypes & UTZNM_SHORT_DAYLIGHT) != 0) {
                ntype = UTZNM_SHORT_GENERIC;
            }

            if (fResults == nullptr) {
                fResults = new TimeZoneNames::MatchInfoCollection();
                if (fResults == nullptr) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                }
            }
            if (U_SUCCESS(status)) {
                U_ASSERT(fResults != nullptr);
                U_ASSERT(match->mzID != nullptr);
                fResults->addMetaZone(ntype, matchLength, UnicodeString(match->mzID, -1), status);
                if (U_SUCCESS(status) && matchLength > fMaxMatchLen) {
                    fMaxMatchLen = matchLength;
                }
            }
        }
    }
    return true;
}

TimeZoneNames::MatchInfoCollection*
TZDBNameSearchHandler::getMatches(int32_t& maxMatchLen) {
    // give the ownership to the caller
    TimeZoneNames::MatchInfoCollection* results = fResults;
    maxMatchLen = fMaxMatchLen;

    // reset
    fResults = nullptr;
    fMaxMatchLen = 0;
    return results;
}

U_CDECL_BEGIN
/**
 * Deleter for TZDBNames
 */
static void U_CALLCONV
deleteTZDBNames(void *obj) {
    if (obj != EMPTY) {
        delete (TZDBNames *)obj;
    }
}

static void U_CALLCONV initTZDBNamesMap(UErrorCode &status) {
    gTZDBNamesMap = uhash_open(uhash_hashUChars, uhash_compareUChars, nullptr, &status);
    if (U_FAILURE(status)) {
        gTZDBNamesMap = nullptr;
        return;
    }
    // no key deleters for tzdb name maps
    uhash_setValueDeleter(gTZDBNamesMap, deleteTZDBNames);
    ucln_i18n_registerCleanup(UCLN_I18N_TZDBTIMEZONENAMES, tzdbTimeZoneNames_cleanup);
}

/**
 * Deleter for TZDBNameInfo
 */
static void U_CALLCONV
deleteTZDBNameInfo(void *obj) {
    if (obj != nullptr) {
        uprv_free(obj);
    }
}

static void U_CALLCONV prepareFind(UErrorCode &status) {
    if (U_FAILURE(status)) {
        return;
    }
    gTZDBNamesTrie = new TextTrieMap(true, deleteTZDBNameInfo);
    if (gTZDBNamesTrie == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }

    const UnicodeString *mzID;
    StringEnumeration *mzIDs = TimeZoneNamesImpl::_getAvailableMetaZoneIDs(status);
    if (U_SUCCESS(status)) {
        while ((mzID = mzIDs->snext(status)) != nullptr && U_SUCCESS(status)) {
            const TZDBNames *names = TZDBTimeZoneNames::getMetaZoneNames(*mzID, status);
            if (U_FAILURE(status)) {
                break;
            }
            if (names == nullptr) {
                continue;
            }
            const char16_t *std = names->getName(UTZNM_SHORT_STANDARD);
            const char16_t *dst = names->getName(UTZNM_SHORT_DAYLIGHT);
            if (std == nullptr && dst == nullptr) {
                continue;
            }
            int32_t numRegions = 0;
            const char **parseRegions = names->getParseRegions(numRegions);

            // The tz database contains a few zones sharing a
            // same name for both standard time and daylight saving
            // time. For example, Australia/Sydney observes DST,
            // but "EST" is used for both standard and daylight.
            // we need to store the information for later processing.
            UBool ambiguousType = (std != nullptr && dst != nullptr && u_strcmp(std, dst) == 0);

            const char16_t *uMzID = ZoneMeta::findMetaZoneID(*mzID);
            if (std != nullptr) {
                TZDBNameInfo *stdInf = (TZDBNameInfo *)uprv_malloc(sizeof(TZDBNameInfo));
                if (stdInf == nullptr) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                    break;
                }
                stdInf->mzID = uMzID;
                stdInf->type = UTZNM_SHORT_STANDARD;
                stdInf->ambiguousType = ambiguousType;
                stdInf->parseRegions = parseRegions;
                stdInf->nRegions = numRegions;
                gTZDBNamesTrie->put(std, stdInf, status);
            }
            if (U_SUCCESS(status) && dst != nullptr) {
                TZDBNameInfo *dstInf = (TZDBNameInfo *)uprv_malloc(sizeof(TZDBNameInfo));
                if (dstInf == nullptr) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                    break;
                }
                dstInf->mzID = uMzID;
                dstInf->type = UTZNM_SHORT_DAYLIGHT;
                dstInf->ambiguousType = ambiguousType;
                dstInf->parseRegions = parseRegions;
                dstInf->nRegions = numRegions;
                gTZDBNamesTrie->put(dst, dstInf, status);
            }
        }
    }
    delete mzIDs;

    if (U_FAILURE(status)) {
        delete gTZDBNamesTrie;
        gTZDBNamesTrie = nullptr;
        return;
    }

    ucln_i18n_registerCleanup(UCLN_I18N_TZDBTIMEZONENAMES, tzdbTimeZoneNames_cleanup);
}

U_CDECL_END

TZDBTimeZoneNames::TZDBTimeZoneNames(const Locale& locale)
: fLocale(locale), fRegion() {
    UBool useWorld = true;
    const char* region = fLocale.getCountry();
    int32_t regionLen = static_cast<int32_t>(uprv_strlen(region));
    if (regionLen == 0) {
        UErrorCode status = U_ZERO_ERROR;
        CharString loc = ulocimp_addLikelySubtags(fLocale.getName(), status);
        ulocimp_getSubtags(loc.data(), nullptr, nullptr, &fRegion, nullptr, nullptr, status);
        if (U_SUCCESS(status)) {
            useWorld = false;
        }
    } else {
        UErrorCode status = U_ZERO_ERROR;
        fRegion.append(region, regionLen, status);
        U_ASSERT(U_SUCCESS(status));
        useWorld = false;
    }
    if (useWorld) {
        UErrorCode status = U_ZERO_ERROR;
        fRegion.append("001", status);
        U_ASSERT(U_SUCCESS(status));
    }
}

TZDBTimeZoneNames::~TZDBTimeZoneNames() {
}

bool
TZDBTimeZoneNames::operator==(const TimeZoneNames& other) const {
    if (this == &other) {
        return true;
    }
    // No implementation for now
    return false;
}

TZDBTimeZoneNames*
TZDBTimeZoneNames::clone() const {
    return new TZDBTimeZoneNames(fLocale);
}

StringEnumeration*
TZDBTimeZoneNames::getAvailableMetaZoneIDs(UErrorCode& status) const {
    return TimeZoneNamesImpl::_getAvailableMetaZoneIDs(status);
}

StringEnumeration*
TZDBTimeZoneNames::getAvailableMetaZoneIDs(const UnicodeString& tzID, UErrorCode& status) const {
    return TimeZoneNamesImpl::_getAvailableMetaZoneIDs(tzID, status);
}

UnicodeString&
TZDBTimeZoneNames::getMetaZoneID(const UnicodeString& tzID, UDate date, UnicodeString& mzID) const {
    return TimeZoneNamesImpl::_getMetaZoneID(tzID, date, mzID);
}

UnicodeString&
TZDBTimeZoneNames::getReferenceZoneID(const UnicodeString& mzID, const char* region, UnicodeString& tzID) const {
    return TimeZoneNamesImpl::_getReferenceZoneID(mzID, region, tzID);
}

UnicodeString&
TZDBTimeZoneNames::getMetaZoneDisplayName(const UnicodeString& mzID,
                                          UTimeZoneNameType type,
                                          UnicodeString& name) const {
    name.setToBogus();
    if (mzID.isEmpty()) {
        return name;
    }

    UErrorCode status = U_ZERO_ERROR;
    const TZDBNames *tzdbNames = TZDBTimeZoneNames::getMetaZoneNames(mzID, status);
    if (U_SUCCESS(status)) {
        if (tzdbNames != nullptr) {
            const char16_t *s = tzdbNames->getName(type);
            if (s != nullptr) {
                name.setTo(true, s, -1);
            }
        }
    }

    return name;
}

UnicodeString&
TZDBTimeZoneNames::getTimeZoneDisplayName(const UnicodeString& /* tzID */, UTimeZoneNameType /* type */, UnicodeString& name) const {
    // No abbreviations associated a zone directly for now.
    name.setToBogus();
    return name;
}

TZDBTimeZoneNames::MatchInfoCollection*
TZDBTimeZoneNames::find(const UnicodeString& text, int32_t start, uint32_t types, UErrorCode& status) const {
    umtx_initOnce(gTZDBNamesTrieInitOnce, &prepareFind, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }

    TZDBNameSearchHandler handler(types, fRegion.toStringPiece());
    gTZDBNamesTrie->search(text, start, (TextTrieMapSearchResultHandler *)&handler, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    int32_t maxLen = 0;
    return handler.getMatches(maxLen);
}

const TZDBNames*
TZDBTimeZoneNames::getMetaZoneNames(const UnicodeString& mzID, UErrorCode& status) {
    umtx_initOnce(gTZDBNamesMapInitOnce, &initTZDBNamesMap, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }

    TZDBNames* tzdbNames = nullptr;

    char16_t mzIDKey[ZID_KEY_MAX + 1];
    mzID.extract(mzIDKey, ZID_KEY_MAX, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    mzIDKey[mzID.length()] = 0;
    if (!uprv_isInvariantUString(mzIDKey, mzID.length())) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }

    static UMutex gTZDBNamesMapLock;
    umtx_lock(&gTZDBNamesMapLock);
    {
        void *cacheVal = uhash_get(gTZDBNamesMap, mzIDKey);
        if (cacheVal == nullptr) {
            UResourceBundle *zoneStringsRes = ures_openDirect(U_ICUDATA_ZONE, "tzdbNames", &status);
            zoneStringsRes = ures_getByKey(zoneStringsRes, gZoneStrings, zoneStringsRes, &status);
            char key[ZID_KEY_MAX + 1];
            mergeTimeZoneKey(mzID, key, sizeof(key), status);
            if (U_SUCCESS(status)) {
                tzdbNames = TZDBNames::createInstance(zoneStringsRes, key);

                if (tzdbNames == nullptr) {
                    cacheVal = (void *)EMPTY;
                } else {
                    cacheVal = tzdbNames;
                }
                // Use the persistent ID as the resource key, so we can
                // avoid duplications.
                // TODO: Is there a more efficient way, like intern() in Java?
                void* newKey = (void*) ZoneMeta::findMetaZoneID(mzID);
                if (newKey != nullptr) {
                    uhash_put(gTZDBNamesMap, newKey, cacheVal, &status);
                    if (U_FAILURE(status)) {
                        if (tzdbNames != nullptr) {
                            delete tzdbNames;
                            tzdbNames = nullptr;
                        }
                    }
                } else {
                    // Should never happen with a valid input
                    if (tzdbNames != nullptr) {
                        // It's not possible that we get a valid tzdbNames with unknown ID.
                        // But just in case..
                        delete tzdbNames;
                        tzdbNames = nullptr;
                    }
                }
            }
            ures_close(zoneStringsRes);
        } else if (cacheVal != EMPTY) {
            tzdbNames = static_cast<TZDBNames*>(cacheVal);
        }
    }
    umtx_unlock(&gTZDBNamesMapLock);

    return tzdbNames;
}

U_NAMESPACE_END


#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
