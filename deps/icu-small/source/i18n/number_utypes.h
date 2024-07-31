// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __SOURCE_NUMBER_UTYPES_H__
#define __SOURCE_NUMBER_UTYPES_H__

#include "unicode/numberformatter.h"
#include "number_types.h"
#include "number_decimalquantity.h"
#include "formatted_string_builder.h"
#include "formattedval_impl.h"

U_NAMESPACE_BEGIN
namespace number::impl {

/** Helper function used in upluralrules.cpp */
const DecimalQuantity* validateUFormattedNumberToDecimalQuantity(
    const UFormattedNumber* uresult, UErrorCode& status);


/**
 * Struct for data used by FormattedNumber.
 *
 * This struct is held internally by the C++ version FormattedNumber since the member types are not
 * declared in the public header file.
 *
 * Exported as U_I18N_API for tests
 */
class U_I18N_API UFormattedNumberData : public FormattedValueStringBuilderImpl {
public:
    UFormattedNumberData() : FormattedValueStringBuilderImpl(kUndefinedField) {}
    virtual ~UFormattedNumberData();

    UFormattedNumberData(UFormattedNumberData&&) = default;
    UFormattedNumberData& operator=(UFormattedNumberData&&) = default;

    // The formatted quantity.
    DecimalQuantity quantity;

    // The output unit for the formatted quantity.
    // TODO(units,hugovdm): populate this correctly for the general case - it's
    // currently only implemented for the .usage() use case.
    MeasureUnit outputUnit;

    // The gender of the formatted output.
    const char *gender = "";
};

} // namespace number::impl
U_NAMESPACE_END

#endif //__SOURCE_NUMBER_UTYPES_H__
#endif /* #if !UCONFIG_NO_FORMATTING */
