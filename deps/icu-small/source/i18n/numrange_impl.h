// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __SOURCE_NUMRANGE_TYPES_H__
#define __SOURCE_NUMRANGE_TYPES_H__

#include "unicode/numberformatter.h"
#include "unicode/numberrangeformatter.h"
#include "unicode/simpleformatter.h"
#include "number_types.h"
#include "number_decimalquantity.h"
#include "number_formatimpl.h"
#include "formatted_string_builder.h"
#include "formattedval_impl.h"
#include "pluralranges.h"

U_NAMESPACE_BEGIN
namespace number::impl {

/**
 * Class similar to UFormattedNumberData.
 *
 * Has incomplete magic number logic that will need to be finished
 * if this is to be exposed as C API in the future.
 *
 * Possible magic number: 0x46445200
 * Reads in ASCII as "FDR" (FormatteDnumberRange with room at the end)
 */
class UFormattedNumberRangeData : public FormattedValueStringBuilderImpl {
public:
    UFormattedNumberRangeData() : FormattedValueStringBuilderImpl(kUndefinedField) {}
    virtual ~UFormattedNumberRangeData();

    DecimalQuantity quantity1;
    DecimalQuantity quantity2;
    UNumberRangeIdentityResult identityResult = UNUM_IDENTITY_RESULT_COUNT;
};


class NumberRangeFormatterImpl : public UMemory {
  public:
    NumberRangeFormatterImpl(const RangeMacroProps& macros, UErrorCode& status);

    void format(UFormattedNumberRangeData& data, bool equalBeforeRounding, UErrorCode& status) const;

  private:
    NumberFormatterImpl formatterImpl1;
    NumberFormatterImpl formatterImpl2;
    bool fSameFormatters;

    UNumberRangeCollapse fCollapse;
    UNumberRangeIdentityFallback fIdentityFallback;

    SimpleFormatter fRangeFormatter;
    NumberFormatterImpl fApproximatelyFormatter;

    StandardPluralRanges fPluralRanges;

    void formatSingleValue(UFormattedNumberRangeData& data,
                           MicroProps& micros1, MicroProps& micros2,
                           UErrorCode& status) const;

    void formatApproximately(UFormattedNumberRangeData& data,
                             MicroProps& micros1, MicroProps& micros2,
                             UErrorCode& status) const;

    void formatRange(UFormattedNumberRangeData& data,
                     MicroProps& micros1, MicroProps& micros2,
                     UErrorCode& status) const;

    const Modifier& resolveModifierPlurals(const Modifier& first, const Modifier& second) const;
};


/** Helper function used in upluralrules.cpp */
const UFormattedNumberRangeData* validateUFormattedNumberRange(
    const UFormattedNumberRange* uresult, UErrorCode& status);

} // namespace number::impl
U_NAMESPACE_END

#endif //__SOURCE_NUMRANGE_TYPES_H__
#endif /* #if !UCONFIG_NO_FORMATTING */
