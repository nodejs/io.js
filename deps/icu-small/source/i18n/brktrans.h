// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (C) 2008-2015, International Business Machines
*   Corporation and others.  All Rights Reserved.
**********************************************************************
*   Date        Name        Description
*   05/11/2008  Andy Heninger  Ported from Java
**********************************************************************
*/
#ifndef BRKTRANS_H
#define BRKTRANS_H

#include "unicode/utypes.h"

#if !UCONFIG_NO_TRANSLITERATION && !UCONFIG_NO_BREAK_ITERATION

#include "unicode/translit.h"

#include "unicode/localpointer.h"


U_NAMESPACE_BEGIN

class UVector32;

/**
 * A transliterator that pInserts the specified characters at word breaks.
 * To restrict it to particular characters, use a filter.
 * TODO: this is an internal class, and only temporary. 
 * Remove it once we have \b notation in Transliterator.
 */
class BreakTransliterator : public Transliterator {
public:

    /**
     * Constructs a transliterator.
     * @param adoptedFilter    the filter for this transliterator.
     */
    BreakTransliterator(UnicodeFilter* adoptedFilter = nullptr);

    /**
     * Destructor.
     */
    virtual ~BreakTransliterator();

    /**
     * Copy constructor.
     */
    BreakTransliterator(const BreakTransliterator&);

    /**
     * Transliterator API.
     * @return    A copy of the object.
     */
    virtual BreakTransliterator* clone() const override;

    virtual const UnicodeString &getInsertion() const;

    virtual void setInsertion(const UnicodeString &insertion);

    /**
     * ICU "poor man's RTTI", returns a UClassID for the actual class.
     */
    virtual UClassID getDynamicClassID() const override;

    /**
     * ICU "poor man's RTTI", returns a UClassID for this class.
     */
    U_I18N_API static UClassID U_EXPORT2 getStaticClassID();

 protected:

    /**
     * Implements {@link Transliterator#handleTransliterate}.
     * @param text          the buffer holding transliterated and
     *                      untransliterated text
     * @param offset        the start and limit of the text, the position
     *                      of the cursor, and the start and limit of transliteration.
     * @param incremental   if true, assume more text may be coming after
     *                      pos.contextLimit. Otherwise, assume the text is complete.
     */
    virtual void handleTransliterate(Replaceable& text, UTransPosition& offset,
                                     UBool isIncremental) const override;

 private:
     LocalPointer<BreakIterator> cachedBI;
     LocalPointer<UVector32>     cachedBoundaries;
     UnicodeString               fInsertion;

     static UnicodeString replaceableAsString(Replaceable &r);

    /**
     * Assignment operator.
     */
    BreakTransliterator& operator=(const BreakTransliterator&);
};

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_TRANSLITERATION */

#endif
