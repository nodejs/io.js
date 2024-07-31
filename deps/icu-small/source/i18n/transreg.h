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
#ifndef _TRANSREG_H
#define _TRANSREG_H

#include "unicode/utypes.h"

#if !UCONFIG_NO_TRANSLITERATION

#include "unicode/uobject.h"
#include "unicode/translit.h"
#include "hash.h"
#include "uvector.h"

U_NAMESPACE_BEGIN

class TransliteratorEntry;
class TransliteratorSpec;
class UnicodeString;

//------------------------------------------------------------------
// TransliteratorAlias
//------------------------------------------------------------------

/**
 * A TransliteratorAlias object is returned by get() if the given ID
 * actually translates into something else.  The caller then invokes
 * the create() method on the alias to create the actual
 * transliterator, and deletes the alias.
 *
 * Why all the shenanigans?  To prevent circular calls between
 * the registry code and the transliterator code that deadlocks.
 */
class TransliteratorAlias : public UMemory {
 public:
    /**
     * Construct a simple alias (type == SIMPLE)
     * @param aliasID the given id.
     */
    TransliteratorAlias(const UnicodeString& aliasID, const UnicodeSet* compoundFilter);

    /**
     * Construct a compound RBT alias (type == COMPOUND)
     */
    TransliteratorAlias(const UnicodeString& ID, const UnicodeString& idBlocks,
                        UVector* adoptedTransliterators,
                        const UnicodeSet* compoundFilter);

    /**
     * Construct a rules alias (type = RULES)
     */
    TransliteratorAlias(const UnicodeString& theID,
                        const UnicodeString& rules,
                        UTransDirection dir);

    ~TransliteratorAlias();

    /**
     * The whole point of create() is that the caller must invoke
     * it when the registry mutex is NOT held, to prevent deadlock.
     * It may only be called once.
     *
     * Note: Only call create() if isRuleBased() returns false.
     *
     * This method must be called *outside* of the TransliteratorRegistry
     * mutex.
     */
    Transliterator* create(UParseError&, UErrorCode&);

    /**
     * Return true if this alias is rule-based.  If so, the caller
     * must call parse() on it, then call TransliteratorRegistry::reget().
     */
    UBool isRuleBased() const;

    /**
     * If isRuleBased() returns true, then the caller must call this
     * method, followed by TransliteratorRegistry::reget().  The latter
     * method must be called inside the TransliteratorRegistry mutex.
     *
     * Note: Only call parse() if isRuleBased() returns true.
     *
     * This method must be called *outside* of the TransliteratorRegistry
     * mutex, because it can instantiate Transliterators embedded in
     * the rules via the "&Latin-Arabic()" syntax.
     */
    void parse(TransliteratorParser& parser,
               UParseError& pe, UErrorCode& ec) const;

 private:
    // We actually come in three flavors:
    // 1. Simple alias
    //    Here aliasID is the alias string.  Everything else is
    //    null, zero, empty.
    // 2. CompoundRBT
    //    Here ID is the ID, aliasID is the idBlock, trans is the
    //    contained RBT, and idSplitPoint is the offset in aliasID
    //    where the contained RBT goes.  compoundFilter is the
    //    compound filter, and it is _not_ owned.
    // 3. Rules
    //    Here ID is the ID, aliasID is the rules string.
    //    idSplitPoint is the UTransDirection.
    UnicodeString ID;
    UnicodeString aliasesOrRules;
    UVector* transes; // owned
    const UnicodeSet* compoundFilter; // alias
    UTransDirection direction;
    enum { SIMPLE, COMPOUND, RULES } type;

    TransliteratorAlias(const TransliteratorAlias &other); // forbid copying of this class
    TransliteratorAlias &operator=(const TransliteratorAlias &other); // forbid copying of this class
};


/**
 * A registry of system transliterators.  This is the data structure
 * that implements the mapping between transliterator IDs and the data
 * or function pointers used to create the corresponding
 * transliterators.  There is one instance of the registry that is
 * created statically.
 *
 * The registry consists of a dynamic component -- a hashtable -- and
 * a static component -- locale resource bundles.  The dynamic store
 * is semantically overlaid on the static store, so the static mapping
 * can be dynamically overridden.
 *
 * This is an internal class that is only used by Transliterator.
 * Transliterator maintains one static instance of this class and
 * delegates all registry-related operations to it.
 *
 * @author Alan Liu
 */
class TransliteratorRegistry : public UMemory {

 public:

    /**
     * Constructor
     * @param status Output param set to success/failure code.
     */
    TransliteratorRegistry(UErrorCode& status);

    /**
     * Nonvirtual destructor -- this class is not subclassable.
     */
    ~TransliteratorRegistry();

    //------------------------------------------------------------------
    // Basic public API
    //------------------------------------------------------------------

