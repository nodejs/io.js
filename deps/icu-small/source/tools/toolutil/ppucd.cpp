// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
*   Copyright (C) 2011-2014, International Business Machines
*   Corporation and others.  All Rights Reserved.
*******************************************************************************
*   file name:  ppucd.cpp
*   encoding:   UTF-8
*   tab size:   8 (not used)
*   indentation:4
*
*   created on: 2011dec11
*   created by: Markus W. Scherer
*/

#include "unicode/utypes.h"
#include "unicode/uchar.h"
#include "charstr.h"
#include "cstring.h"
#include "ppucd.h"
#include "uassert.h"
#include "uparse.h"

#include <stdio.h>
#include <string.h>

U_NAMESPACE_BEGIN

PropertyNames::~PropertyNames() {}

UniProps::UniProps()
        : start(U_SENTINEL), end(U_SENTINEL),
          bmg(U_SENTINEL), bpb(U_SENTINEL),
          scf(U_SENTINEL), slc(U_SENTINEL), stc(U_SENTINEL), suc(U_SENTINEL),
          digitValue(-1), numericValue(nullptr),
          name(nullptr), nameAlias(nullptr) {
    memset(binProps, 0, sizeof(binProps));
    memset(intProps, 0, sizeof(intProps));
    memset(age, 0, 4);
}

UniProps::~UniProps() {}

const int32_t PreparsedUCD::kNumLineBuffers;

PreparsedUCD::PreparsedUCD(const char *filename, UErrorCode &errorCode)
        : pnames(nullptr),
          file(nullptr),
          defaultLineIndex(-1), blockLineIndex(-1), lineIndex(0),
          lineNumber(0),
          lineType(NO_LINE),
          fieldLimit(nullptr), lineLimit(nullptr) {
    if(U_FAILURE(errorCode)) { return; }

    if(filename==nullptr || *filename==0 || (*filename=='-' && filename[1]==0)) {
        filename=nullptr;
        file=stdin;
    } else {
        file=fopen(filename, "r");
    }
    if(file==nullptr) {
        perror("error opening preparsed UCD");
        fprintf(stderr, "error opening preparsed UCD file %s\n", filename ? filename : "\"no file name given\"");
        errorCode=U_FILE_ACCESS_ERROR;
        return;
    }

    memset(ucdVersion, 0, 4);
    lines[0][0]=0;
}

PreparsedUCD::~PreparsedUCD() {
    if(file!=stdin) {
        fclose(file);
    }
}

// Same order as the LineType values.
static const char *lineTypeStrings[]={
    nullptr,
    nullptr,
    "ucd",
    "property",
    "binary",
    "value",
    "defaults",
    "block",
    "cp",
    "unassigned",
    "algnamesrange"
};

