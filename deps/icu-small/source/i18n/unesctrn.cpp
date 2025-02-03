// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 **********************************************************************
 *   Copyright (c) 2001-2011, International Business Machines
 *   Corporation and others.  All Rights Reserved.
 **********************************************************************
 *   Date        Name        Description
 *   11/19/2001  aliu        Creation.
 **********************************************************************
 */

#include "unicode/utypes.h"

#if !UCONFIG_NO_TRANSLITERATION

#include "unicode/uchar.h"
#include "unicode/utf16.h"
#include "unesctrn.h"
#include "util.h"

#include "cmemory.h"

U_NAMESPACE_BEGIN

/**
 * Special character marking the end of the spec[] array.
 */
static const char16_t END = 0xFFFF;

// Unicode: "U+10FFFF" hex, min=4, max=6
static const char16_t SPEC_Unicode[] = {
    2, 0, 16, 4, 6, 85/*U*/, 43/*+*/,
    END
};

// Java: "\\uFFFF" hex, min=4, max=4
static const char16_t SPEC_Java[] = {
    2, 0, 16, 4, 4, 92/*\*/, 117/*u*/,
    END
};

// C: "\\uFFFF" hex, min=4, max=4; \\U0010FFFF hex, min=8, max=8
static const char16_t SPEC_C[] = {
    2, 0, 16, 4, 4, 92/*\*/, 117/*u*/,
    2, 0, 16, 8, 8, 92/*\*/, 85/*U*/,
    END
};

// XML: "&#x10FFFF;" hex, min=1, max=6
static const char16_t SPEC_XML[] = {
    3, 1, 16, 1, 6, 38/*&*/, 35/*#*/, 120/*x*/, 59/*;*/,
    END
};

// XML10: "&#1114111;" dec, min=1, max=7 (not really "Hex-Any")
static const char16_t SPEC_XML10[] = {
    2, 1, 10, 1, 7, 38/*&*/, 35/*#*/, 59/*;*/,
    END
};

// Perl: "\\x{263A}" hex, min=1, max=6
static const char16_t SPEC_Perl[] = {
    3, 1, 16, 1, 6, 92/*\*/, 120/*x*/, 123/*{*/, 125/*}*/,
    END
};

// All: Java, C, Perl, XML, XML10, Unicode
static const char16_t SPEC_Any[] = {
    2, 0, 16, 4, 6, 85/*U*/, 43/*+*/,                      // Unicode
    2, 0, 16, 4, 4, 92/*\*/, 117/*u*/,                     // Java
    2, 0, 16, 8, 8, 92/*\*/, 85/*U*/,                      // C (surrogates)
    3, 1, 16, 1, 6, 38/*&*/, 35/*#*/, 120/*x*/, 59/*;*/,   // XML
    2, 1, 10, 1, 7, 38/*&*/, 35/*#*/, 59/*;*/,             // XML10
    3, 1, 16, 1, 6, 92/*\*/, 120/*x*/, 123/*{*/, 125/*}*/, // Perl
    END
};

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(UnescapeTransliterator)

static char16_t* copySpec(const char16_t* spec) {
    int32_t len = 0;
    while (spec[len] != END) {
        ++len;
    }
    ++len;
    char16_t* result = static_cast<char16_t*>(uprv_malloc(len * sizeof(char16_t)));
    // Check for memory allocation error. 
    if (result != nullptr) {
    	uprv_memcpy(result, spec, (size_t)len*sizeof(result[0]));
    }
    return result;
}

/**
 * Factory methods.  Ignore the context.
 */
static Transliterator* _createUnicode(const UnicodeString& ID, Transliterator::Token /*context*/) {
    return new UnescapeTransliterator(ID, SPEC_Unicode);
}
static Transliterator* _createJava(const UnicodeString& ID, Transliterator::Token /*context*/) {
    return new UnescapeTransliterator(ID, SPEC_Java);
}
static Transliterator* _createC(const UnicodeString& ID, Transliterator::Token /*context*/) {
    return new UnescapeTransliterator(ID, SPEC_C);
}
static Transliterator* _createXML(const UnicodeString& ID, Transliterator::Token /*context*/) {
    return new UnescapeTransliterator(ID, SPEC_XML);
}
static Transliterator* _createXML10(const UnicodeString& ID, Transliterator::Token /*context*/) {
    return new UnescapeTransliterator(ID, SPEC_XML10);
}
static Transliterator* _createPerl(const UnicodeString& ID, Transliterator::Token /*context*/) {
    return new UnescapeTransliterator(ID, SPEC_Perl);
}
static Transliterator* _createAny(const UnicodeString& ID, Transliterator::Token /*context*/) {
    return new UnescapeTransliterator(ID, SPEC_Any);
}

/**
 * Registers standard variants with the system.  Called by
 * Transliterator during initialization.
 */
