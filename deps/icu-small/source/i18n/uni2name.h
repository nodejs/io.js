// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
*   Copyright (C) 2001-2007, International Business Machines
*   Corporation and others.  All Rights Reserved.
**********************************************************************
*   Date        Name        Description
*   06/06/01    aliu        Creation.
**********************************************************************
*/
#ifndef UNI2NAME_H
#define UNI2NAME_H

#include "unicode/utypes.h"

#if !UCONFIG_NO_TRANSLITERATION

#include "unicode/translit.h"

U_NAMESPACE_BEGIN

/**
 * A transliterator that performs character to name mapping.
 * It generates the Perl syntax \N{name}.
 * @author Alan Liu
 */
class UnicodeNameTransliterator : public Transliterator {

 public:

    /**
     * Constructs a transliterator.
     * @param adoptedFilter the filter to be adopted.
     */
    UnicodeNameTransliterator(UnicodeFilter* adoptedFilter = nullptr);

    /**
     * Destructor.
     */
    virtual ~UnicodeNameTransliterator();

    /**
     * Copy constructor.
     */
    UnicodeNameTransliterator(const UnicodeNameTransliterator&);

    /**
     * Transliterator API.
     */
    virtual UnicodeNameTransliterator* clone() const override;

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
     * @param text        the buffer holding transliterated and
     *                    untransliterated text
     * @param offset      the start and limit of the text, the position
     *                    of the cursor, and the start and limit of transliteration.
     * @param incremental if true, assume more text may be coming after
     *                    pos.contextLimit.  Otherwise, assume the text is complete.
     */
    virtual void handleTransliterate(Replaceable& text, UTransPosition& offset,
                                     UBool isIncremental) const override;

private:
    /**
     * Assignment operator.
     */
    UnicodeNameTransliterator& operator=(const UnicodeNameTransliterator&);

};

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_TRANSLITERATION */

#endif