PreparsedUCD::LineType
PreparsedUCD::readLine(UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return NO_LINE; }
    // Select the next available line buffer.
    while(!isLineBufferAvailable(lineIndex)) {
        ++lineIndex;
        if (lineIndex == kNumLineBuffers) {
            lineIndex = 0;
        }
    }
    char *line=lines[lineIndex];
    *line=0;
    lineLimit=fieldLimit=line;
    lineType=NO_LINE;
    char *result=fgets(line, sizeof(lines[0]), file);
    if(result==nullptr) {
        if(ferror(file)) {
            perror("error reading preparsed UCD");
            fprintf(stderr, "error reading preparsed UCD before line %ld\n", static_cast<long>(lineNumber));
            errorCode=U_FILE_ACCESS_ERROR;
        }
        return NO_LINE;
    }
    ++lineNumber;
    if(*line=='#') {
        fieldLimit=strchr(line, 0);
        return lineType=EMPTY_LINE;
    }
    // Remove trailing /r/n.
    char c;
    char *limit=strchr(line, 0);
    while(line<limit && ((c=*(limit-1))=='\n' || c=='\r')) { --limit; }
    // Remove trailing white space.
    while(line<limit && ((c=*(limit-1))==' ' || c=='\t')) { --limit; }
    *limit=0;
    lineLimit=limit;
    if(line==limit) {
        fieldLimit=limit;
        return lineType=EMPTY_LINE;
    }
    // Split by ';'.
    char *semi=line;
    while((semi=strchr(semi, ';'))!=nullptr) { *semi++=0; }
    fieldLimit=strchr(line, 0);
    // Determine the line type.
    int32_t type;
    for(type=EMPTY_LINE+1;; ++type) {
        if(type==LINE_TYPE_COUNT) {
            fprintf(stderr,
                    "error in preparsed UCD: unknown line type (first field) '%s' on line %ld\n",
                    line, static_cast<long>(lineNumber));
            errorCode=U_PARSE_ERROR;
            return NO_LINE;
        }
        if(0==strcmp(line, lineTypeStrings[type])) {
            break;
        }
    }
    lineType = static_cast<LineType>(type);
    if(lineType==UNICODE_VERSION_LINE && fieldLimit<lineLimit) {
        u_versionFromString(ucdVersion, fieldLimit+1);
    }
    return lineType;
}

const char *
PreparsedUCD::firstField() {
    char *field=lines[lineIndex];
    fieldLimit=strchr(field, 0);
    return field;
}

const char *
PreparsedUCD::nextField() {
    if(fieldLimit==lineLimit) { return nullptr; }
    char *field=fieldLimit+1;
    fieldLimit=strchr(field, 0);
    return field;
}

const UniProps *
PreparsedUCD::getProps(UnicodeSet &newValues, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return nullptr; }
    newValues.clear();
    if(!lineHasPropertyValues()) {
        errorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }
    firstField();
    const char *field=nextField();
    if(field==nullptr) {
        // No range field after the type.
        fprintf(stderr,
                "error in preparsed UCD: missing default/block/cp range field "
                "(no second field) on line %ld\n",
                static_cast<long>(lineNumber));
        errorCode=U_PARSE_ERROR;
        return nullptr;
    }
    UChar32 start, end;
    if(!parseCodePointRange(field, start, end, errorCode)) { return nullptr; }
    UniProps *props;
    UBool insideBlock=false;  // true if cp or unassigned range inside the block range.
    switch(lineType) {
    case DEFAULTS_LINE:
        // Should occur before any block/cp/unassigned line.
        if(blockLineIndex>=0) {
            fprintf(stderr,
                    "error in preparsed UCD: default line %ld after one or more block lines\n",
                    static_cast<long>(lineNumber));
            errorCode=U_PARSE_ERROR;
            return nullptr;
        }
        if(defaultLineIndex>=0) {
            fprintf(stderr,
                    "error in preparsed UCD: second line with default properties on line %ld\n",
                    static_cast<long>(lineNumber));
            errorCode=U_PARSE_ERROR;
            return nullptr;
        }
        if(start!=0 || end!=0x10ffff) {
            fprintf(stderr,
                    "error in preparsed UCD: default range must be 0..10FFFF, not '%s' on line %ld\n",
                    field, static_cast<long>(lineNumber));
            errorCode=U_PARSE_ERROR;
            return nullptr;
        }
        props=&defaultProps;
        defaultLineIndex=lineIndex;
        break;
    case BLOCK_LINE:
        blockProps=defaultProps;  // Block inherits default properties.
        props=&blockProps;
        blockLineIndex=lineIndex;
        break;
    case CP_LINE:
    case UNASSIGNED_LINE:
        if(blockProps.start<=start && end<=blockProps.end) {
            insideBlock=true;
            if(lineType==CP_LINE) {
                // Code point range fully inside the last block inherits the block properties.
                cpProps=blockProps;
            } else {
                // Unassigned line inside the block is based on default properties
                // which override block properties.
                cpProps=defaultProps;
                newValues=blockValues;
                // Except, it inherits the one blk=Block property.
                int32_t blkIndex=UCHAR_BLOCK-UCHAR_INT_START;
                cpProps.intProps[blkIndex]=blockProps.intProps[blkIndex];
                newValues.remove(static_cast<UChar32>(UCHAR_BLOCK));
            }
        } else if(start>blockProps.end || end<blockProps.start) {
            // Code point range fully outside the last block inherits the default properties.
            cpProps=defaultProps;
        } else {
            // Code point range partially overlapping with the last block is illegal.
            fprintf(stderr,
                    "error in preparsed UCD: cp range %s on line %ld only "
                    "partially overlaps with block range %04lX..%04lX\n",
                    field, static_cast<long>(lineNumber), static_cast<long>(blockProps.start), static_cast<long>(blockProps.end));
            errorCode=U_PARSE_ERROR;
            return nullptr;
        }
        props=&cpProps;
        break;
    default:
        // Will not occur because of the range check above.
        errorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }
    props->start=start;
    props->end=end;
    while((field=nextField())!=nullptr) {
        if(!parseProperty(*props, field, newValues, errorCode)) { return nullptr; }
    }
    if(lineType==BLOCK_LINE) {
        blockValues=newValues;
    } else if(lineType==UNASSIGNED_LINE && insideBlock) {
        // Unset newValues for values that are the same as the block values.
        for(int32_t prop=0; prop<UCHAR_BINARY_LIMIT; ++prop) {
            if(newValues.contains(prop) && cpProps.binProps[prop]==blockProps.binProps[prop]) {
                newValues.remove(prop);
            }
        }
        for(int32_t prop=UCHAR_INT_START; prop<UCHAR_INT_LIMIT; ++prop) {
            int32_t index=prop-UCHAR_INT_START;
            if(newValues.contains(prop) && cpProps.intProps[index]==blockProps.intProps[index]) {
                newValues.remove(prop);
            }
        }
    }
    return props;
}

