// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMBER_ASFORMAT_H__
#define __NUMBER_ASFORMAT_H__

#include "unicode/numberformatter.h"
#include "number_types.h"
#include "number_decimalquantity.h"
#include "number_scientific.h"
#include "number_patternstring.h"
#include "number_modifiers.h"
#include "number_multiplier.h"
#include "number_roundingutils.h"
#include "decNumber.h"
#include "charstr.h"

U_NAMESPACE_BEGIN
namespace number::impl {

/**
 * A wrapper around LocalizedNumberFormatter implementing the Format interface, enabling improved
 * compatibility with other APIs.
 *
 * @see NumberFormatter
 */
class U_I18N_API LocalizedNumberFormatterAsFormat : public Format {
  public:
    LocalizedNumberFormatterAsFormat(const LocalizedNumberFormatter& formatter, const Locale& locale);

    /**
     * Destructor.
     */
    ~LocalizedNumberFormatterAsFormat() override;

    /**
     * Equals operator.
     */
    bool operator==(const Format& other) const override;

    /**
     * Creates a copy of this object.
     */
    LocalizedNumberFormatterAsFormat* clone() const override;

    /**
     * Formats a Number using the wrapped LocalizedNumberFormatter. The provided formattable must be a
     * number type.
     */
    UnicodeString& format(const Formattable& obj, UnicodeString& appendTo, FieldPosition& pos,
                          UErrorCode& status) const override;

    /**
     * Formats a Number using the wrapped LocalizedNumberFormatter. The provided formattable must be a
     * number type.
     */
    UnicodeString& format(const Formattable& obj, UnicodeString& appendTo, FieldPositionIterator* posIter,
                          UErrorCode& status) const override;

    /**
     * Not supported: sets an error index and returns.
     */
    void parseObject(const UnicodeString& source, Formattable& result,
                     ParsePosition& parse_pos) const override;

    /**
     * Gets the LocalizedNumberFormatter that this wrapper class uses to format numbers.
     *
     * For maximum efficiency, this function returns by const reference. You must copy the return value
     * into a local variable if you want to use it beyond the lifetime of the current object:
     *
     * <pre>
     * LocalizedNumberFormatter localFormatter = fmt->getNumberFormatter();
     * </pre>
     *
     * You can however use the return value directly when chaining:
     *
     * <pre>
     * FormattedNumber result = fmt->getNumberFormatter().formatDouble(514.23, status);
     * </pre>
     *
     * @return The unwrapped LocalizedNumberFormatter.
     */
    const LocalizedNumberFormatter& getNumberFormatter() const;

    UClassID getDynamicClassID() const override;
    static UClassID U_EXPORT2 getStaticClassID();

  private:
    LocalizedNumberFormatter fFormatter;

    // Even though the locale is inside the LocalizedNumberFormatter, we have to keep it here, too, because
    // LocalizedNumberFormatter doesn't have a getLocale() method, and ICU-TC didn't want to add one.
    Locale fLocale;
};

} // namespace number::impl
U_NAMESPACE_END

#endif // __NUMBER_ASFORMAT_H__

#endif /* #if !UCONFIG_NO_FORMATTING */
