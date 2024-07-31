// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#ifndef __PLURALRANGES_H__
#define __PLURALRANGES_H__

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/uobject.h"
#include "unicode/locid.h"
#include "unicode/plurrule.h"
#include "standardplural.h"
#include "cmemory.h"

U_NAMESPACE_BEGIN

// Forward declarations
namespace number::impl {
class UFormattedNumberRangeData;
}

class StandardPluralRanges : public UMemory {
  public:
    /** Create a new StandardPluralRanges for the given locale */
    static StandardPluralRanges forLocale(const Locale& locale, UErrorCode& status);

    /** Explicit copy constructor */
    StandardPluralRanges copy(UErrorCode& status) const;

    /** Create an object (called on an rvalue) */
    LocalPointer<StandardPluralRanges> toPointer(UErrorCode& status) && noexcept;

    /** Select rule based on the first and second forms */
    StandardPlural::Form resolve(StandardPlural::Form first, StandardPlural::Form second) const;

    /** Used for data loading. */
    void addPluralRange(
        StandardPlural::Form first,
        StandardPlural::Form second,
        StandardPlural::Form result);

    /** Used for data loading. */
    void setCapacity(int32_t length, UErrorCode& status);

  private:
    struct StandardPluralRangeTriple {
        StandardPlural::Form first;
        StandardPlural::Form second;
        StandardPlural::Form result;
    };

    // TODO: An array is simple here, but it results in linear lookup time.
    // Certain locales have 20-30 entries in this list.
    // Consider changing to a smarter data structure.
    typedef MaybeStackArray<StandardPluralRangeTriple, 3> PluralRangeTriples;
    PluralRangeTriples fTriples;
    int32_t fTriplesLen = 0;
};

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */
#endif //__PLURALRANGES_H__
