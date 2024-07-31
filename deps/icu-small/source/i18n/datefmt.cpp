// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 *******************************************************************************
 * Copyright (C) 1997-2015, International Business Machines Corporation and    *
 * others. All Rights Reserved.                                                *
 *******************************************************************************
 *
 * File DATEFMT.CPP
 *
 * Modification History:
 *
 *   Date        Name        Description
 *   02/19/97    aliu        Converted from java.
 *   03/31/97    aliu        Modified extensively to work with 50 locales.
 *   04/01/97    aliu        Added support for centuries.
 *   08/12/97    aliu        Fixed operator== to use Calendar::equivalentTo.
 *   07/20/98    stephen     Changed ParsePosition initialization
 ********************************************************************************
 */

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/ures.h"
#include "unicode/datefmt.h"
#include "unicode/smpdtfmt.h"
#include "unicode/dtptngen.h"
#include "unicode/udisplaycontext.h"
#include "unicode/gregocal.h"
#include "reldtfmt.h"
#include "sharedobject.h"
#include "unifiedcache.h"
#include "uarrsort.h"

#include "cstring.h"
#include "windtfmt.h"

#if defined( U_DEBUG_CALSVC ) || defined (U_DEBUG_CAL)
#include <stdio.h>
#endif

// *****************************************************************************
// class DateFormat
// *****************************************************************************

U_NAMESPACE_BEGIN

class DateFmtBestPattern : public SharedObject {
public:
    UnicodeString fPattern;

    DateFmtBestPattern(const UnicodeString &pattern)
            : fPattern(pattern) { }
    ~DateFmtBestPattern();
};

DateFmtBestPattern::~DateFmtBestPattern() {
}

template<> 
const DateFmtBestPattern *LocaleCacheKey<DateFmtBestPattern>::createObject(
        const void * /*creationContext*/, UErrorCode &status) const {
    status = U_UNSUPPORTED_ERROR;
    return nullptr;
}

class DateFmtBestPatternKey : public LocaleCacheKey<DateFmtBestPattern> { 
private:
    UnicodeString fSkeleton;
protected:
    virtual bool equals(const CacheKeyBase &other) const override {
       if (!LocaleCacheKey<DateFmtBestPattern>::equals(other)) {
           return false;
       }
       // We know that this and other are of same class if we get this far.
       return operator==(static_cast<const DateFmtBestPatternKey &>(other));
    }
public:
    DateFmtBestPatternKey(
        const Locale &loc,
        const UnicodeString &skeleton,
        UErrorCode &status)
            : LocaleCacheKey<DateFmtBestPattern>(loc),
              fSkeleton(DateTimePatternGenerator::staticGetSkeleton(skeleton, status)) { }
    DateFmtBestPatternKey(const DateFmtBestPatternKey &other) :
            LocaleCacheKey<DateFmtBestPattern>(other),
            fSkeleton(other.fSkeleton) { }
    virtual ~DateFmtBestPatternKey();
    virtual int32_t hashCode() const override {
        return (int32_t)(37u * (uint32_t)LocaleCacheKey<DateFmtBestPattern>::hashCode() + (uint32_t)fSkeleton.hashCode());
    }
    inline bool operator==(const DateFmtBestPatternKey &other) const {
        return fSkeleton == other.fSkeleton;
    }
    virtual CacheKeyBase *clone() const override {
        return new DateFmtBestPatternKey(*this);
    }
    virtual const DateFmtBestPattern *createObject(
            const void * /*unused*/, UErrorCode &status) const override {
        LocalPointer<DateTimePatternGenerator> dtpg(
                    DateTimePatternGenerator::createInstance(fLoc, status));
        if (U_FAILURE(status)) {
            return nullptr;
        }
  
        LocalPointer<DateFmtBestPattern> pattern(
                new DateFmtBestPattern(
                        dtpg->getBestPattern(fSkeleton, status)),
                status);
        if (U_FAILURE(status)) {
            return nullptr;
        }
        DateFmtBestPattern *result = pattern.orphan();
        result->addRef();
        return result;
    }
};

DateFmtBestPatternKey::~DateFmtBestPatternKey() { }


DateFormat::DateFormat()
:   fCalendar(nullptr),
    fNumberFormat(nullptr),
    fCapitalizationContext(UDISPCTX_CAPITALIZATION_NONE)
{
}

//----------------------------------------------------------------------

