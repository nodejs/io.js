// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (c) 2001-2014, International Business Machines
*   Corporation and others.  All Rights Reserved.
**********************************************************************
*   Date        Name        Description
*   08/10/2001  aliu        Creation.
**********************************************************************
*/

#include "unicode/utypes.h"
#include "unicode/rep.h"

#if !UCONFIG_NO_TRANSLITERATION

#include "unicode/translit.h"
#include "unicode/resbund.h"
#include "unicode/uniset.h"
#include "unicode/uscript.h"
#include "rbt.h"
#include "cpdtrans.h"
#include "nultrans.h"
#include "transreg.h"
#include "rbt_data.h"
#include "rbt_pars.h"
#include "tridpars.h"
#include "charstr.h"
#include "uassert.h"
#include "locutil.h"

// Enable the following symbol to add debugging code that tracks the
// allocation, deletion, and use of Entry objects.  BoundsChecker has
// reported dangling pointer errors with these objects, but I have
// been unable to confirm them.  I suspect BoundsChecker is getting
// confused with pointers going into and coming out of a UHashtable,
// despite the hinting code that is designed to help it.
// #define DEBUG_MEM
#ifdef DEBUG_MEM
#include <stdio.h>
#endif

// char16_t constants
static const char16_t LOCALE_SEP  = 95; // '_'
//static const char16_t ID_SEP      = 0x002D; /*-*/
//static const char16_t VARIANT_SEP = 0x002F; // '/'

// String constants
static const char16_t ANY[] = { 0x41, 0x6E, 0x79, 0 }; // Any
static const char16_t LAT[] = { 0x4C, 0x61, 0x74, 0 }; // Lat

// empty string
#define NO_VARIANT UnicodeString()

// initial estimate for specDAG size
// ICU 60 Transliterator::countAvailableSources()
#define SPECDAG_INIT_SIZE 149

// initial estimate for number of variant names
#define VARIANT_LIST_INIT_SIZE 11
#define VARIANT_LIST_MAX_SIZE 31

// initial estimate for availableIDs count (default estimate is 8 => multiple reallocs)
// ICU 60 Transliterator::countAvailableIDs()
#define AVAILABLE_IDS_INIT_SIZE 641

// initial estimate for number of targets for source "Any", "Lat"
// ICU 60 Transliterator::countAvailableTargets("Any")/("Latn")
#define ANY_TARGETS_INIT_SIZE 125
#define LAT_TARGETS_INIT_SIZE 23

/**
 * Resource bundle key for the RuleBasedTransliterator rule.
 */
//static const char RB_RULE[] = "Rule";

U_NAMESPACE_BEGIN

//------------------------------------------------------------------
// Alias
//------------------------------------------------------------------

TransliteratorAlias::TransliteratorAlias(const UnicodeString& theAliasID,
                                         const UnicodeSet* cpdFilter) :
    ID(),
    aliasesOrRules(theAliasID),
    transes(nullptr),
    compoundFilter(cpdFilter),
    direction(UTRANS_FORWARD),
    type(TransliteratorAlias::SIMPLE) {
}

TransliteratorAlias::TransliteratorAlias(const UnicodeString& theID,
                                         const UnicodeString& idBlocks,
                                         UVector* adoptedTransliterators,
                                         const UnicodeSet* cpdFilter) :
    ID(theID),
    aliasesOrRules(idBlocks),
    transes(adoptedTransliterators),
    compoundFilter(cpdFilter),
    direction(UTRANS_FORWARD),
    type(TransliteratorAlias::COMPOUND) {
}

TransliteratorAlias::TransliteratorAlias(const UnicodeString& theID,
                                         const UnicodeString& rules,
                                         UTransDirection dir) :
    ID(theID),
    aliasesOrRules(rules),
    transes(nullptr),
    compoundFilter(nullptr),
    direction(dir),
    type(TransliteratorAlias::RULES) {
}

TransliteratorAlias::~TransliteratorAlias() {
    delete transes;
}


Transliterator* TransliteratorAlias::create(UParseError& pe,
                                            UErrorCode& ec) {
    if (U_FAILURE(ec)) {
        return nullptr;
    }
    Transliterator *t = nullptr;
    switch (type) {
    case SIMPLE:
        t = Transliterator::createInstance(aliasesOrRules, UTRANS_FORWARD, pe, ec);
        if(U_FAILURE(ec)){
            return nullptr;
        }
        if (compoundFilter != nullptr)
            t->adoptFilter(compoundFilter->clone());
        break;
    case COMPOUND:
        {
            // the total number of transliterators in the compound is the total number of anonymous transliterators
            // plus the total number of ID blocks-- we start by assuming the list begins and ends with an ID
            // block and that each pair anonymous transliterators has an ID block between them.  Then we go back
            // to see whether there really are ID blocks at the beginning and end (by looking for U+FFFF, which
            // marks the position where an anonymous transliterator goes) and adjust accordingly
            int32_t anonymousRBTs = transes->size();
            UnicodeString noIDBlock((char16_t)(0xffff));
            noIDBlock += ((char16_t)(0xffff));
            int32_t pos = aliasesOrRules.indexOf(noIDBlock);
            while (pos >= 0) {
                pos = aliasesOrRules.indexOf(noIDBlock, pos + 1);
            }

            UVector transliterators(uprv_deleteUObject, nullptr, ec);
            UnicodeString idBlock;
            int32_t blockSeparatorPos = aliasesOrRules.indexOf((char16_t)(0xffff));
            while (blockSeparatorPos >= 0) {
                aliasesOrRules.extract(0, blockSeparatorPos, idBlock);
                aliasesOrRules.remove(0, blockSeparatorPos + 1);
                if (!idBlock.isEmpty())
                    transliterators.adoptElement(Transliterator::createInstance(idBlock, UTRANS_FORWARD, pe, ec), ec);
                if (!transes->isEmpty())
                    transliterators.adoptElement(transes->orphanElementAt(0), ec);
                blockSeparatorPos = aliasesOrRules.indexOf((char16_t)(0xffff));
            }
            if (!aliasesOrRules.isEmpty())
                transliterators.adoptElement(Transliterator::createInstance(aliasesOrRules, UTRANS_FORWARD, pe, ec), ec);
            while (!transes->isEmpty())
                transliterators.adoptElement(transes->orphanElementAt(0), ec);
            transliterators.setDeleter(nullptr);

            if (U_SUCCESS(ec)) {
                t = new CompoundTransliterator(ID, transliterators,
                        (compoundFilter ? compoundFilter->clone() : nullptr),
                        anonymousRBTs, pe, ec);
                if (t == nullptr) {
                    ec = U_MEMORY_ALLOCATION_ERROR;
                    return nullptr;
                }
            } else {
                for (int32_t i = 0; i < transliterators.size(); i++)
                    delete (Transliterator*)(transliterators.elementAt(i));
            }
        }
        break;
    case RULES:
        UPRV_UNREACHABLE_EXIT; // don't call create() if isRuleBased() returns true!
    }
    return t;
}