void UnescapeTransliterator::registerIDs() {
    Token t = integerToken(0);

    Transliterator::_registerFactory(UNICODE_STRING_SIMPLE("Hex-Any/Unicode"), _createUnicode, t);

    Transliterator::_registerFactory(UNICODE_STRING_SIMPLE("Hex-Any/Java"), _createJava, t);

    Transliterator::_registerFactory(UNICODE_STRING_SIMPLE("Hex-Any/C"), _createC, t);

    Transliterator::_registerFactory(UNICODE_STRING_SIMPLE("Hex-Any/XML"), _createXML, t);

    Transliterator::_registerFactory(UNICODE_STRING_SIMPLE("Hex-Any/XML10"), _createXML10, t);

    Transliterator::_registerFactory(UNICODE_STRING_SIMPLE("Hex-Any/Perl"), _createPerl, t);

    Transliterator::_registerFactory(UNICODE_STRING_SIMPLE("Hex-Any"), _createAny, t);
}

/**
 * Constructor.  Takes the encoded spec array.
 */
UnescapeTransliterator::UnescapeTransliterator(const UnicodeString& newID,
                                               const char16_t *newSpec) :
    Transliterator(newID, nullptr)
{
    this->spec = copySpec(newSpec);
}

/**
 * Copy constructor.
 */
UnescapeTransliterator::UnescapeTransliterator(const UnescapeTransliterator& o) :
    Transliterator(o) {
    this->spec = copySpec(o.spec);
}

UnescapeTransliterator::~UnescapeTransliterator() {
    uprv_free(spec);
}

/**
 * Transliterator API.
 */
UnescapeTransliterator* UnescapeTransliterator::clone() const {
    return new UnescapeTransliterator(*this);
}

/**
 * Implements {@link Transliterator#handleTransliterate}.
 */
void UnescapeTransliterator::handleTransliterate(Replaceable& text, UTransPosition& pos,
                                                 UBool isIncremental) const {
    int32_t start = pos.start;
    int32_t limit = pos.limit;
    int32_t i, ipat;

    while (start < limit) {
        // Loop over the forms in spec[].  Exit this loop when we
        // match one of the specs.  Exit the outer loop if a
        // partial match is detected and isIncremental is true.
        for (ipat=0; spec[ipat] != END;) {

            // Read the header
            int32_t prefixLen = spec[ipat++];
            int32_t suffixLen = spec[ipat++];
            int8_t radix = static_cast<int8_t>(spec[ipat++]);
            int32_t minDigits = spec[ipat++];
            int32_t maxDigits = spec[ipat++];

            // s is a copy of start that is advanced over the
            // characters as we parse them.
            int32_t s = start;
            UBool match = true;

            for (i=0; i<prefixLen; ++i) {
                if (s >= limit) {
                    if (i > 0) {
                        // We've already matched a character.  This is
                        // a partial match, so we return if in
                        // incremental mode.  In non-incremental mode,
                        // go to the next spec.
                        if (isIncremental) {
                            goto exit;
                        }
                        match = false;
                        break;
                    }
                }
                char16_t c = text.charAt(s++);
                if (c != spec[ipat + i]) {
                    match = false;
                    break;
                }
            }

            if (match) {
                UChar32 u = 0;
                int32_t digitCount = 0;
                for (;;) {
                    if (s >= limit) {
                        // Check for partial match in incremental mode.
                        if (s > start && isIncremental) {
                            goto exit;
                        }
                        break;
                    }
                    UChar32 ch = text.char32At(s);
                    int32_t digit = u_digit(ch, radix);
                    if (digit < 0) {
                        break;
                    }
                    s += U16_LENGTH(ch);
                    u = (u * radix) + digit;
                    if (++digitCount == maxDigits) {
                        break;
                    }
                }

                match = (digitCount >= minDigits);

                if (match) {
                    for (i=0; i<suffixLen; ++i) {
                        if (s >= limit) {
                            // Check for partial match in incremental mode.
                            if (s > start && isIncremental) {
                                goto exit;
                            }
                            match = false;
                            break;
                        }
                        char16_t c = text.charAt(s++);
                        if (c != spec[ipat + prefixLen + i]) {
                            match = false;
                            break;
                        }
                    }

                    if (match) {
                        // At this point, we have a match
                        UnicodeString str(u);
                        text.handleReplaceBetween(start, s, str);
                        limit -= s - start - str.length();
                        // The following break statement leaves the
                        // loop that is traversing the forms in
                        // spec[].  We then parse the next input
                        // character.
                        break;
                    }
                }
            }

            ipat += prefixLen + suffixLen;
        }

        if (start < limit) {
            start += U16_LENGTH(text.char32At(start));
        }
    }

  exit:
    pos.contextLimit += limit - pos.limit;
    pos.limit = limit;
    pos.start = start;
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_TRANSLITERATION */

//eof