DateFormat::DateFormat(const DateFormat& other)
:   Format(other),
    fCalendar(nullptr),
    fNumberFormat(nullptr),
    fCapitalizationContext(UDISPCTX_CAPITALIZATION_NONE)
{
    *this = other;
}

//----------------------------------------------------------------------

DateFormat& DateFormat::operator=(const DateFormat& other)
{
    if (this != &other)
    {
        delete fCalendar;
        delete fNumberFormat;
        if(other.fCalendar) {
          fCalendar = other.fCalendar->clone();
        } else {
          fCalendar = nullptr;
        }
        if(other.fNumberFormat) {
          fNumberFormat = other.fNumberFormat->clone();
        } else {
          fNumberFormat = nullptr;
        }
        fBoolFlags = other.fBoolFlags;
        fCapitalizationContext = other.fCapitalizationContext;
    }
    return *this;
}

//----------------------------------------------------------------------

DateFormat::~DateFormat()
{
    delete fCalendar;
    delete fNumberFormat;
}

//----------------------------------------------------------------------

bool
DateFormat::operator==(const Format& other) const
{
    if (this == &other) {
        return true;
    }
    if (!(Format::operator==(other))) {
        return false;
    }
    // Format::operator== guarantees that this cast is safe
    DateFormat* fmt = (DateFormat*)&other;
    return fCalendar&&(fCalendar->isEquivalentTo(*fmt->fCalendar)) &&
         (fNumberFormat && *fNumberFormat == *fmt->fNumberFormat) &&
         (fCapitalizationContext == fmt->fCapitalizationContext);
}

//----------------------------------------------------------------------

UnicodeString&
DateFormat::format(const Formattable& obj,
                   UnicodeString& appendTo,
                   FieldPosition& fieldPosition,
                   UErrorCode& status) const
{
    if (U_FAILURE(status)) return appendTo;

    // if the type of the Formattable is double or long, treat it as if it were a Date
    UDate date = 0;
    switch (obj.getType())
    {
    case Formattable::kDate:
        date = obj.getDate();
        break;
    case Formattable::kDouble:
        date = (UDate)obj.getDouble();
        break;
    case Formattable::kLong:
        date = (UDate)obj.getLong();
        break;
    default:
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return appendTo;
    }

    // Is this right?
    //if (fieldPosition.getBeginIndex() == fieldPosition.getEndIndex())
    //  status = U_ILLEGAL_ARGUMENT_ERROR;

    return format(date, appendTo, fieldPosition);
}

//----------------------------------------------------------------------

UnicodeString&
DateFormat::format(const Formattable& obj,
                   UnicodeString& appendTo,
                   FieldPositionIterator* posIter,
                   UErrorCode& status) const
{
    if (U_FAILURE(status)) return appendTo;

    // if the type of the Formattable is double or long, treat it as if it were a Date
    UDate date = 0;
    switch (obj.getType())
    {
    case Formattable::kDate:
        date = obj.getDate();
        break;
    case Formattable::kDouble:
        date = (UDate)obj.getDouble();
        break;
    case Formattable::kLong:
        date = (UDate)obj.getLong();
        break;
    default:
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return appendTo;
    }

    // Is this right?
    //if (fieldPosition.getBeginIndex() == fieldPosition.getEndIndex())
    //  status = U_ILLEGAL_ARGUMENT_ERROR;

    return format(date, appendTo, posIter, status);
}

//----------------------------------------------------------------------

// Default implementation for backwards compatibility, subclasses should implement.
UnicodeString&
DateFormat::format(Calendar& /* unused cal */,
                   UnicodeString& appendTo,
                   FieldPositionIterator* /* unused posIter */,
                   UErrorCode& status) const {
    if (U_SUCCESS(status)) {
        status = U_UNSUPPORTED_ERROR;
    }
    return appendTo;
}

//----------------------------------------------------------------------

UnicodeString&
DateFormat::format(UDate date, UnicodeString& appendTo, FieldPosition& fieldPosition) const {
    if (fCalendar != nullptr) {
        UErrorCode ec = U_ZERO_ERROR;
        const auto* calType = fCalendar->getType();
        // Avoid a heap allocation and corresponding free for the common case
        if (uprv_strcmp(calType, "gregorian") == 0) {
            GregorianCalendar cal(*static_cast<GregorianCalendar*>(fCalendar));
            cal.setTime(date, ec);
            if (U_SUCCESS(ec)) {
                format(cal, appendTo, fieldPosition);
            }
        } else {
            // Use a clone of our calendar instance
            Calendar *calClone = fCalendar->clone();
            if (calClone != nullptr) {
                calClone->setTime(date, ec);
                if (U_SUCCESS(ec)) {
                    format(*calClone, appendTo, fieldPosition);
                }
                delete calClone;
            }
        }
    }
    return appendTo;
}

