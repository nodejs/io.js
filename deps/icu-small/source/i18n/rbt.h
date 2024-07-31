// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (C) 1999-2007, International Business Machines
*   Corporation and others.  All Rights Reserved.
**********************************************************************
*   Date        Name        Description
*   11/17/99    aliu        Creation.
**********************************************************************
*/
#ifndef RBT_H
#define RBT_H

#include "unicode/utypes.h"

#if !UCONFIG_NO_TRANSLITERATION

#include "unicode/translit.h"
#include "unicode/utypes.h"
#include "unicode/parseerr.h"
#include "unicode/udata.h"

#define U_ICUDATA_TRANSLIT U_ICUDATA_NAME U_TREE_SEPARATOR_STRING "translit"

U_NAMESPACE_BEGIN

class TransliterationRuleData;

/**
 * <code>RuleBasedTransliterator</code> is a transliterator
 * built from a set of rules as defined for
 * Transliterator::createFromRules().
 * See the C++ class Transliterator documentation for the rule syntax.
 *
 * @author Alan Liu
 * @internal Use transliterator factory methods instead since this class will be removed in that release.
 */
class RuleBasedTransliterator : public Transliterator {
private:
    /**
     * The data object is immutable, so we can freely share it with
     * other instances of RBT, as long as we do NOT own this object.
     *  TODO:  data is no longer immutable.  See bugs #1866, 2155
     */
    TransliterationRuleData* fData;

    /**
     * If true, we own the data object and must delete it.
     */
    UBool isDataOwned;

public:

    /**
     * Constructs a new transliterator from the given rules.
     * @param rules rules, separated by ';'
     * @param direction either FORWARD or REVERSE.
     * @exception IllegalArgumentException if rules are malformed.
     * @internal Use transliterator factory methods instead since this class will be removed in that release.
     */
    RuleBasedTransliterator(const UnicodeString& id,
                            const UnicodeString& rules,
                            UTransDirection direction,
                            UnicodeFilter* adoptedFilter,
                            UParseError& parseError,
                            UErrorCode& status);

    /**
     * Constructs a new transliterator from the given rules.
     * @param rules rules, separated by ';'
     * @param direction either FORWARD or REVERSE.
     * @exception IllegalArgumentException if rules are malformed.
     * @internal Use transliterator factory methods instead since this class will be removed in that release.
     */
    /*RuleBasedTransliterator(const UnicodeString& id,
                            const UnicodeString& rules,
                            UTransDirection direction,
                            UnicodeFilter* adoptedFilter,
                            UErrorCode& status);*/

    /**
     * Convenience constructor with no filter.
     * @internal Use transliterator factory methods instead since this class will be removed in that release.
     */
    /*RuleBasedTransliterator(const UnicodeString& id,
                            const UnicodeString& rules,
                            UTransDirection direction,
                            UErrorCode& status);*/

    /**
     * Convenience constructor with no filter and FORWARD direction.
     * @internal Use transliterator factory methods instead since this class will be removed in that release.
     */
    /*RuleBasedTransliterator(const UnicodeString& id,
                            const UnicodeString& rules,
                            UErrorCode& status);*/

    /**
     * Convenience constructor with FORWARD direction.
     * @internal Use transliterator factory methods instead since this class will be removed in that release.
     */
    /*RuleBasedTransliterator(const UnicodeString& id,
                            const UnicodeString& rules,
                            UnicodeFilter* adoptedFilter,
                            UErrorCode& status);*/
private:

     friend class TransliteratorRegistry; // to access TransliterationRuleData convenience ctor
    /**
     * Convenience constructor.
     * @param id            the id for the transliterator.
     * @param theData       the rule data for the transliterator.
     * @param adoptedFilter the filter for the transliterator
     */
    RuleBasedTransliterator(const UnicodeString& id,
                            const TransliterationRuleData* theData,
                            UnicodeFilter* adoptedFilter = nullptr);


    friend class Transliterator; // to access following ct

    /**
     * Internal constructor.
     * @param id            the id for the transliterator.
     * @param theData       the rule data for the transliterator.
     * @param isDataAdopted determine who will own the 'data' object. True, the caller should not delete 'data'.
     */
    RuleBasedTransliterator(const UnicodeString& id,
                            TransliterationRuleData* data,
                            UBool isDataAdopted);

public:

    /**
     * Copy constructor.
     * @internal Use transliterator factory methods instead since this class will be removed in that release.
     */
    RuleBasedTransliterator(const RuleBasedTransliterator&);

    virtual ~RuleBasedTransliterator();

    /**
     * Implement Transliterator API.
     * @internal Use transliterator factory methods instead since this class will be removed in that release.
     */
    virtual RuleBasedTransliterator* clone() const override;

protected:
    /**
     * Implements {@link Transliterator#handleTransliterate}.
     * @internal Use transliterator factory methods instead since this class will be removed in that release.
     */
    virtual void handleTransliterate(Replaceable& text, UTransPosition& offsets,
                                     UBool isIncremental) const override;

public:
    /**
     * Return a representation of this transliterator as source rules.
     * These rules will produce an equivalent transliterator if used
     * to construct a new transliterator.
     * @param result the string to receive the rules.  Previous
     * contents will be deleted.
     * @param escapeUnprintable if true then convert unprintable
     * character to their hex escape representations, \uxxxx or
     * \Uxxxxxxxx.  Unprintable characters are those other than
     * U+000A, U+0020..U+007E.
     * @internal Use transliterator factory methods instead since this class will be removed in that release.
     */
    virtual UnicodeString& toRules(UnicodeString& result,
                                   UBool escapeUnprintable) const override;

protected:
    /**
     * Implement Transliterator framework
     */
    virtual void handleGetSourceSet(UnicodeSet& result) const override;

public:
    /**
     * Override Transliterator framework
     */
    virtual UnicodeSet& getTargetSet(UnicodeSet& result) const override;

    /**
     * Return the class ID for this class.  This is useful only for
     * comparing to a return value from getDynamicClassID().  For example:
     * <pre>
     * .      Base* polymorphic_pointer = createPolymorphicObject();
     * .      if (polymorphic_pointer->getDynamicClassID() ==
     * .          Derived::getStaticClassID()) ...
     * </pre>
     * @return          The class ID for all objects of this class.
     * @internal Use transliterator factory methods instead since this class will be removed in that release.
     */
    U_I18N_API static UClassID U_EXPORT2 getStaticClassID();

    /**
     * Returns a unique class ID <b>polymorphically</b>.  This method
     * is to implement a simple version of RTTI, since not all C++
     * compilers support genuine RTTI.  Polymorphic operator==() and
     * clone() methods call this method.
     * 
     * @return The class ID for this object. All objects of a given
     * class have the same class ID.  Objects of other classes have
     * different class IDs.
     */
    virtual UClassID getDynamicClassID() const override;

private:

    void _construct(const UnicodeString& rules,
                    UTransDirection direction,
                    UParseError& parseError,
                    UErrorCode& status);
};


U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_TRANSLITERATION */

#endif