static const struct {
    const char *name;
    int32_t prop;
} ppucdProperties[]={
    { "Name_Alias", PPUCD_NAME_ALIAS },
    { "Conditional_Case_Mappings", PPUCD_CONDITIONAL_CASE_MAPPINGS },
    { "Turkic_Case_Folding", PPUCD_TURKIC_CASE_FOLDING }
};

// Returns true for "ok to continue parsing fields".
UBool
PreparsedUCD::parseProperty(UniProps &props, const char *field, UnicodeSet &newValues,
                            UErrorCode &errorCode) {
    CharString pBuffer;
    const char *p=field;
    const char *v=strchr(p, '=');
    int binaryValue;
    if(*p=='-') {
        if(v!=nullptr) {
            fprintf(stderr,
                    "error in preparsed UCD: mix of binary-property-no and "
                    "enum-property syntax '%s' on line %ld\n",
                    field, static_cast<long>(lineNumber));
            errorCode=U_PARSE_ERROR;
            return false;
        }
        binaryValue=0;
        ++p;
    } else if(v==nullptr) {
        binaryValue=1;
    } else {
        binaryValue=-1;
        // Copy out the property name rather than modifying the field (writing a NUL).
        pBuffer.append(p, static_cast<int32_t>(v - p), errorCode);
        p=pBuffer.data();
        ++v;
    }
    int32_t prop=pnames->getPropertyEnum(p);
    if(prop<0) {
        for(int32_t i=0;; ++i) {
            if(i==UPRV_LENGTHOF(ppucdProperties)) {
                // Ignore unknown property names.
                return true;
            }
            if(0==uprv_stricmp(p, ppucdProperties[i].name)) {
                prop=ppucdProperties[i].prop;
                U_ASSERT(prop>=0);
                break;
            }
        }
    }
    if(prop<UCHAR_BINARY_LIMIT) {
        if(binaryValue>=0) {
            props.binProps[prop] = static_cast<UBool>(binaryValue);
        } else {
            // No binary value for a binary property.
            fprintf(stderr,
                    "error in preparsed UCD: enum-property syntax '%s' "
                    "for binary property on line %ld\n",
                    field, static_cast<long>(lineNumber));
            errorCode=U_PARSE_ERROR;
        }
    } else if(binaryValue>=0) {
        // Binary value for a non-binary property.
        fprintf(stderr,
                "error in preparsed UCD: binary-property syntax '%s' "
                "for non-binary property on line %ld\n",
                field, static_cast<long>(lineNumber));
        errorCode=U_PARSE_ERROR;
    } else if (prop < UCHAR_INT_START) {
        fprintf(stderr,
                "error in preparsed UCD: prop value is invalid: '%d' for line %ld\n",
                prop, static_cast<long>(lineNumber));
        errorCode=U_PARSE_ERROR;
    } else if(prop<UCHAR_INT_LIMIT) {
        int32_t value=pnames->getPropertyValueEnum(prop, v);
        if(value==UCHAR_INVALID_CODE && prop==UCHAR_CANONICAL_COMBINING_CLASS) {
            // TODO: Make getPropertyValueEnum(UCHAR_CANONICAL_COMBINING_CLASS, v) work.
            char *end;
            unsigned long ccc=uprv_strtoul(v, &end, 10);
            if(v<end && *end==0 && ccc<=254) {
                value = static_cast<int32_t>(ccc);
            }
        }
        if(value==UCHAR_INVALID_CODE) {
            fprintf(stderr,
                    "error in preparsed UCD: '%s' is not a valid value on line %ld\n",
                    field, static_cast<long>(lineNumber));
            errorCode=U_PARSE_ERROR;
        } else {
            props.intProps[prop-UCHAR_INT_START]=value;
        }
    } else if(*v=='<') {
        // Do not parse default values like <code point>, just set null values.
        switch(prop) {
        case UCHAR_BIDI_MIRRORING_GLYPH:
            props.bmg=U_SENTINEL;
            break;
        case UCHAR_BIDI_PAIRED_BRACKET:
            props.bpb=U_SENTINEL;
            break;
        case UCHAR_SIMPLE_CASE_FOLDING:
            props.scf=U_SENTINEL;
            break;
        case UCHAR_SIMPLE_LOWERCASE_MAPPING:
            props.slc=U_SENTINEL;
            break;
        case UCHAR_SIMPLE_TITLECASE_MAPPING:
            props.stc=U_SENTINEL;
            break;
        case UCHAR_SIMPLE_UPPERCASE_MAPPING:
            props.suc=U_SENTINEL;
            break;
        case UCHAR_CASE_FOLDING:
            props.cf.remove();
            break;
        case UCHAR_LOWERCASE_MAPPING:
            props.lc.remove();
            break;
        case UCHAR_TITLECASE_MAPPING:
            props.tc.remove();
            break;
        case UCHAR_UPPERCASE_MAPPING:
            props.uc.remove();
            break;
        case UCHAR_SCRIPT_EXTENSIONS:
            props.scx.clear();
            break;
        default:
            fprintf(stderr,
                    "error in preparsed UCD: '%s' is not a valid default value on line %ld\n",
                    field, static_cast<long>(lineNumber));
            errorCode=U_PARSE_ERROR;
        }
    } else {
        char c;
        switch(prop) {
        case UCHAR_NUMERIC_VALUE:
            props.numericValue=v;
            c=*v;
            if('0'<=c && c<='9' && v[1]==0) {
                props.digitValue=c-'0';
            } else {
                props.digitValue=-1;
            }
            break;
        case UCHAR_NAME:
            props.name=v;
            break;
        case UCHAR_AGE:
            u_versionFromString(props.age, v);  // Writes 0.0.0.0 if v is not numeric.
            break;
        case UCHAR_BIDI_MIRRORING_GLYPH:
            props.bmg=parseCodePoint(v, errorCode);
            break;
        case UCHAR_BIDI_PAIRED_BRACKET:
            props.bpb=parseCodePoint(v, errorCode);
            break;
        case UCHAR_SIMPLE_CASE_FOLDING:
            props.scf=parseCodePoint(v, errorCode);
            break;
        case UCHAR_SIMPLE_LOWERCASE_MAPPING:
            props.slc=parseCodePoint(v, errorCode);
            break;
        case UCHAR_SIMPLE_TITLECASE_MAPPING:
            props.stc=parseCodePoint(v, errorCode);
            break;
        case UCHAR_SIMPLE_UPPERCASE_MAPPING:
            props.suc=parseCodePoint(v, errorCode);
            break;
        case UCHAR_CASE_FOLDING:
            parseString(v, props.cf, errorCode);
            break;
        case UCHAR_LOWERCASE_MAPPING:
            parseString(v, props.lc, errorCode);
            break;
        case UCHAR_TITLECASE_MAPPING:
            parseString(v, props.tc, errorCode);
            break;
        case UCHAR_UPPERCASE_MAPPING:
            parseString(v, props.uc, errorCode);
            break;
        case PPUCD_NAME_ALIAS:
            props.nameAlias=v;
            break;
        case PPUCD_CONDITIONAL_CASE_MAPPINGS:
        case PPUCD_TURKIC_CASE_FOLDING:
            // No need to parse their values: They are hardcoded in the runtime library.
            break;
        case UCHAR_SCRIPT_EXTENSIONS:
            parseScriptExtensions(v, props.scx, errorCode);
            break;
        case UCHAR_IDENTIFIER_TYPE:
            parseIdentifierType(v, props.idType, errorCode);
            break;
        default:
            // Ignore unhandled properties.
            return true;
        }
    }
    if(U_SUCCESS(errorCode)) {
        newValues.add(static_cast<UChar32>(prop));
        return true;
    } else {
        return false;
    }
}

