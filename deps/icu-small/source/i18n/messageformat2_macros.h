// © 2024 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#include "unicode/utypes.h"

#ifndef U_HIDE_DEPRECATED_API

#ifndef MESSAGEFORMAT2_MACROS_H
#define MESSAGEFORMAT2_MACROS_H

#if U_SHOW_CPLUSPLUS_API

#if !UCONFIG_NO_FORMATTING

#if !UCONFIG_NO_MF2

#include "unicode/format.h"
#include "unicode/unistr.h"
#include "plurrule_impl.h"

U_NAMESPACE_BEGIN

namespace message2 {

using namespace pluralimpl;

// Tokens for parser and serializer

// Syntactically significant characters
#define LEFT_CURLY_BRACE ((UChar32)0x007B)
#define RIGHT_CURLY_BRACE ((UChar32)0x007D)
#define HTAB ((UChar32)0x0009)
#define CR ((UChar32)0x000D)
#define LF ((UChar32)0x000A)
#define IDEOGRAPHIC_SPACE ((UChar32)0x3000)

#define PIPE ((UChar32)0x007C)
#define EQUALS ((UChar32)0x003D)
#define DOLLAR ((UChar32)0x0024)
#define COLON ((UChar32)0x003A)
#define PLUS ((UChar32)0x002B)
#define HYPHEN ((UChar32)0x002D)
#define PERIOD ((UChar32)0x002E)
#define UNDERSCORE ((UChar32)0x005F)

#define LOWERCASE_E ((UChar32)0x0065)
#define UPPERCASE_E ((UChar32)0x0045)

// Reserved sigils
#define BANG ((UChar32)0x0021)
#define AT ((UChar32)0x0040)
#define PERCENT ((UChar32)0x0025)
#define CARET ((UChar32)0x005E)
#define AMPERSAND ((UChar32)0x0026)
#define LESS_THAN ((UChar32)0x003C)
#define GREATER_THAN ((UChar32)0x003E)
#define QUESTION ((UChar32)0x003F)
#define TILDE ((UChar32)0x007E)

// Fallback
#define REPLACEMENT ((UChar32) 0xFFFD)

// MessageFormat2 uses four keywords: `.input`, `.local`, `.when`, and `.match`.

static constexpr UChar32 ID_INPUT[] = {
    0x2E, 0x69, 0x6E, 0x70, 0x75, 0x74, 0 /* ".input" */
};

static constexpr UChar32 ID_LOCAL[] = {
    0x2E, 0x6C, 0x6F, 0x63, 0x61, 0x6C, 0 /* ".local" */
};

static constexpr UChar32 ID_MATCH[] = {
    0x2E, 0x6D, 0x61, 0x74, 0x63, 0x68, 0 /* ".match" */
};

// Returns immediately if `errorCode` indicates failure
#define CHECK_ERROR(errorCode)                                                                          \
    if (U_FAILURE(errorCode)) {                                                                         \
        return;                                                                                         \
    }

// Returns immediately if `errorCode` indicates failure
#define NULL_ON_ERROR(errorCode)                                                                          \
    if (U_FAILURE(errorCode)) {                                                                         \
        return nullptr;                                                                                         \
    }

// Returns immediately if `errorCode` indicates failure
#define THIS_ON_ERROR(errorCode)                                                                          \
    if (U_FAILURE(errorCode)) {                                                                         \
        return *this; \
    }

// Returns immediately if `errorCode` indicates failure
#define EMPTY_ON_ERROR(errorCode)                                                                          \
    if (U_FAILURE(errorCode)) {                                                                         \
        return {}; \
    }

} // namespace message2
U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_MF2 */

#endif /* #if !UCONFIG_NO_FORMATTING */

#endif /* U_SHOW_CPLUSPLUS_API */

#endif // MESSAGEFORMAT2_MACROS_H

#endif // U_HIDE_DEPRECATED_API
// eof