    /**
     * Given a simple ID (forward direction, no inline filter, not
     * compound) attempt to instantiate it from the registry.  Return
     * 0 on failure.
     *
     * Return a non-nullptr aliasReturn value if the ID points to an alias.
     * We cannot instantiate it ourselves because the alias may contain
     * filters or compounds, which we do not understand.  Caller should
     * make aliasReturn nullptr before calling.
     * @param ID          the given ID
     * @param aliasReturn output param to receive TransliteratorAlias;
     *                    should be nullptr on entry
     * @param parseError  Struct to receive information on position
     *                    of error if an error is encountered
     * @param status      Output param set to success/failure code.
     */
    Transliterator* get(const UnicodeString& ID,
                        TransliteratorAlias*& aliasReturn,
                        UErrorCode& status);

    /**
     * The caller must call this after calling get(), if [a] calling get()
     * returns an alias, and [b] the alias is rule based.  In that
     * situation the caller must call alias->parse() to do the parsing
     * OUTSIDE THE REGISTRY MUTEX, then call this method to retry
     * instantiating the transliterator.
     *
     * Note: Another alias might be returned by this method.
     *
     * This method (like all public methods of this class) must be called
     * from within the TransliteratorRegistry mutex.
     *
     * @param aliasReturn output param to receive TransliteratorAlias;
     *                    should be nullptr on entry
     */
    Transliterator* reget(const UnicodeString& ID,
                          TransliteratorParser& parser,
                          TransliteratorAlias*& aliasReturn,
                          UErrorCode& status);

    /**
     * Register a prototype (adopted).  This adds an entry to the
     * dynamic store, or replaces an existing entry.  Any entry in the
     * underlying static locale resource store is masked.
     */
    void put(Transliterator* adoptedProto,
             UBool visible,
             UErrorCode& ec);

    /**
     * Register an ID and a factory function pointer.  This adds an
     * entry to the dynamic store, or replaces an existing entry.  Any
     * entry in the underlying static locale resource store is masked.
     */
    void put(const UnicodeString& ID,
             Transliterator::Factory factory,
             Transliterator::Token context,
             UBool visible,
             UErrorCode& ec);

    /**
     * Register an ID and a resource name.  This adds an entry to the
     * dynamic store, or replaces an existing entry.  Any entry in the
     * underlying static locale resource store is masked.
     */
    void put(const UnicodeString& ID,
             const UnicodeString& resourceName,
             UTransDirection dir,
             UBool readonlyResourceAlias,
             UBool visible,
             UErrorCode& ec);

    /**
     * Register an ID and an alias ID.  This adds an entry to the
     * dynamic store, or replaces an existing entry.  Any entry in the
     * underlying static locale resource store is masked.
     */
    void put(const UnicodeString& ID,
             const UnicodeString& alias,
             UBool readonlyAliasAlias,
             UBool visible,
             UErrorCode& ec);

    /**
     * Unregister an ID.  This removes an entry from the dynamic store
     * if there is one.  The static locale resource store is
     * unaffected.
     * @param ID    the given ID.
     */
    void remove(const UnicodeString& ID);

    //------------------------------------------------------------------
    // Public ID and spec management
    //------------------------------------------------------------------

    /**
     * Return a StringEnumeration over the IDs currently registered
     * with the system.
     * @internal
     */
    StringEnumeration* getAvailableIDs() const;

    /**
     * == OBSOLETE - remove in ICU 3.4 ==
     * Return the number of IDs currently registered with the system.
     * To retrieve the actual IDs, call getAvailableID(i) with
     * i from 0 to countAvailableIDs() - 1.
     * @return the number of IDs currently registered with the system.
     * @internal
     */
    int32_t countAvailableIDs() const;

    /**
     * == OBSOLETE - remove in ICU 3.4 ==
     * Return the index-th available ID.  index must be between 0
     * and countAvailableIDs() - 1, inclusive.  If index is out of
     * range, the result of getAvailableID(0) is returned.
     * @param index the given index.
     * @return the index-th available ID.  index must be between 0
     *         and countAvailableIDs() - 1, inclusive.  If index is out of
     *         range, the result of getAvailableID(0) is returned.
     * @internal
     */
    const UnicodeString& getAvailableID(int32_t index) const;

    /**
     * Return the number of registered source specifiers.
     * @return the number of registered source specifiers.
     */
    int32_t countAvailableSources() const;

    /**
     * Return a registered source specifier.
     * @param index which specifier to return, from 0 to n-1, where
     * n = countAvailableSources()
     * @param result fill-in parameter to receive the source specifier.
     * If index is out of range, result will be empty.
     * @return reference to result
     */
    UnicodeString& getAvailableSource(int32_t index,
                                      UnicodeString& result) const;

    /**
     * Return the number of registered target specifiers for a given
     * source specifier.
     * @param source the given source specifier.
     * @return the number of registered target specifiers for a given
     *         source specifier.
     */
    int32_t countAvailableTargets(const UnicodeString& source) const;