UBool TransliteratorAlias::isRuleBased() const {
    return type == RULES;
}

void TransliteratorAlias::parse(TransliteratorParser& parser,
                                UParseError& pe, UErrorCode& ec) const {
    U_ASSERT(type == RULES);
    if (U_FAILURE(ec)) {
        return;
    }

    parser.parse(aliasesOrRules, direction, pe, ec);
}

//----------------------------------------------------------------------
// class TransliteratorSpec
//----------------------------------------------------------------------

/**
 * A TransliteratorSpec is a string specifying either a source or a target.  In more
 * general terms, it may also specify a variant, but we only use the
 * Spec class for sources and targets.
 *
 * A Spec may be a locale or a script.  If it is a locale, it has a
 * fallback chain that goes xx_YY_ZZZ -> xx_YY -> xx -> ssss, where
 * ssss is the script mapping of xx_YY_ZZZ.  The Spec API methods
 * hasFallback(), next(), and reset() iterate over this fallback
 * sequence.
 *
 * The Spec class canonicalizes itself, so the locale is put into
 * canonical form, or the script is transformed from an abbreviation
 * to a full name.
 */
class TransliteratorSpec : public UMemory {
 public:
    TransliteratorSpec(const UnicodeString& spec);
    ~TransliteratorSpec();

    const UnicodeString& get() const;
    UBool hasFallback() const;
    const UnicodeString& next();
    void reset();

    UBool isLocale() const;
    ResourceBundle& getBundle() const;

    operator const UnicodeString&() const { return get(); }
    const UnicodeString& getTop() const { return top; }

 private:
    void setupNext();

    UnicodeString top;
    UnicodeString spec;
    UnicodeString nextSpec;
    UnicodeString scriptName;
    UBool isSpecLocale; // true if spec is a locale
    UBool isNextLocale; // true if nextSpec is a locale
    ResourceBundle* res;

    TransliteratorSpec(const TransliteratorSpec &other); // forbid copying of this class
    TransliteratorSpec &operator=(const TransliteratorSpec &other); // forbid copying of this class
};

TransliteratorSpec::TransliteratorSpec(const UnicodeString& theSpec)
: top(theSpec),
  res(nullptr)
{
    UErrorCode status = U_ZERO_ERROR;
    Locale topLoc("");
    LocaleUtility::initLocaleFromName(theSpec, topLoc);
    if (!topLoc.isBogus()) {
        res = new ResourceBundle(U_ICUDATA_TRANSLIT, topLoc, status);
        /* test for nullptr */
        if (res == nullptr) {
            return;
        }
        if (U_FAILURE(status) || status == U_USING_DEFAULT_WARNING) {
            delete res;
            res = nullptr;
        }
    }

    // Canonicalize script name -or- do locale->script mapping
    status = U_ZERO_ERROR;
    static const int32_t capacity = 10;
    UScriptCode script[capacity]={USCRIPT_INVALID_CODE};
    int32_t num = uscript_getCode(CharString().appendInvariantChars(theSpec, status).data(),
                                  script, capacity, &status);
    if (num > 0 && script[0] != USCRIPT_INVALID_CODE) {
        scriptName = UnicodeString(uscript_getName(script[0]), -1, US_INV);
    }

    // Canonicalize top
    if (res != nullptr) {
        // Canonicalize locale name
        UnicodeString locStr;
        LocaleUtility::initNameFromLocale(topLoc, locStr);
        if (!locStr.isBogus()) {
            top = locStr;
        }
    } else if (scriptName.length() != 0) {
        // We are a script; use canonical name
        top = scriptName;
    }

    // assert(spec != top);
    reset();
}

TransliteratorSpec::~TransliteratorSpec() {
    delete res;
}

UBool TransliteratorSpec::hasFallback() const {
    return nextSpec.length() != 0;
}

void TransliteratorSpec::reset() {
    if (spec != top) {
        spec = top;
        isSpecLocale = (res != nullptr);
        setupNext();
    }
}

void TransliteratorSpec::setupNext() {
    isNextLocale = false;
    if (isSpecLocale) {
        nextSpec = spec;
        int32_t i = nextSpec.lastIndexOf(LOCALE_SEP);
        // If i == 0 then we have _FOO, so we fall through
        // to the scriptName.
        if (i > 0) {
            nextSpec.truncate(i);
            isNextLocale = true;
        } else {
            nextSpec = scriptName; // scriptName may be empty
        }
    } else {
        // spec is a script, so we are at the end
        nextSpec.truncate(0);
    }
}

// Protocol:
// for(const UnicodeString& s(spec.get());
//     spec.hasFallback(); s(spec.next())) { ...

const UnicodeString& TransliteratorSpec::next() {
    spec = nextSpec;
    isSpecLocale = isNextLocale;
    setupNext();
    return spec;
}

const UnicodeString& TransliteratorSpec::get() const {
    return spec;
}

UBool TransliteratorSpec::isLocale() const {
    return isSpecLocale;
}

ResourceBundle& TransliteratorSpec::getBundle() const {
    return *res;
}

//----------------------------------------------------------------------

#ifdef DEBUG_MEM

// Vector of Entry pointers currently in use
static UVector* DEBUG_entries = nullptr;