UBool
PreparsedUCD::getRangeForAlgNames(UChar32 &start, UChar32 &end, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return false; }
    if(lineType!=ALG_NAMES_RANGE_LINE) {
        errorCode=U_ILLEGAL_ARGUMENT_ERROR;
        return false;
    }
    firstField();
    const char *field=nextField();
    if(field==nullptr) {
        // No range field after the type.
        fprintf(stderr,
                "error in preparsed UCD: missing algnamesrange range field "
                "(no second field) on line %ld\n",
                static_cast<long>(lineNumber));
        errorCode=U_PARSE_ERROR;
        return false;
    }
    return parseCodePointRange(field, start, end, errorCode);
}

UChar32
PreparsedUCD::parseCodePoint(const char *s, UErrorCode &errorCode) {
    char *end;
    uint32_t value = static_cast<uint32_t>(uprv_strtoul(s, &end, 16));
    if(end<=s || *end!=0 || value>=0x110000) {
        fprintf(stderr,
                "error in preparsed UCD: '%s' is not a valid code point on line %ld\n",
                s, static_cast<long>(lineNumber));
        errorCode=U_PARSE_ERROR;
        return U_SENTINEL;
    }
    return static_cast<UChar32>(value);
}

UBool
PreparsedUCD::parseCodePointRange(const char *s, UChar32 &start, UChar32 &end, UErrorCode &errorCode) {
    uint32_t st, e;
    u_parseCodePointRange(s, &st, &e, &errorCode);
    if(U_FAILURE(errorCode)) {
        fprintf(stderr,
                "error in preparsed UCD: '%s' is not a valid code point range on line %ld\n",
                s, static_cast<long>(lineNumber));
        return false;
    }
    start = static_cast<UChar32>(st);
    end = static_cast<UChar32>(e);
    return true;
}