    /**
     * Return a registered target specifier for a given source.
     * @param index which specifier to return, from 0 to n-1, where
     * n = countAvailableTargets(source)
     * @param source the source specifier
     * @param result fill-in parameter to receive the target specifier.
     * If source is invalid or if index is out of range, result will
     * be empty.
     * @return reference to result
     */
    UnicodeString& getAvailableTarget(int32_t index,
                                      const UnicodeString& source,
                                      UnicodeString& result) const;

    /**
     * Return the number of registered variant specifiers for a given
     * source-target pair.  There is always at least one variant: If
     * just source-target is registered, then the single variant
     * NO_VARIANT is returned.  If source-target/variant is registered
     * then that variant is returned.
     * @param source the source specifiers
     * @param target the target specifiers
     * @return the number of registered variant specifiers for a given
     *         source-target pair.
     */
    int32_t countAvailableVariants(const UnicodeString& source,
                                   const UnicodeString& target) const;

    /**
     * Return a registered variant specifier for a given source-target
     * pair.  If NO_VARIANT is one of the variants, then it will be
     * at index 0.
     * @param index which specifier to return, from 0 to n-1, where
     * n = countAvailableVariants(source, target)
     * @param source the source specifier
     * @param target the target specifier
     * @param result fill-in parameter to receive the variant
     * specifier.  If source is invalid or if target is invalid or if
     * index is out of range, result will be empty.
     * @return reference to result
     */
    UnicodeString& getAvailableVariant(int32_t index,
                                       const UnicodeString& source,
                                       const UnicodeString& target,
                                       UnicodeString& result) const;

 private:

    //----------------------------------------------------------------
    // Private implementation
    //----------------------------------------------------------------

    TransliteratorEntry* find(const UnicodeString& ID);

    TransliteratorEntry* find(UnicodeString& source,
                UnicodeString& target,
                UnicodeString& variant);

    TransliteratorEntry* findInDynamicStore(const TransliteratorSpec& src,
                              const TransliteratorSpec& trg,
                              const UnicodeString& variant) const;

    TransliteratorEntry* findInStaticStore(const TransliteratorSpec& src,
                             const TransliteratorSpec& trg,
                             const UnicodeString& variant);

    static TransliteratorEntry* findInBundle(const TransliteratorSpec& specToOpen,
                               const TransliteratorSpec& specToFind,
                               const UnicodeString& variant,
                               UTransDirection direction);

    void registerEntry(const UnicodeString& source,
                       const UnicodeString& target,
                       const UnicodeString& variant,
                       TransliteratorEntry* adopted,
                       UBool visible);

    void registerEntry(const UnicodeString& ID,
                       TransliteratorEntry* adopted,
                       UBool visible);

    void registerEntry(const UnicodeString& ID,
                       const UnicodeString& source,
                       const UnicodeString& target,
                       const UnicodeString& variant,
                       TransliteratorEntry* adopted,
                       UBool visible);

    void registerSTV(const UnicodeString& source,
                     const UnicodeString& target,
                     const UnicodeString& variant);

    void removeSTV(const UnicodeString& source,
                   const UnicodeString& target,
                   const UnicodeString& variant);

    Transliterator* instantiateEntry(const UnicodeString& ID,
                                     TransliteratorEntry *entry,
                                     TransliteratorAlias*& aliasReturn,
                                     UErrorCode& status);

    /**
     * A StringEnumeration over the registered IDs in this object.
     */
    class Enumeration : public StringEnumeration {
    public:
        Enumeration(const TransliteratorRegistry& reg);
        virtual ~Enumeration();
        virtual int32_t count(UErrorCode& status) const override;
        virtual const UnicodeString* snext(UErrorCode& status) override;
        virtual void reset(UErrorCode& status) override;
        static UClassID U_EXPORT2 getStaticClassID();
        virtual UClassID getDynamicClassID() const override;
    private:
        int32_t pos;
        int32_t size;
        const TransliteratorRegistry& reg;
    };
    friend class Enumeration;

 private:

    /**
     * Dynamic registry mapping full IDs to Entry objects.  This
     * contains both public and internal entities.  The visibility is
     * controlled by whether an entry is listed in availableIDs and
     * specDAG or not.
     */
    Hashtable registry;

    /**
     * DAG of visible IDs by spec.  Hashtable: source => (Hashtable:
     * target => variant bitmask)
     */
    Hashtable specDAG;

    /**
     * Vector of all variant names
     */
    UVector variantList;

    /**
     * Vector of public full IDs.
     */
    Hashtable availableIDs;

    TransliteratorRegistry(const TransliteratorRegistry &other); // forbid copying of this class
    TransliteratorRegistry &operator=(const TransliteratorRegistry &other); // forbid copying of this class
};

U_NAMESPACE_END

U_CFUNC UBool utrans_transliterator_cleanup();

#endif /* #if !UCONFIG_NO_TRANSLITERATION */

#endif
//eof
