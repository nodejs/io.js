// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*   Copyright (C) 2011-2013, International Business Machines
*   Corporation and others.  All Rights Reserved.
*******************************************************************************
*   file name:  ppucd.h
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2011dec11
*   created by: Markus W. Scherer
*/

#ifndef __PPUCD_H__
#define __PPUCD_H__

#include "unicode/utypes.h"
#include "unicode/uniset.h"
#include "unicode/unistr.h"

#include <stdio.h>

/** Additions to the uchar.h enum UProperty. */
enum {
    /** Name_Alias */
    PPUCD_NAME_ALIAS=UCHAR_STRING_LIMIT,
    PPUCD_CONDITIONAL_CASE_MAPPINGS,
    PPUCD_TURKIC_CASE_FOLDING
};

U_NAMESPACE_BEGIN

class U_TOOLUTIL_API PropertyNames {
public:
    virtual ~PropertyNames();
    virtual int32_t getPropertyEnum(const char *name) const = 0;
    virtual int32_t getPropertyValueEnum(int32_t property, const char *name) const = 0;
};

struct U_TOOLUTIL_API UniProps {
    UniProps();
    ~UniProps();

    int32_t getIntProp(int32_t prop) const { return intProps[prop-UCHAR_INT_START]; }

    UChar32 start, end;
    UBool binProps[UCHAR_BINARY_LIMIT];
    int32_t intProps[UCHAR_INT_LIMIT-UCHAR_INT_START];
    UVersionInfo age;
    UChar32 bmg, bpb;
    UChar32 scf, slc, stc, suc;
    int32_t digitValue;
    const char *numericValue;
    const char *name;
    const char *nameAlias;
    UnicodeString cf, lc, tc, uc;
    UnicodeSet scx, idType;
};

class U_TOOLUTIL_API PreparsedUCD {
public:
    enum LineType {
        /** No line, end of file. */
        NO_LINE,
        /** Empty line. (Might contain a comment.) */
        EMPTY_LINE,

        /** ucd;6.1.0 */
        UNICODE_VERSION_LINE,

        /** property;Binary;Alpha;Alphabetic */
        PROPERTY_LINE,
        /** binary;N;No;F;False */
        BINARY_LINE,
        /** value;gc;Zs;Space_Separator */
        VALUE_LINE,

        /** defaults;0000..10FFFF;age=NA;bc=L;... */
        DEFAULTS_LINE,
        /** block;0000..007F;age=1.1;blk=ASCII;ea=Na;... */
        BLOCK_LINE,
        /** cp;0030;AHex;bc=EN;gc=Nd;na=DIGIT ZERO;... */
        CP_LINE,
        /** unassigned;E01F0..E0FFF;bc=BN;CWKCF;DI;GCB=CN;NFKC_CF= */
        UNASSIGNED_LINE,

        /** algnamesrange;4E00..9FCC;han;CJK UNIFIED IDEOGRAPH- */
        ALG_NAMES_RANGE_LINE,

        LINE_TYPE_COUNT
    };

    /**
     * Constructor.
     * Prepare this object for a new, empty package.
     */
    PreparsedUCD(const char *filename, UErrorCode &errorCode);

    /** Destructor. */
    ~PreparsedUCD();

    /** Sets (aliases) a PropertyNames implementation. Caller retains ownership. */
    void setPropertyNames(const PropertyNames *pn) { pnames=pn; }

    /**
     * Reads a line from the preparsed UCD file.
     * Splits the line by replacing each ';' with a NUL.
     */
    LineType readLine(UErrorCode &errorCode);

    /** Returns the number of the line read by readLine(). */
    int32_t getLineNumber() const { return lineNumber; }

    /** Returns the line's next field, or nullptr. */
    const char *nextField();

    /** Returns the Unicode version when or after the UNICODE_VERSION_LINE has been read. */
    const UVersionInfo &getUnicodeVersion() const { return ucdVersion; }

    /** Returns true if the current line has property values. */
    UBool lineHasPropertyValues() const {
        return DEFAULTS_LINE<=lineType && lineType<=UNASSIGNED_LINE;
    }

    /**
     * Parses properties from the current line.
     * Clears newValues and sets UProperty codes for property values mentioned
     * on the current line (as opposed to being inherited).
     * Returns a pointer to the filled-in UniProps, or nullptr if something went wrong.
     * The returned UniProps are usable until the next line of the same type is read.
     */
    const UniProps *getProps(UnicodeSet &newValues, UErrorCode &errorCode);

    /**
     * Returns the code point range for the current algnamesrange line.
     * Calls & parses nextField().
     * Further nextField() calls will yield the range's type & prefix string.
     * Returns U_SUCCESS(errorCode).
     */
    UBool getRangeForAlgNames(UChar32 &start, UChar32 &end, UErrorCode &errorCode);

private:
    UBool isLineBufferAvailable(int32_t i) {
        return defaultLineIndex!=i && blockLineIndex!=i;
    }

    /** Resets the field iterator and returns the line's first field (the line type field). */
    const char *firstField();

    UBool parseProperty(UniProps &props, const char *field, UnicodeSet &newValues,
                        UErrorCode &errorCode);
    UChar32 parseCodePoint(const char *s, UErrorCode &errorCode);
    UBool parseCodePointRange(const char *s, UChar32 &start, UChar32 &end, UErrorCode &errorCode);
    void parseString(const char *s, UnicodeString &uni, UErrorCode &errorCode);
    void parseScriptExtensions(const char *s, UnicodeSet &scx, UErrorCode &errorCode);
    void parseIdentifierType(const char *s, UnicodeSet &idType, UErrorCode &errorCode);

    static const int32_t kNumLineBuffers=3;

    const PropertyNames *pnames;  // aliased
    FILE *file;
    int32_t defaultLineIndex, blockLineIndex, lineIndex;
    int32_t lineNumber;
    LineType lineType;
    char *fieldLimit;
    char *lineLimit;

    UVersionInfo ucdVersion;
    UniProps defaultProps, blockProps, cpProps;
    UnicodeSet blockValues;
    // Multiple lines so that default and block properties can maintain pointers
    // into their line buffers.
    char lines[kNumLineBuffers][4096];
};

U_NAMESPACE_END

#endif  // __PPUCD_H__