//----------------------------------------------------------------------

UnicodeString&
DateFormat::format(UDate date, UnicodeString& appendTo, FieldPositionIterator* posIter,
                   UErrorCode& status) const {
    if (fCalendar != nullptr) {
        UErrorCode ec = U_ZERO_ERROR;
        const auto* calType = fCalendar->getType();
        // Avoid a heap allocation and corresponding free for the common case
        if (uprv_strcmp(calType, "gregorian") == 0) {
            GregorianCalendar cal(*static_cast<GregorianCalendar*>(fCalendar));
            cal.setTime(date, ec);
            if (U_SUCCESS(ec)) {
                format(cal, appendTo, posIter, status);
            }
        } else {
            Calendar* calClone = fCalendar->clone();
            if (calClone != nullptr) {
                calClone->setTime(date, status);
                if (U_SUCCESS(status)) {
                    format(*calClone, appendTo, posIter, status);
                }
                delete calClone;
            }
        }
    }
    return appendTo;
}

//----------------------------------------------------------------------

UnicodeString&
DateFormat::format(UDate date, UnicodeString& appendTo) const
{
    // Note that any error information is just lost.  That's okay
    // for this convenience method.
    FieldPosition fpos(FieldPosition::DONT_CARE);
    return format(date, appendTo, fpos);
}

//----------------------------------------------------------------------

UDate
DateFormat::parse(const UnicodeString& text,
                  ParsePosition& pos) const
{
    UDate d = 0; // Error return UDate is 0 (the epoch)
    if (fCalendar != nullptr) {
        Calendar* calClone = fCalendar->clone();
        if (calClone != nullptr) {
            int32_t start = pos.getIndex();
            calClone->clear();
            parse(text, *calClone, pos);
            if (pos.getIndex() != start) {
                UErrorCode ec = U_ZERO_ERROR;
                d = calClone->getTime(ec);
                if (U_FAILURE(ec)) {
                    // We arrive here if fCalendar => calClone is non-lenient and
                    // there is an out-of-range field.  We don't know which field
                    // was illegal so we set the error index to the start.
                    pos.setIndex(start);
                    pos.setErrorIndex(start);
                    d = 0;
                }
            }
            delete calClone;
        }
    }
    return d;
}

//----------------------------------------------------------------------

UDate
DateFormat::parse(const UnicodeString& text,
                  UErrorCode& status) const
{
    if (U_FAILURE(status)) return 0;

    ParsePosition pos(0);
    UDate result = parse(text, pos);
    if (pos.getIndex() == 0) {
#if defined (U_DEBUG_CAL)
      fprintf(stderr, "%s:%d - - failed to parse  - err index %d\n"
              , __FILE__, __LINE__, pos.getErrorIndex() );
#endif
      status = U_ILLEGAL_ARGUMENT_ERROR;
    }
    return result;
}

//----------------------------------------------------------------------

void
DateFormat::parseObject(const UnicodeString& source,
                        Formattable& result,
                        ParsePosition& pos) const
{
    result.setDate(parse(source, pos));
}

//----------------------------------------------------------------------

DateFormat* U_EXPORT2
DateFormat::createTimeInstance(DateFormat::EStyle style,
                               const Locale& aLocale)
{
    return createDateTimeInstance(kNone, style, aLocale);
}

//----------------------------------------------------------------------

DateFormat* U_EXPORT2
DateFormat::createDateInstance(DateFormat::EStyle style,
                               const Locale& aLocale)
{
    return createDateTimeInstance(style, kNone, aLocale);
}

//----------------------------------------------------------------------

DateFormat* U_EXPORT2
DateFormat::createDateTimeInstance(EStyle dateStyle,
                                   EStyle timeStyle,
                                   const Locale& aLocale)
{
   if(dateStyle != kNone)
   {
       dateStyle = (EStyle) (dateStyle + kDateOffset);
   }
   return create(timeStyle, dateStyle, aLocale);
}

//----------------------------------------------------------------------

DateFormat* U_EXPORT2
DateFormat::createInstance()
{
    return createDateTimeInstance(kShort, kShort, Locale::getDefault());
}

//----------------------------------------------------------------------

