// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2007-2016, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
* File DTPTNGEN.H
*
*******************************************************************************
*/

#ifndef __DTPTNGEN_IMPL_H__
#define __DTPTNGEN_IMPL_H__

#include "unicode/udatpg.h"

#include "unicode/strenum.h"
#include "unicode/unistr.h"
#include "uvector.h"

// TODO(claireho): Split off Builder class.
// TODO(claireho): If splitting off Builder class: As subclass or independent?

#define MAX_PATTERN_ENTRIES 52
#define MAX_CLDR_FIELD_LEN  60
#define MAX_DT_TOKEN        50
#define MAX_RESOURCE_FIELD  12
#define MAX_AVAILABLE_FORMATS  12
#define NONE          0
#define EXTRA_FIELD   0x10000
#define MISSING_FIELD  0x1000
#define MAX_STRING_ENUMERATION  200
#define SINGLE_QUOTE      ((char16_t)0x0027)
#define FORWARDSLASH      ((char16_t)0x002F)
#define BACKSLASH         ((char16_t)0x005C)
#define SPACE             ((char16_t)0x0020)
#define QUOTATION_MARK    ((char16_t)0x0022)
#define ASTERISK          ((char16_t)0x002A)
#define PLUSSITN          ((char16_t)0x002B)
#define COMMA             ((char16_t)0x002C)
#define HYPHEN            ((char16_t)0x002D)
#define DOT               ((char16_t)0x002E)
#define COLON             ((char16_t)0x003A)
#define CAP_A             ((char16_t)0x0041)
#define CAP_B             ((char16_t)0x0042)
#define CAP_C             ((char16_t)0x0043)
#define CAP_D             ((char16_t)0x0044)
#define CAP_E             ((char16_t)0x0045)
#define CAP_F             ((char16_t)0x0046)
#define CAP_G             ((char16_t)0x0047)
#define CAP_H             ((char16_t)0x0048)
#define CAP_J             ((char16_t)0x004A)
#define CAP_K             ((char16_t)0x004B)
#define CAP_L             ((char16_t)0x004C)
#define CAP_M             ((char16_t)0x004D)
#define CAP_O             ((char16_t)0x004F)
#define CAP_Q             ((char16_t)0x0051)
#define CAP_S             ((char16_t)0x0053)
#define CAP_T             ((char16_t)0x0054)
#define CAP_U             ((char16_t)0x0055)
#define CAP_V             ((char16_t)0x0056)
#define CAP_W             ((char16_t)0x0057)
#define CAP_X             ((char16_t)0x0058)
#define CAP_Y             ((char16_t)0x0059)
#define CAP_Z             ((char16_t)0x005A)
#define LOWLINE           ((char16_t)0x005F)
#define LOW_A             ((char16_t)0x0061)
#define LOW_B             ((char16_t)0x0062)
#define LOW_C             ((char16_t)0x0063)
#define LOW_D             ((char16_t)0x0064)
#define LOW_E             ((char16_t)0x0065)
#define LOW_F             ((char16_t)0x0066)
#define LOW_G             ((char16_t)0x0067)
#define LOW_H             ((char16_t)0x0068)
#define LOW_I             ((char16_t)0x0069)
#define LOW_J             ((char16_t)0x006A)
#define LOW_K             ((char16_t)0x006B)
#define LOW_L             ((char16_t)0x006C)
#define LOW_M             ((char16_t)0x006D)
#define LOW_N             ((char16_t)0x006E)
#define LOW_O             ((char16_t)0x006F)
#define LOW_P             ((char16_t)0x0070)
#define LOW_Q             ((char16_t)0x0071)
#define LOW_R             ((char16_t)0x0072)
#define LOW_S             ((char16_t)0x0073)
#define LOW_T             ((char16_t)0x0074)
#define LOW_U             ((char16_t)0x0075)
#define LOW_V             ((char16_t)0x0076)
#define LOW_W             ((char16_t)0x0077)
#define LOW_X             ((char16_t)0x0078)
#define LOW_Y             ((char16_t)0x0079)
#define LOW_Z             ((char16_t)0x007A)
#define DT_NARROW         -0x101
#define DT_SHORTER        -0x102
#define DT_SHORT          -0x103
#define DT_LONG           -0x104
#define DT_NUMERIC         0x100
#define DT_DELTA           0x10