static void DEBUG_setup() {
    if (DEBUG_entries == nullptr) {
        UErrorCode ec = U_ZERO_ERROR;
        DEBUG_entries = new UVector(ec);
    }
}

// Caller must call DEBUG_setup first.  Return index of given Entry,
// if it is in use (not deleted yet), or -1 if not found.
static int DEBUG_findEntry(TransliteratorEntry* e) {
    for (int i=0; i<DEBUG_entries->size(); ++i) {
        if (e == (TransliteratorEntry*) DEBUG_entries->elementAt(i)) {
            return i;
        }
    }
    return -1;
}

// Track object creation
static void DEBUG_newEntry(TransliteratorEntry* e) {
    DEBUG_setup();
    if (DEBUG_findEntry(e) >= 0) {
        // This should really never happen unless the heap is broken
        printf("ERROR DEBUG_newEntry duplicate new pointer %08X\n", e);
        return;
    }
    UErrorCode ec = U_ZERO_ERROR;
    DEBUG_entries->addElement(e, ec);
}

// Track object deletion
static void DEBUG_delEntry(TransliteratorEntry* e) {
    DEBUG_setup();
    int i = DEBUG_findEntry(e);
    if (i < 0) {
        printf("ERROR DEBUG_delEntry possible double deletion %08X\n", e);
        return;
    }
    DEBUG_entries->removeElementAt(i);
}

// Track object usage
static void DEBUG_useEntry(TransliteratorEntry* e) {
    if (e == nullptr) return;
    DEBUG_setup();
    int i = DEBUG_findEntry(e);
    if (i < 0) {
        printf("ERROR DEBUG_useEntry possible dangling pointer %08X\n", e);
    }
}

#else
// If we're not debugging then make these macros into NOPs
#define DEBUG_newEntry(x)
#define DEBUG_delEntry(x)
#define DEBUG_useEntry(x)
#endif

//----------------------------------------------------------------------
// class Entry
//----------------------------------------------------------------------

/**
 * The Entry object stores objects of different types and
 * singleton objects as placeholders for rule-based transliterators to
 * be built as needed.  Instances of this struct can be placeholders,
 * can represent prototype transliterators to be cloned, or can
 * represent TransliteratorData objects.  We don't support storing
 * classes in the registry because we don't have the rtti infrastructure
 * for it.  We could easily add this if there is a need for it in the
 * future.
 */
class TransliteratorEntry : public UMemory {
public:
    enum Type {
        RULES_FORWARD,
        RULES_REVERSE,
        LOCALE_RULES,
        PROTOTYPE,
        RBT_DATA,
        COMPOUND_RBT,
        ALIAS,
        FACTORY,
        NONE // Only used for uninitialized entries
    } entryType;
    // NOTE: stringArg cannot go inside the union because
    // it has a copy constructor
    UnicodeString stringArg; // For RULES_*, ALIAS, COMPOUND_RBT
    int32_t intArg; // For COMPOUND_RBT, LOCALE_RULES
    UnicodeSet* compoundFilter; // For COMPOUND_RBT
    union {
        Transliterator* prototype; // For PROTOTYPE
        TransliterationRuleData* data; // For RBT_DATA
        UVector* dataVector;    // For COMPOUND_RBT
        struct {
            Transliterator::Factory function;
            Transliterator::Token   context;
        } factory; // For FACTORY
    } u;
    TransliteratorEntry();
    ~TransliteratorEntry();
    void adoptPrototype(Transliterator* adopted);
    void setFactory(Transliterator::Factory factory,
                    Transliterator::Token context);

private:

    TransliteratorEntry(const TransliteratorEntry &other); // forbid copying of this class
    TransliteratorEntry &operator=(const TransliteratorEntry &other); // forbid copying of this class
};

TransliteratorEntry::TransliteratorEntry() {
    u.prototype = nullptr;
    compoundFilter = nullptr;
    entryType = NONE;
    DEBUG_newEntry(this);
}

TransliteratorEntry::~TransliteratorEntry() {
    DEBUG_delEntry(this);
    if (entryType == PROTOTYPE) {
        delete u.prototype;
    } else if (entryType == RBT_DATA) {
        // The data object is shared between instances of RBT.  The
        // entry object owns it.  It should only be deleted when the
        // transliterator component is being cleaned up.  Doing so
        // invalidates any RBTs that the user has instantiated.
        delete u.data;
    } else if (entryType == COMPOUND_RBT) {
        while (u.dataVector != nullptr && !u.dataVector->isEmpty())
            delete (TransliterationRuleData*)u.dataVector->orphanElementAt(0);
        delete u.dataVector;
    }
    delete compoundFilter;
}

void TransliteratorEntry::adoptPrototype(Transliterator* adopted) {
    if (entryType == PROTOTYPE) {
        delete u.prototype;
    }
    entryType = PROTOTYPE;
    u.prototype = adopted;
}

void TransliteratorEntry::setFactory(Transliterator::Factory factory,
                       Transliterator::Token context) {
    if (entryType == PROTOTYPE) {
        delete u.prototype;
    }
    entryType = FACTORY;
    u.factory.function = factory;
    u.factory.context = context;
}

// UObjectDeleter for Hashtable::setValueDeleter
U_CDECL_BEGIN
static void U_CALLCONV
deleteEntry(void* obj) {
    delete (TransliteratorEntry*) obj;
}
U_CDECL_END

//----------------------------------------------------------------------
// class TransliteratorRegistry: Basic public API
//----------------------------------------------------------------------

TransliteratorRegistry::TransliteratorRegistry(UErrorCode& status) :
    registry(true, status),
    specDAG(true, SPECDAG_INIT_SIZE, status),
    variantList(VARIANT_LIST_INIT_SIZE, status),
    availableIDs(true, AVAILABLE_IDS_INIT_SIZE, status)
{
    registry.setValueDeleter(deleteEntry);
    variantList.setDeleter(uprv_deleteUObject);
    variantList.setComparer(uhash_compareCaselessUnicodeString);
    UnicodeString *emptyString = new UnicodeString();
    if (emptyString != nullptr) {
        variantList.adoptElement(emptyString, status);
    }
    specDAG.setValueDeleter(uhash_deleteHashtable);
}