UnicodeString U_EXPORT2
DateFormat::getBestPattern(
        const Locale &locale,
        const UnicodeString &skeleton,
        UErrorCode &status) {
    UnifiedCache *cache = UnifiedCache::getInstance(status);
    if (U_FAILURE(status)) {
        return {};
    }
    DateFmtBestPatternKey key(locale, skeleton, status);
    const DateFmtBestPattern *patternPtr = nullptr;
    cache->get(key, patternPtr, status);
    if (U_FAILURE(status)) {
        return {};
    }
    UnicodeString result(patternPtr->fPattern);
    patternPtr->removeRef();
    return result;
}

DateFormat* U_EXPORT2
DateFormat::createInstanceForSkeleton(
        Calendar *calendarToAdopt,
        const UnicodeString& skeleton,
        const Locale &locale,
        UErrorCode &status) {
    LocalPointer<Calendar> calendar(calendarToAdopt);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    if (calendar.isNull()) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return nullptr;
    }
    Locale localeWithCalendar = locale;
    localeWithCalendar.setKeywordValue("calendar", calendar->getType(), status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    DateFormat *result = createInstanceForSkeleton(skeleton, localeWithCalendar, status);
    if (U_FAILURE(status)) {
        return nullptr;
    }
    result->adoptCalendar(calendar.orphan());
    return result;
}

DateFormat* U_EXPORT2
DateFormat::createInstanceForSkeleton(
        const UnicodeString& skeleton,
        const Locale &locale,
        UErrorCode &status) {
    if (U_FAILURE(status)) {
        return nullptr;
    }
    LocalPointer<DateFormat> df(
        new SimpleDateFormat(
            getBestPattern(locale, skeleton, status),
            locale, status),
        status);
    return U_SUCCESS(status) ? df.orphan() : nullptr;
}

DateFormat* U_EXPORT2
DateFormat::createInstanceForSkeleton(
        const UnicodeString& skeleton,
        UErrorCode &status) {
    return createInstanceForSkeleton(
            skeleton, Locale::getDefault(), status);
}

//----------------------------------------------------------------------

DateFormat* U_EXPORT2
DateFormat::create(EStyle timeStyle, EStyle dateStyle, const Locale& locale)
{
    UErrorCode status = U_ZERO_ERROR;
#if U_PLATFORM_USES_ONLY_WIN32_API
    char buffer[8];
    int32_t count = locale.getKeywordValue("compat", buffer, sizeof(buffer), status);

    // if the locale has "@compat=host", create a host-specific DateFormat...
    if (count > 0 && uprv_strcmp(buffer, "host") == 0) {
        Win32DateFormat *f = new Win32DateFormat(timeStyle, dateStyle, locale, status);

        if (U_SUCCESS(status)) {
            return f;
        }

        delete f;
    }
#endif

    // is it relative?
    if(/*((timeStyle!=UDAT_NONE)&&(timeStyle & UDAT_RELATIVE)) || */((dateStyle!=kNone)&&((dateStyle-kDateOffset) & UDAT_RELATIVE))) {
        RelativeDateFormat *r = new RelativeDateFormat((UDateFormatStyle)timeStyle, (UDateFormatStyle)(dateStyle-kDateOffset), locale, status);
        if(U_SUCCESS(status)) return r;
        delete r;
        status = U_ZERO_ERROR;
    }

    // Try to create a SimpleDateFormat of the desired style.
    SimpleDateFormat *f = new SimpleDateFormat(timeStyle, dateStyle, locale, status);
    if (U_SUCCESS(status)) return f;
    delete f;

    // If that fails, try to create a format using the default pattern and
    // the DateFormatSymbols for this locale.
    status = U_ZERO_ERROR;
    f = new SimpleDateFormat(locale, status);
    if (U_SUCCESS(status)) return f;
    delete f;

    // This should never really happen, because the preceding constructor
    // should always succeed.  If the resource data is unavailable, a last
    // resort object should be returned.
    return nullptr;
}

//----------------------------------------------------------------------

const Locale* U_EXPORT2
DateFormat::getAvailableLocales(int32_t& count)
{
    // Get the list of installed locales.
    // Even if root has the correct date format for this locale,
    // it's still a valid locale (we don't worry about data fallbacks).
    return Locale::getAvailableLocales(count);
}

//----------------------------------------------------------------------

void
DateFormat::adoptCalendar(Calendar* newCalendar)
{
    delete fCalendar;
    fCalendar = newCalendar;
}