U_NAMESPACE_BEGIN

const int32_t UDATPG_FRACTIONAL_MASK = 1<<UDATPG_FRACTIONAL_SECOND_FIELD;
const int32_t UDATPG_SECOND_AND_FRACTIONAL_MASK = (1<<UDATPG_SECOND_FIELD) | (1<<UDATPG_FRACTIONAL_SECOND_FIELD);

typedef enum dtStrEnum {
    DT_BASESKELETON,
    DT_SKELETON,
    DT_PATTERN
}dtStrEnum;

typedef struct dtTypeElem {
    char16_t               patternChar;
    UDateTimePatternField  field;
    int16_t                type;
    int16_t                minLen;
    int16_t                weight;
} dtTypeElem;

// A compact storage mechanism for skeleton field strings.  Several dozen of these will be created
// for a typical DateTimePatternGenerator instance.
class SkeletonFields : public UMemory {
public:
    SkeletonFields();
    void clear();
    void copyFrom(const SkeletonFields& other);
    void clearField(int32_t field);
    char16_t getFieldChar(int32_t field) const;
    int32_t getFieldLength(int32_t field) const;
    void populate(int32_t field, const UnicodeString& value);
    void populate(int32_t field, char16_t repeatChar, int32_t repeatCount);
    UBool isFieldEmpty(int32_t field) const;
    UnicodeString& appendTo(UnicodeString& string) const;
    UnicodeString& appendFieldTo(int32_t field, UnicodeString& string) const;
    char16_t getFirstChar() const;
    inline bool operator==(const SkeletonFields& other) const;
    inline bool operator!=(const SkeletonFields& other) const;

private:
    int8_t chars[UDATPG_FIELD_COUNT];
    int8_t lengths[UDATPG_FIELD_COUNT];
};

inline bool SkeletonFields::operator==(const SkeletonFields& other) const {
    return (uprv_memcmp(chars, other.chars, sizeof(chars)) == 0
        && uprv_memcmp(lengths, other.lengths, sizeof(lengths)) == 0);
}

inline bool SkeletonFields::operator!=(const SkeletonFields& other) const {
    return (! operator==(other));
}

class PtnSkeleton : public UMemory {
public:
    int32_t type[UDATPG_FIELD_COUNT];
    SkeletonFields original;
    SkeletonFields baseOriginal;
    UBool addedDefaultDayPeriod;

    PtnSkeleton();
    PtnSkeleton(const PtnSkeleton& other);
    void copyFrom(const PtnSkeleton& other);
    void clear();
    UBool equals(const PtnSkeleton& other) const;
    UnicodeString getSkeleton() const;
    UnicodeString getBaseSkeleton() const;
    char16_t getFirstChar() const;

    // TODO: Why is this virtual, as well as the other destructors in this file? We don't want
    // vtables when we don't use class objects polymorphically.
    virtual ~PtnSkeleton();
};

class PtnElem : public UMemory {
public:
    UnicodeString basePattern;
    LocalPointer<PtnSkeleton> skeleton;
    UnicodeString pattern;
    UBool         skeletonWasSpecified; // if specified in availableFormats, not derived
    LocalPointer<PtnElem> next;

    PtnElem(const UnicodeString &basePattern, const UnicodeString &pattern);
    virtual ~PtnElem();
};

class FormatParser : public UMemory {
public:
    UnicodeString items[MAX_DT_TOKEN];
    int32_t itemNumber;

    FormatParser();
    virtual ~FormatParser();
    void set(const UnicodeString& patternString);
    void getQuoteLiteral(UnicodeString& quote, int32_t *itemIndex);
    UBool isPatternSeparator(const UnicodeString& field) const;
    static UBool isQuoteLiteral(const UnicodeString& s);
    static int32_t getCanonicalIndex(const UnicodeString& s) { return getCanonicalIndex(s, true); }
    static int32_t getCanonicalIndex(const UnicodeString& s, UBool strict);

private:
   typedef enum TokenStatus {
       START,
       ADD_TOKEN,
       SYNTAX_ERROR,
       DONE
   } TokenStatus;

   TokenStatus status;
   virtual TokenStatus setTokens(const UnicodeString& pattern, int32_t startPos, int32_t *len);
};