TransliteratorRegistry::~TransliteratorRegistry() {
    // Through the magic of C++, everything cleans itself up
}

Transliterator* TransliteratorRegistry::get(const UnicodeString& ID,
                                            TransliteratorAlias*& aliasReturn,
                                            UErrorCode& status) {
    U_ASSERT(aliasReturn == nullptr);
    TransliteratorEntry *entry = find(ID);
    return entry == nullptr ? nullptr
        : instantiateEntry(ID, entry, aliasReturn, status);
}

Transliterator* TransliteratorRegistry::reget(const UnicodeString& ID,
                                              TransliteratorParser& parser,
                                              TransliteratorAlias*& aliasReturn,
                                              UErrorCode& status) {
    U_ASSERT(aliasReturn == nullptr);
    TransliteratorEntry *entry = find(ID);

    if (entry == nullptr) {
        // We get to this point if there are two threads, one of which
        // is instantiating an ID, and another of which is removing
        // the same ID from the registry, and the timing is just right.
        return nullptr;
    }

    // The usage model for the caller is that they will first call
    // reg->get() inside the mutex, they'll get back an alias, they call
    // alias->isRuleBased(), and if they get true, they call alias->parse()
    // outside the mutex, then reg->reget() inside the mutex again.  A real
    // mess, but it gets things working for ICU 3.0. [alan].

    // Note: It's possible that in between the caller calling
    // alias->parse() and reg->reget(), that another thread will have
    // called reg->reget(), and the entry will already have been fixed up.
    // We have to detect this so we don't stomp over existing entry
    // data members and potentially leak memory (u.data and compoundFilter).

    if (entry->entryType == TransliteratorEntry::RULES_FORWARD ||
        entry->entryType == TransliteratorEntry::RULES_REVERSE ||
        entry->entryType == TransliteratorEntry::LOCALE_RULES) {
        
        if (parser.idBlockVector.isEmpty() && parser.dataVector.isEmpty()) {
            entry->u.data = nullptr;
            entry->entryType = TransliteratorEntry::ALIAS;
            entry->stringArg = UNICODE_STRING_SIMPLE("Any-nullptr");
        }
        else if (parser.idBlockVector.isEmpty() && parser.dataVector.size() == 1) {
            entry->u.data = (TransliterationRuleData*)parser.dataVector.orphanElementAt(0);
            entry->entryType = TransliteratorEntry::RBT_DATA;
        }
        else if (parser.idBlockVector.size() == 1 && parser.dataVector.isEmpty()) {
            entry->stringArg = *(UnicodeString*)(parser.idBlockVector.elementAt(0));
            entry->compoundFilter = parser.orphanCompoundFilter();
            entry->entryType = TransliteratorEntry::ALIAS;
        }
        else {
            entry->entryType = TransliteratorEntry::COMPOUND_RBT;
            entry->compoundFilter = parser.orphanCompoundFilter();
            entry->u.dataVector = new UVector(status);
            // TODO ICU-21701: missing check for nullptr and failed status.
            //       Unclear how best to bail out.
            entry->stringArg.remove();

            int32_t limit = parser.idBlockVector.size();
            if (parser.dataVector.size() > limit)
                limit = parser.dataVector.size();

            for (int32_t i = 0; i < limit; i++) {
                if (i < parser.idBlockVector.size()) {
                    UnicodeString* idBlock = (UnicodeString*)parser.idBlockVector.elementAt(i);
                    if (!idBlock->isEmpty())
                        entry->stringArg += *idBlock;
                }
                if (!parser.dataVector.isEmpty()) {
                    TransliterationRuleData* data = (TransliterationRuleData*)parser.dataVector.orphanElementAt(0);
                    entry->u.dataVector->addElement(data, status);
                    if (U_FAILURE(status)) {
                        delete data;
                    }
                    entry->stringArg += (char16_t)0xffff;  // use U+FFFF to mark position of RBTs in ID block
                }
            }
        }
    }

    Transliterator *t =
        instantiateEntry(ID, entry, aliasReturn, status);
    return t;
}