void
PreparsedUCD::parseString(const char *s, UnicodeString &uni, UErrorCode &errorCode) {
    char16_t *buffer=toUCharPtr(uni.getBuffer(-1));
    int32_t length=u_parseString(s, buffer, uni.getCapacity(), nullptr, &errorCode);
    if(errorCode==U_BUFFER_OVERFLOW_ERROR) {
        errorCode=U_ZERO_ERROR;
        uni.releaseBuffer(0);
        buffer=toUCharPtr(uni.getBuffer(length));
        length=u_parseString(s, buffer, uni.getCapacity(), nullptr, &errorCode);
    }
    uni.releaseBuffer(length);
    if(U_FAILURE(errorCode)) {
        fprintf(stderr,
                "error in preparsed UCD: '%s' is not a valid Unicode string on line %ld\n",
                s, static_cast<long>(lineNumber));
    }
}

void
PreparsedUCD::parseScriptExtensions(const char *s, UnicodeSet &scx, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    scx.clear();
    CharString scString;
    for(;;) {
        const char *scs;
        const char *scLimit=strchr(s, ' ');
        if(scLimit!=nullptr) {
            scs = scString.clear().append(s, static_cast<int32_t>(scLimit - s), errorCode).data();
            if(U_FAILURE(errorCode)) { return; }
        } else {
            scs=s;
        }
        int32_t script=pnames->getPropertyValueEnum(UCHAR_SCRIPT, scs);
        if(script==UCHAR_INVALID_CODE) {
            fprintf(stderr,
                    "error in preparsed UCD: '%s' is not a valid script code on line %ld\n",
                    scs, static_cast<long>(lineNumber));
            errorCode=U_PARSE_ERROR;
            return;
        } else if(scx.contains(script)) {
            fprintf(stderr,
                    "error in preparsed UCD: scx has duplicate '%s' codes on line %ld\n",
                    scs, static_cast<long>(lineNumber));
            errorCode=U_PARSE_ERROR;
            return;
        } else {
            scx.add(script);
        }
        if(scLimit!=nullptr) {
            s=scLimit+1;
        } else {
            break;
        }
    }
    if(scx.isEmpty()) {
        fprintf(stderr, "error in preparsed UCD: empty scx= on line %ld\n", static_cast<long>(lineNumber));
        errorCode=U_PARSE_ERROR;
    }
}

