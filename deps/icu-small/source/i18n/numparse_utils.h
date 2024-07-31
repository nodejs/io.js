// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING
#ifndef __NUMPARSE_UTILS_H__
#define __NUMPARSE_UTILS_H__

#include "numparse_types.h"
#include "unicode/uniset.h"

U_NAMESPACE_BEGIN
namespace numparse::impl::utils {

inline static void putLeadCodePoints(const UnicodeSet* input, UnicodeSet* output) {
    for (int32_t i = 0; i < input->getRangeCount(); i++) {
        output->add(input->getRangeStart(i), input->getRangeEnd(i));
    }
    // TODO: ANDY: How to iterate over the strings in ICU4C UnicodeSet?
}

inline static void putLeadCodePoint(const UnicodeString& input, UnicodeSet* output) {
    if (!input.isEmpty()) {
        output->add(input.char32At(0));
    }
}

inline static void copyCurrencyCode(char16_t* dest, const char16_t* src) {
    uprv_memcpy(dest, src, sizeof(char16_t) * 3);
    dest[3] = 0;
}

} // namespace numparse::impl::utils
U_NAMESPACE_END

#endif //__NUMPARSE_UTILS_H__
#endif /* #if !UCONFIG_NO_FORMATTING */