//----------------------------------------------------------------------
void
DateFormat::setCalendar(const Calendar& newCalendar)
{
    Calendar* newCalClone = newCalendar.clone();
    if (newCalClone != nullptr) {
        adoptCalendar(newCalClone);
    }
}

//----------------------------------------------------------------------

const Calendar*
DateFormat::getCalendar() const
{
    return fCalendar;
}

//----------------------------------------------------------------------

void
DateFormat::adoptNumberFormat(NumberFormat* newNumberFormat)
{
    delete fNumberFormat;
    fNumberFormat = newNumberFormat;
    newNumberFormat->setParseIntegerOnly(true);
    newNumberFormat->setGroupingUsed(false);
}
//----------------------------------------------------------------------

void
DateFormat::setNumberFormat(const NumberFormat& newNumberFormat)
{
    NumberFormat* newNumFmtClone = newNumberFormat.clone();
    if (newNumFmtClone != nullptr) {
        adoptNumberFormat(newNumFmtClone);
    }
}

//----------------------------------------------------------------------

const NumberFormat*
DateFormat::getNumberFormat() const
{
    return fNumberFormat;
}

//----------------------------------------------------------------------

void
DateFormat::adoptTimeZone(TimeZone* zone)
{
    if (fCalendar != nullptr) {
        fCalendar->adoptTimeZone(zone);
    }
}
//----------------------------------------------------------------------

void
DateFormat::setTimeZone(const TimeZone& zone)
{
    if (fCalendar != nullptr) {
        fCalendar->setTimeZone(zone);
    }
}

//----------------------------------------------------------------------

const TimeZone&
DateFormat::getTimeZone() const
{
    if (fCalendar != nullptr) {
        return fCalendar->getTimeZone();
    }
    // If calendar doesn't exists, create default timezone.
    // fCalendar is rarely null
    return *(TimeZone::createDefault());
}

//----------------------------------------------------------------------

void
DateFormat::setLenient(UBool lenient)
{
    if (fCalendar != nullptr) {
        fCalendar->setLenient(lenient);
    }
    UErrorCode status = U_ZERO_ERROR;
    setBooleanAttribute(UDAT_PARSE_ALLOW_WHITESPACE, lenient, status);
    setBooleanAttribute(UDAT_PARSE_ALLOW_NUMERIC, lenient, status);
}

//----------------------------------------------------------------------

UBool
DateFormat::isLenient() const
{
    UBool lenient = true;
    if (fCalendar != nullptr) {
        lenient = fCalendar->isLenient();
    }
    UErrorCode status = U_ZERO_ERROR;
    return lenient
        && getBooleanAttribute(UDAT_PARSE_ALLOW_WHITESPACE, status)
        && getBooleanAttribute(UDAT_PARSE_ALLOW_NUMERIC, status);
}

void
DateFormat::setCalendarLenient(UBool lenient)
{
    if (fCalendar != nullptr) {
        fCalendar->setLenient(lenient);
    }
}

//----------------------------------------------------------------------

UBool
DateFormat::isCalendarLenient() const
{
    if (fCalendar != nullptr) {
        return fCalendar->isLenient();
    }
    // fCalendar is rarely null
    return false;
}


//----------------------------------------------------------------------


void DateFormat::setContext(UDisplayContext value, UErrorCode& status)
{
    if (U_FAILURE(status))
        return;
    if ( (UDisplayContextType)((uint32_t)value >> 8) == UDISPCTX_TYPE_CAPITALIZATION ) {
        fCapitalizationContext = value;
    } else {
        status = U_ILLEGAL_ARGUMENT_ERROR;
   }
}


//----------------------------------------------------------------------


UDisplayContext DateFormat::getContext(UDisplayContextType type, UErrorCode& status) const
{
    if (U_FAILURE(status))
        return (UDisplayContext)0;
    if (type != UDISPCTX_TYPE_CAPITALIZATION) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return (UDisplayContext)0;
    }
    return fCapitalizationContext;
}


//----------------------------------------------------------------------


DateFormat& 
DateFormat::setBooleanAttribute(UDateFormatBooleanAttribute attr,
    									UBool newValue,
    									UErrorCode &status) {
    if(!fBoolFlags.isValidValue(newValue)) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
    } else {
        fBoolFlags.set(attr, newValue);
    }

    return *this;
}

//----------------------------------------------------------------------

UBool 
DateFormat::getBooleanAttribute(UDateFormatBooleanAttribute attr, UErrorCode &/*status*/) const {

    return static_cast<UBool>(fBoolFlags.get(attr));
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