class DistanceInfo : public UMemory {
public:
    int32_t missingFieldMask;
    int32_t extraFieldMask;

    DistanceInfo() {}
    virtual ~DistanceInfo();
    void clear() { missingFieldMask = extraFieldMask = 0; }
    void setTo(const DistanceInfo& other);
    void addMissing(int32_t field) { missingFieldMask |= (1<<field); }
    void addExtra(int32_t field) { extraFieldMask |= (1<<field); }
};

class DateTimeMatcher: public UMemory {
public:
    PtnSkeleton skeleton;

    void getBasePattern(UnicodeString& basePattern);
    UnicodeString getPattern();
    void set(const UnicodeString& pattern, FormatParser* fp);
    void set(const UnicodeString& pattern, FormatParser* fp, PtnSkeleton& skeleton);
    void copyFrom(const PtnSkeleton& skeleton);
    void copyFrom();
    PtnSkeleton* getSkeletonPtr();
    UBool equals(const DateTimeMatcher* other) const;
    int32_t getDistance(const DateTimeMatcher& other, int32_t includeMask, DistanceInfo& distanceInfo) const;
    DateTimeMatcher();
    DateTimeMatcher(const DateTimeMatcher& other);
    DateTimeMatcher& operator=(const DateTimeMatcher& other);
    virtual ~DateTimeMatcher();
    int32_t getFieldMask() const;
};

class PatternMap : public UMemory {
public:
    PtnElem *boot[MAX_PATTERN_ENTRIES];
    PatternMap();
    virtual  ~PatternMap();
    void  add(const UnicodeString& basePattern, const PtnSkeleton& skeleton, const UnicodeString& value, UBool skeletonWasSpecified, UErrorCode& status);
    const UnicodeString* getPatternFromBasePattern(const UnicodeString& basePattern, UBool& skeletonWasSpecified) const;
    const UnicodeString* getPatternFromSkeleton(const PtnSkeleton& skeleton, const PtnSkeleton** specifiedSkeletonPtr = nullptr) const;
    void copyFrom(const PatternMap& other, UErrorCode& status);
    PtnElem* getHeader(char16_t baseChar) const;
    UBool equals(const PatternMap& other) const;
private:
    UBool isDupAllowed;
    PtnElem*  getDuplicateElem(const UnicodeString& basePattern, const PtnSkeleton& skeleton, PtnElem *baseElem);
}; // end  PatternMap

class PatternMapIterator : public UMemory {
public:
    PatternMapIterator(UErrorCode &status);
    virtual ~PatternMapIterator();
    void set(PatternMap& patternMap);
    PtnSkeleton* getSkeleton() const;
    UBool hasNext() const;
    DateTimeMatcher& next();
private:
    int32_t bootIndex;
    PtnElem *nodePtr;
    LocalPointer<DateTimeMatcher> matcher;
    PatternMap *patternMap;
};

class DTSkeletonEnumeration : public StringEnumeration {
public:
    DTSkeletonEnumeration(PatternMap& patternMap, dtStrEnum type, UErrorCode& status);
    virtual ~DTSkeletonEnumeration();
    static UClassID U_EXPORT2 getStaticClassID();
    virtual UClassID getDynamicClassID() const override;
    virtual const UnicodeString* snext(UErrorCode& status) override;
    virtual void reset(UErrorCode& status) override;
    virtual int32_t count(UErrorCode& status) const override;
private:
    int32_t pos;
    UBool isCanonicalItem(const UnicodeString& item);
    LocalPointer<UVector> fSkeletons;
};

class DTRedundantEnumeration : public StringEnumeration {
public:
    DTRedundantEnumeration();
    virtual ~DTRedundantEnumeration();
    static UClassID U_EXPORT2 getStaticClassID();
    virtual UClassID getDynamicClassID() const override;
    virtual const UnicodeString* snext(UErrorCode& status) override;
    virtual void reset(UErrorCode& status) override;
    virtual int32_t count(UErrorCode& status) const override;
    void add(const UnicodeString &pattern, UErrorCode& status);
private:
    int32_t pos;
    UBool isCanonicalItem(const UnicodeString& item) const;
    LocalPointer<UVector> fPatterns;
};

U_NAMESPACE_END

#endif
