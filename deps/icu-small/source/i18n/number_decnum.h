// © 2017 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMBER_DECNUM_H__
#define __NUMBER_DECNUM_H__

#include "decNumber.h"
#include "charstr.h"
#include "bytesinkutil.h"

U_NAMESPACE_BEGIN

#define DECNUM_INITIAL_CAPACITY 34

// Export an explicit template instantiation of the MaybeStackHeaderAndArray that is used as a data member of DecNum.
// When building DLLs for Windows this is required even though no direct access to the MaybeStackHeaderAndArray leaks out of the i18n library.
// (See digitlst.h, pluralaffix.h, datefmt.h, and others for similar examples.)
#if U_PF_WINDOWS <= U_PLATFORM && U_PLATFORM <= U_PF_CYGWIN
template class U_I18N_API MaybeStackHeaderAndArray<decNumber, char, DECNUM_INITIAL_CAPACITY>;
#endif

namespace number::impl {

/** A very thin C++ wrapper around decNumber.h */
// Exported as U_I18N_API for tests
class U_I18N_API DecNum : public UMemory {
  public:
    DecNum();  // leaves object in valid but undefined state

    // Copy-like constructor; use the default move operators.
    DecNum(const DecNum& other, UErrorCode& status);

    /** Sets the decNumber to the StringPiece. */
    void setTo(StringPiece str, UErrorCode& status);

    /** Sets the decNumber to the NUL-terminated char string. */
    void setTo(const char* str, UErrorCode& status);

    /** Uses double_conversion to set this decNumber to the given double. */
    void setTo(double d, UErrorCode& status);

    /** Sets the decNumber to the BCD representation. */
    void setTo(const uint8_t* bcd, int32_t length, int32_t scale, bool isNegative, UErrorCode& status);

    void normalize();

    void multiplyBy(const DecNum& rhs, UErrorCode& status);

    void divideBy(const DecNum& rhs, UErrorCode& status);

    bool isNegative() const;

    bool isZero() const;

    /** Is infinity or NaN */
    bool isSpecial() const;

    bool isInfinity() const;

    bool isNaN() const;

    void toString(ByteSink& output, UErrorCode& status) const;

    inline CharString toCharString(UErrorCode& status) const {
      CharString cstr;
      CharStringByteSink sink(&cstr);
      toString(sink, status);
      return cstr;
    }

    inline const decNumber* getRawDecNumber() const {
        return fData.getAlias();
    }

  private:
    static constexpr int32_t kDefaultDigits = DECNUM_INITIAL_CAPACITY;
    MaybeStackHeaderAndArray<decNumber, char, kDefaultDigits> fData;
    decContext fContext;

    void _setTo(const char* str, int32_t maxDigits, UErrorCode& status);
};

} // namespace number::impl

U_NAMESPACE_END

#endif // __NUMBER_DECNUM_H__

#endif /* #if !UCONFIG_NO_FORMATTING */