void
PreparsedUCD::parseIdentifierType(const char *s, UnicodeSet &idType, UErrorCode &errorCode) {
    if(U_FAILURE(errorCode)) { return; }
    idType.clear();
    CharString typeString;
    for(;;) {
        const char *typeChars;
        const char *limit=strchr(s, ' ');
        if(limit!=nullptr) {
            typeChars = typeString.clear().append(s, static_cast<int32_t>(limit - s), errorCode).data();
            if(U_FAILURE(errorCode)) { return; }
        } else {
            typeChars=s;
        }
        int32_t type=pnames->getPropertyValueEnum(UCHAR_IDENTIFIER_TYPE, typeChars);
        if(type==UCHAR_INVALID_CODE) {
            fprintf(stderr,
                    "error in preparsed UCD: '%s' is not a valid Identifier_Type on line %ld\n",
                    typeChars, static_cast<long>(lineNumber));
            errorCode=U_PARSE_ERROR;
            return;
        } else if(idType.contains(type)) {
            fprintf(stderr,
                    "error in preparsed UCD: Identifier_Type has duplicate '%s' values on line %ld\n",
                    typeChars, static_cast<long>(lineNumber));
            errorCode=U_PARSE_ERROR;
            return;
        } else {
            idType.add(type);
        }
        if(limit!=nullptr) {
            s=limit+1;
        } else {
            break;
        }
    }
    if(idType.isEmpty()) {
        fprintf(stderr,
                "error in preparsed UCD: empty Identifier_Type= on line %ld\n",
                static_cast<long>(lineNumber));
        errorCode=U_PARSE_ERROR;
    }
}

U_NAMESPACE_END