void TransliteratorRegistry::put(Transliterator* adoptedProto,
                                 UBool visible,
                                 UErrorCode& ec)
{
    TransliteratorEntry *entry = new TransliteratorEntry();
    if (entry == nullptr) {
        ec = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    entry->adoptPrototype(adoptedProto);
    registerEntry(adoptedProto->getID(), entry, visible);
}

void TransliteratorRegistry::put(const UnicodeString& ID,
                                 Transliterator::Factory factory,
                                 Transliterator::Token context,
                                 UBool visible,
                                 UErrorCode& ec) {
    TransliteratorEntry *entry = new TransliteratorEntry();
    if (entry == nullptr) {
        ec = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    entry->setFactory(factory, context);
    registerEntry(ID, entry, visible);
}

void TransliteratorRegistry::put(const UnicodeString& ID,
                                 const UnicodeString& resourceName,
                                 UTransDirection dir,
                                 UBool readonlyResourceAlias,
                                 UBool visible,
                                 UErrorCode& ec) {
    TransliteratorEntry *entry = new TransliteratorEntry();
    if (entry == nullptr) {
        ec = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    entry->entryType = (dir == UTRANS_FORWARD) ? TransliteratorEntry::RULES_FORWARD
        : TransliteratorEntry::RULES_REVERSE;
    if (readonlyResourceAlias) {
        entry->stringArg.setTo(true, resourceName.getBuffer(), -1);
    }
    else {
        entry->stringArg = resourceName;
    }
    registerEntry(ID, entry, visible);
}

void TransliteratorRegistry::put(const UnicodeString& ID,
                                 const UnicodeString& alias,
                                 UBool readonlyAliasAlias,
                                 UBool visible,
                                 UErrorCode& /*ec*/) {
    TransliteratorEntry *entry = new TransliteratorEntry();
    // Null pointer check
    if (entry != nullptr) {
        entry->entryType = TransliteratorEntry::ALIAS;
        if (readonlyAliasAlias) {
            entry->stringArg.setTo(true, alias.getBuffer(), -1);
        }
        else {
            entry->stringArg = alias;
        }
        registerEntry(ID, entry, visible);
    }
}

void TransliteratorRegistry::remove(const UnicodeString& ID) {
    UnicodeString source, target, variant;
    UBool sawSource;
    TransliteratorIDParser::IDtoSTV(ID, source, target, variant, sawSource);
    // Only need to do this if ID.indexOf('-') < 0
    UnicodeString id;
    TransliteratorIDParser::STVtoID(source, target, variant, id);
    registry.remove(id);
    removeSTV(source, target, variant);
    availableIDs.remove(id);
}

//----------------------------------------------------------------------
// class TransliteratorRegistry: Public ID and spec management
//----------------------------------------------------------------------

/**
 * == OBSOLETE - remove in ICU 3.4 ==
 * Return the number of IDs currently registered with the system.
 * To retrieve the actual IDs, call getAvailableID(i) with
 * i from 0 to countAvailableIDs() - 1.
 */
int32_t TransliteratorRegistry::countAvailableIDs() const {
    return availableIDs.count();
}

/**
 * == OBSOLETE - remove in ICU 3.4 ==
 * Return the index-th available ID.  index must be between 0
 * and countAvailableIDs() - 1, inclusive.  If index is out of
 * range, the result of getAvailableID(0) is returned.
 */
const UnicodeString& TransliteratorRegistry::getAvailableID(int32_t index) const {
    if (index < 0 || index >= availableIDs.count()) {
        index = 0;
    }

    int32_t pos = UHASH_FIRST;
    const UHashElement *e = nullptr;
    while (index-- >= 0) {
        e = availableIDs.nextElement(pos);
        if (e == nullptr) {
            break;
        }
    }

    if (e != nullptr) {
        return *(UnicodeString*) e->key.pointer;
    }

    // If the code reaches here, the hash table was likely modified during iteration.
    // Return an statically initialized empty string due to reference return type.
    static UnicodeString empty;
    return empty;
}

StringEnumeration* TransliteratorRegistry::getAvailableIDs() const {
    return new Enumeration(*this);
}

int32_t TransliteratorRegistry::countAvailableSources() const {
    return specDAG.count();
}

UnicodeString& TransliteratorRegistry::getAvailableSource(int32_t index,
                                                          UnicodeString& result) const {
    int32_t pos = UHASH_FIRST;
    const UHashElement* e = nullptr;
    while (index-- >= 0) {
        e = specDAG.nextElement(pos);
        if (e == nullptr) {
            break;
        }
    }
    if (e == nullptr) {
        result.truncate(0);
    } else {
        result = *(UnicodeString*) e->key.pointer;
    }
    return result;
}

int32_t TransliteratorRegistry::countAvailableTargets(const UnicodeString& source) const {
    Hashtable *targets = (Hashtable*) specDAG.get(source);
    return (targets == nullptr) ? 0 : targets->count();
}

UnicodeString& TransliteratorRegistry::getAvailableTarget(int32_t index,
                                                          const UnicodeString& source,
                                                          UnicodeString& result) const {
    Hashtable *targets = (Hashtable*) specDAG.get(source);
    if (targets == nullptr) {
        result.truncate(0); // invalid source
        return result;
    }
    int32_t pos = UHASH_FIRST;
    const UHashElement* e = nullptr;
    while (index-- >= 0) {
        e = targets->nextElement(pos);
        if (e == nullptr) {
            break;
        }
    }
    if (e == nullptr) {
        result.truncate(0); // invalid index
    } else {
        result = *(UnicodeString*) e->key.pointer;
    }
    return result;
}

int32_t TransliteratorRegistry::countAvailableVariants(const UnicodeString& source,
                                                       const UnicodeString& target) const {
    Hashtable *targets = (Hashtable*) specDAG.get(source);
    if (targets == nullptr) {
        return 0;
    }
    uint32_t varMask = targets->geti(target);
    int32_t varCount = 0;
    while (varMask > 0) {
        if (varMask & 1) {
            varCount++;
        }
        varMask >>= 1;
    }
    return varCount;
}

UnicodeString& TransliteratorRegistry::getAvailableVariant(int32_t index,
                                                           const UnicodeString& source,
                                                           const UnicodeString& target,
                                                           UnicodeString& result) const {
    Hashtable *targets = (Hashtable*) specDAG.get(source);
    if (targets == nullptr) {
        result.truncate(0); // invalid source
        return result;
    }
    uint32_t varMask = targets->geti(target);
    int32_t varCount = 0;
    int32_t varListIndex = 0;
    while (varMask > 0) {
        if (varMask & 1) {
            if (varCount == index) {
                UnicodeString *v = (UnicodeString*) variantList.elementAt(varListIndex);
                if (v != nullptr) {
                    result = *v;
                    return result;
                }
                break;
            }
            varCount++;
        }
        varMask >>= 1;
        varListIndex++;
    }
    result.truncate(0); // invalid target or index
    return result;
}

//----------------------------------------------------------------------
// class TransliteratorRegistry::Enumeration
//----------------------------------------------------------------------

TransliteratorRegistry::Enumeration::Enumeration(const TransliteratorRegistry& _reg) :
    pos(UHASH_FIRST), size(_reg.availableIDs.count()), reg(_reg) {
}

TransliteratorRegistry::Enumeration::~Enumeration() {
}

int32_t TransliteratorRegistry::Enumeration::count(UErrorCode& /*status*/) const {
    return size;
}

const UnicodeString* TransliteratorRegistry::Enumeration::snext(UErrorCode& status) {
    // This is sloppy but safe -- if we get out of sync with the underlying
    // registry, we will still return legal strings, but they might not
    // correspond to the snapshot at construction time.  So there could be
    // duplicate IDs or omitted IDs if insertions or deletions occur in one
    // thread while another is iterating.  To be more rigorous, add a timestamp,
    // which is incremented with any modification, and validate this iterator
    // against the timestamp at construction time.  This probably isn't worth
    // doing as long as there is some possibility of removing this code in favor
    // of some new code based on Doug's service framework.
    if (U_FAILURE(status)) {
        return nullptr;
    }
    int32_t n = reg.availableIDs.count();
    if (n != size) {
        status = U_ENUM_OUT_OF_SYNC_ERROR;
        return nullptr;
    }

    const UHashElement* element = reg.availableIDs.nextElement(pos);
    if (element == nullptr) {
        // If the code reaches this point, it means that it's out of sync
        // or the caller keeps asking for snext().
        return nullptr;
    }

    // Copy the string! This avoids lifetime problems.
    unistr = *(const UnicodeString*) element->key.pointer;
    return &unistr;
}

void TransliteratorRegistry::Enumeration::reset(UErrorCode& /*status*/) {
    pos = UHASH_FIRST;
    size = reg.availableIDs.count();
}

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(TransliteratorRegistry::Enumeration)

//----------------------------------------------------------------------
// class TransliteratorRegistry: internal
//----------------------------------------------------------------------

/**
 * Convenience method.  Calls 6-arg registerEntry().
 */
void TransliteratorRegistry::registerEntry(const UnicodeString& source,
                                           const UnicodeString& target,
                                           const UnicodeString& variant,
                                           TransliteratorEntry* adopted,
                                           UBool visible) {
    UnicodeString ID;
    UnicodeString s(source);
    if (s.length() == 0) {
        s.setTo(true, ANY, 3);
    }
    TransliteratorIDParser::STVtoID(source, target, variant, ID);
    registerEntry(ID, s, target, variant, adopted, visible);
}

/**
 * Convenience method.  Calls 6-arg registerEntry().
 */
void TransliteratorRegistry::registerEntry(const UnicodeString& ID,
                                           TransliteratorEntry* adopted,
                                           UBool visible) {
    UnicodeString source, target, variant;
    UBool sawSource;
    TransliteratorIDParser::IDtoSTV(ID, source, target, variant, sawSource);
    // Only need to do this if ID.indexOf('-') < 0
    UnicodeString id;
    TransliteratorIDParser::STVtoID(source, target, variant, id);
    registerEntry(id, source, target, variant, adopted, visible);
}

/**
 * Register an entry object (adopted) with the given ID, source,
 * target, and variant strings.
 */
void TransliteratorRegistry::registerEntry(const UnicodeString& ID,
                                           const UnicodeString& source,
                                           const UnicodeString& target,
                                           const UnicodeString& variant,
                                           TransliteratorEntry* adopted,
                                           UBool visible) {
    UErrorCode status = U_ZERO_ERROR;
    registry.put(ID, adopted, status);
    if (visible) {
        registerSTV(source, target, variant);
        if (!availableIDs.containsKey(ID)) {
            availableIDs.puti(ID, /* unused value */ 1, status);
        }
    } else {
        removeSTV(source, target, variant);
        availableIDs.remove(ID);
    }
}

/**
 * Register a source-target/variant in the specDAG.  Variant may be
 * empty, but source and target must not be.
 */
void TransliteratorRegistry::registerSTV(const UnicodeString& source,
                                         const UnicodeString& target,
                                         const UnicodeString& variant) {
    // assert(source.length() > 0);
    // assert(target.length() > 0);
    UErrorCode status = U_ZERO_ERROR;
    Hashtable *targets = (Hashtable*) specDAG.get(source);
    if (targets == nullptr) {
        int32_t size = 3;
        if (source.compare(ANY,3) == 0) {
            size = ANY_TARGETS_INIT_SIZE;
        } else if (source.compare(LAT,3) == 0) {
            size = LAT_TARGETS_INIT_SIZE;
        }
        targets = new Hashtable(true, size, status);
        if (U_FAILURE(status) || targets == nullptr) {
            return;
        }
        specDAG.put(source, targets, status);
    }
    int32_t variantListIndex = variantList.indexOf((void*) &variant, 0);
    if (variantListIndex < 0) {
        if (variantList.size() >= VARIANT_LIST_MAX_SIZE) {
            // can't handle any more variants
            return;
        }
        UnicodeString *variantEntry = new UnicodeString(variant);
        if (variantEntry != nullptr) {
            variantList.adoptElement(variantEntry, status);
            if (U_SUCCESS(status)) {
                variantListIndex = variantList.size() - 1;
            }
        }
        if (variantListIndex < 0) {
            return;
        }
    }
    uint32_t addMask = 1 << variantListIndex;
    uint32_t varMask = targets->geti(target);
    targets->puti(target, varMask | addMask, status);
}

/**
 * Remove a source-target/variant from the specDAG.
 */
void TransliteratorRegistry::removeSTV(const UnicodeString& source,
                                       const UnicodeString& target,
                                       const UnicodeString& variant) {
    // assert(source.length() > 0);
    // assert(target.length() > 0);
    UErrorCode status = U_ZERO_ERROR;
    Hashtable *targets = (Hashtable*) specDAG.get(source);
    if (targets == nullptr) {
        return; // should never happen for valid s-t/v
    }
    uint32_t varMask = targets->geti(target);
    if (varMask == 0) {
        return; // should never happen for valid s-t/v
    }
    int32_t variantListIndex = variantList.indexOf((void*) &variant, 0);
    if (variantListIndex < 0) {
        return; // should never happen for valid s-t/v
    }
    int32_t remMask = 1 << variantListIndex;
    varMask &= (~remMask);
    if (varMask != 0) {
        targets->puti(target, varMask, status);
    } else {
        targets->remove(target); // should delete variants
        if (targets->count() == 0) {
            specDAG.remove(source); // should delete targets
        }
    }
}

/**
 * Attempt to find a source-target/variant in the dynamic registry
 * store.  Return 0 on failure.
 *
 * Caller does NOT own returned object.
 */
TransliteratorEntry* TransliteratorRegistry::findInDynamicStore(const TransliteratorSpec& src,
                                                  const TransliteratorSpec& trg,
                                                  const UnicodeString& variant) const {
    UnicodeString ID;
    TransliteratorIDParser::STVtoID(src, trg, variant, ID);
    TransliteratorEntry *e = (TransliteratorEntry*) registry.get(ID);
    DEBUG_useEntry(e);
    return e;
}

/**
 * Attempt to find a source-target/variant in the static locale
 * resource store.  Do not perform fallback.  Return 0 on failure.
 *
 * On success, create a new entry object, register it in the dynamic
 * store, and return a pointer to it, but do not make it public --
 * just because someone requested something, we do not expand the
 * available ID list (or spec DAG).
 *
 * Caller does NOT own returned object.
 */
TransliteratorEntry* TransliteratorRegistry::findInStaticStore(const TransliteratorSpec& src,
                                                 const TransliteratorSpec& trg,
                                                 const UnicodeString& variant) {
    TransliteratorEntry* entry = nullptr;
    if (src.isLocale()) {
        entry = findInBundle(src, trg, variant, UTRANS_FORWARD);
    } else if (trg.isLocale()) {
        entry = findInBundle(trg, src, variant, UTRANS_REVERSE);
    }

    // If we found an entry, store it in the Hashtable for next
    // time.
    if (entry != nullptr) {
        registerEntry(src.getTop(), trg.getTop(), variant, entry, false);
    }

    return entry;
}

// As of 2.0, resource bundle keys cannot contain '_'
static const char16_t TRANSLITERATE_TO[] = {84,114,97,110,115,108,105,116,101,114,97,116,101,84,111,0}; // "TransliterateTo"

static const char16_t TRANSLITERATE_FROM[] = {84,114,97,110,115,108,105,116,101,114,97,116,101,70,114,111,109,0}; // "TransliterateFrom"

static const char16_t TRANSLITERATE[] = {84,114,97,110,115,108,105,116,101,114,97,116,101,0}; // "Transliterate"

/**
 * Attempt to find an entry in a single resource bundle.  This is
 * a one-sided lookup.  findInStaticStore() performs up to two such
 * lookups, one for the source, and one for the target.
 *
 * Do not perform fallback.  Return 0 on failure.
 *
 * On success, create a new Entry object, populate it, and return it.
 * The caller owns the returned object.
 */
TransliteratorEntry* TransliteratorRegistry::findInBundle(const TransliteratorSpec& specToOpen,
                                            const TransliteratorSpec& specToFind,
                                            const UnicodeString& variant,
                                            UTransDirection direction)
{
    UnicodeString utag;
    UnicodeString resStr;
    int32_t pass;

    for (pass=0; pass<2; ++pass) {
        utag.truncate(0);
        // First try either TransliteratorTo_xxx or
        // TransliterateFrom_xxx, then try the bidirectional
        // Transliterate_xxx.  This precedence order is arbitrary
        // but must be consistent and documented.
        if (pass == 0) {
            utag.append(direction == UTRANS_FORWARD ?
                        TRANSLITERATE_TO : TRANSLITERATE_FROM, -1);
        } else {
            utag.append(TRANSLITERATE, -1);
        }
        UnicodeString s(specToFind.get());
        utag.append(s.toUpper(""));
        UErrorCode status = U_ZERO_ERROR;
        ResourceBundle subres(specToOpen.getBundle().get(
            CharString().appendInvariantChars(utag, status).data(), status));
        if (U_FAILURE(status) || status == U_USING_DEFAULT_WARNING) {
            continue;
        }

        s.truncate(0);
        if (specToOpen.get() != LocaleUtility::initNameFromLocale(subres.getLocale(), s)) {
            continue;
        }

        if (variant.length() != 0) {
            status = U_ZERO_ERROR;
            resStr = subres.getStringEx(
                CharString().appendInvariantChars(variant, status).data(), status);
            if (U_SUCCESS(status)) {
                // Exit loop successfully
                break;
            }
        } else {
            // Variant is empty, which means match the first variant listed.
            status = U_ZERO_ERROR;
            resStr = subres.getStringEx(1, status);
            if (U_SUCCESS(status)) {
                // Exit loop successfully
                break;
            }
        }
    }

    if (pass==2) {
        // Failed
        return nullptr;
    }

    // We have succeeded in loading a string from the locale
    // resources.  Create a new registry entry to hold it and return it.
    TransliteratorEntry *entry = new TransliteratorEntry();
    if (entry != nullptr) {
        // The direction is always forward for the
        // TransliterateTo_xxx and TransliterateFrom_xxx
        // items; those are unidirectional forward rules.
        // For the bidirectional Transliterate_xxx items,
        // the direction is the value passed in to this
        // function.
        int32_t dir = (pass == 0) ? UTRANS_FORWARD : direction;
        entry->entryType = TransliteratorEntry::LOCALE_RULES;
        entry->stringArg = resStr;
        entry->intArg = dir;
    }

    return entry;
}

/**
 * Convenience method.  Calls 3-arg find().
 */
TransliteratorEntry* TransliteratorRegistry::find(const UnicodeString& ID) {
    UnicodeString source, target, variant;
    UBool sawSource;
    TransliteratorIDParser::IDtoSTV(ID, source, target, variant, sawSource);
    return find(source, target, variant);
}

/**
 * Top-level find method.  Attempt to find a source-target/variant in
 * either the dynamic or the static (locale resource) store.  Perform
 * fallback.
 * 
 * Lookup sequence for ss_SS_SSS-tt_TT_TTT/v:
 *
 *   ss_SS_SSS-tt_TT_TTT/v -- in hashtable
 *   ss_SS_SSS-tt_TT_TTT/v -- in ss_SS_SSS (no fallback)
 * 
 *     repeat with t = tt_TT_TTT, tt_TT, tt, and tscript
 *
 *     ss_SS_SSS-t/ *
 *     ss_SS-t/ *
 *     ss-t/ *
 *     sscript-t/ *
 *
 * Here * matches the first variant listed.
 *
 * Caller does NOT own returned object.  Return 0 on failure.
 */
TransliteratorEntry* TransliteratorRegistry::find(UnicodeString& source,
                                    UnicodeString& target,
                                    UnicodeString& variant) {
    
    TransliteratorSpec src(source);
    TransliteratorSpec trg(target);
    TransliteratorEntry* entry;

    // Seek exact match in hashtable.  Temporary fix for ICU 4.6.
    // TODO: The general logic for finding a matching transliterator needs to be reviewed.
    // ICU ticket #8089
    UnicodeString ID;
    TransliteratorIDParser::STVtoID(source, target, variant, ID);
    entry = (TransliteratorEntry*) registry.get(ID);
    if (entry != nullptr) {
        // std::string ss;
        // std::cout << ID.toUTF8String(ss) << std::endl;
        return entry;
    }

    if (variant.length() != 0) {
        
        // Seek exact match in hashtable
        entry = findInDynamicStore(src, trg, variant);
        if (entry != nullptr) {
            return entry;
        }

        // Seek exact match in locale resources
        entry = findInStaticStore(src, trg, variant);
        if (entry != nullptr) {
            return entry;
        }
    }

    for (;;) {
        src.reset();
        for (;;) {
            // Seek match in hashtable
            entry = findInDynamicStore(src, trg, NO_VARIANT);
            if (entry != nullptr) {
                return entry;
            }

            // Seek match in locale resources
            entry = findInStaticStore(src, trg, NO_VARIANT);
            if (entry != nullptr) {
                return entry;
            }
            if (!src.hasFallback()) {
                break;
            }
            src.next();
        }
        if (!trg.hasFallback()) {
            break;
        }
        trg.next();
    }

    return nullptr;
}

/**
 * Given an Entry object, instantiate it.  Caller owns result.  Return
 * 0 on failure.
 *
 * Return a non-empty aliasReturn value if the ID points to an alias.
 * We cannot instantiate it ourselves because the alias may contain
 * filters or compounds, which we do not understand.  Caller should
 * make aliasReturn empty before calling.
 *
 * The entry object is assumed to reside in the dynamic store.  It may be
 * modified.
 */
Transliterator* TransliteratorRegistry::instantiateEntry(const UnicodeString& ID,
                                                         TransliteratorEntry *entry,
                                                         TransliteratorAlias* &aliasReturn,
                                                         UErrorCode& status) {
    Transliterator* t = nullptr;
    U_ASSERT(aliasReturn == 0);

    switch (entry->entryType) {
    case TransliteratorEntry::RBT_DATA:
        t = new RuleBasedTransliterator(ID, entry->u.data);
        if (t == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
        }
        return t;
    case TransliteratorEntry::PROTOTYPE:
        t = entry->u.prototype->clone();
        if (t == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
        }
        return t;
    case TransliteratorEntry::ALIAS:
        aliasReturn = new TransliteratorAlias(entry->stringArg, entry->compoundFilter);
        if (aliasReturn == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
        }
        return nullptr;
    case TransliteratorEntry::FACTORY:
        t = entry->u.factory.function(ID, entry->u.factory.context);
        if (t == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
        }
        return t;
    case TransliteratorEntry::COMPOUND_RBT:
        {
            UVector* rbts = new UVector(uprv_deleteUObject, nullptr, entry->u.dataVector->size(), status);
            // Check for null pointer
            if (rbts == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                return nullptr;
            }
            int32_t passNumber = 1;
            for (int32_t i = 0; U_SUCCESS(status) && i < entry->u.dataVector->size(); i++) {
                // TODO: Should passNumber be turned into a decimal-string representation (1 -> "1")?
                Transliterator* tl = new RuleBasedTransliterator(UnicodeString(CompoundTransliterator::PASS_STRING) + UnicodeString(passNumber++),
                    (TransliterationRuleData*)(entry->u.dataVector->elementAt(i)), false);
                if (tl == nullptr)
                    status = U_MEMORY_ALLOCATION_ERROR;
                else
                    rbts->adoptElement(tl, status);
            }
            if (U_FAILURE(status)) {
                delete rbts;
                return nullptr;
            }
            rbts->setDeleter(nullptr);
            aliasReturn = new TransliteratorAlias(ID, entry->stringArg, rbts, entry->compoundFilter);
        }
        if (aliasReturn == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
        }
        return nullptr;
    case TransliteratorEntry::LOCALE_RULES:
        aliasReturn = new TransliteratorAlias(ID, entry->stringArg,
                                              (UTransDirection) entry->intArg);
        if (aliasReturn == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
        }
        return nullptr;
    case TransliteratorEntry::RULES_FORWARD:
    case TransliteratorEntry::RULES_REVERSE:
        // Process the rule data into a TransliteratorRuleData object,
        // and possibly also into an ::id header and/or footer.  Then
        // we modify the registry with the parsed data and retry.
        {
            TransliteratorParser parser(status);
            
            // We use the file name, taken from another resource bundle
            // 2-d array at static init time, as a locale language.  We're
            // just using the locale mechanism to map through to a file
            // name; this in no way represents an actual locale.
            //CharString ch(entry->stringArg);
            //UResourceBundle *bundle = ures_openDirect(0, ch, &status);
            UnicodeString rules = entry->stringArg;
            //ures_close(bundle);
            
            //if (U_FAILURE(status)) {
                // We have a failure of some kind.  Remove the ID from the
                // registry so we don't keep trying.  NOTE: This will throw off
                // anyone who is, at the moment, trying to iterate over the
                // available IDs.  That's acceptable since we should never
                // really get here except under installation, configuration,
                // or unrecoverable run time memory failures.
            //    remove(ID);
            //} else {
                
                // If the status indicates a failure, then we don't have any
                // rules -- there is probably an installation error.  The list
                // in the root locale should correspond to all the installed
                // transliterators; if it lists something that's not
                // installed, we'll get an error from ResourceBundle.
                aliasReturn = new TransliteratorAlias(ID, rules,
                    ((entry->entryType == TransliteratorEntry::RULES_REVERSE) ?
                     UTRANS_REVERSE : UTRANS_FORWARD));
                if (aliasReturn == nullptr) {
                    status = U_MEMORY_ALLOCATION_ERROR;
                }
            //}
        }
        return nullptr;
    default:
        UPRV_UNREACHABLE_EXIT; // can't get here
    }
}
U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_TRANSLITERATION */

//eof
