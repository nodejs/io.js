// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*******************************************************************************
* Copyright (C) 2008-2016, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
* File DTITVFMT.CPP
*
*******************************************************************************
*/

#include "utypeinfo.h"  // for 'typeid' to work

#include "unicode/dtitvfmt.h"

#if !UCONFIG_NO_FORMATTING

//TODO: put in compilation
//#define DTITVFMT_DEBUG 1

#include "unicode/calendar.h"
#include "unicode/dtptngen.h"
#include "unicode/dtitvinf.h"
#include "unicode/simpleformatter.h"
#include "unicode/udisplaycontext.h"
#include "cmemory.h"
#include "cstring.h"
#include "dtitv_impl.h"
#include "mutex.h"
#include "uresimp.h"
#include "formattedval_impl.h"

#ifdef DTITVFMT_DEBUG
#include <iostream>
#endif

U_NAMESPACE_BEGIN



#ifdef DTITVFMT_DEBUG
#define PRINTMESG(msg) { std::cout << "(" << __FILE__ << ":" << __LINE__ << ") " << msg << "\n"; }
#endif


static const char16_t gDateFormatSkeleton[][11] = {
//yMMMMEEEEd
{LOW_Y, CAP_M, CAP_M, CAP_M, CAP_M, CAP_E, CAP_E, CAP_E, CAP_E, LOW_D, 0},
//yMMMMd
{LOW_Y, CAP_M, CAP_M, CAP_M, CAP_M, LOW_D, 0},
//yMMMd
{LOW_Y, CAP_M, CAP_M, CAP_M, LOW_D, 0},
//yMd
{LOW_Y, CAP_M, LOW_D, 0} };


static const char gCalendarTag[] = "calendar";
static const char gGregorianTag[] = "gregorian";
static const char gDateTimePatternsTag[] = "DateTimePatterns";


// latestFirst:
static const char16_t gLaterFirstPrefix[] = {LOW_L, LOW_A, LOW_T, LOW_E, LOW_S,LOW_T, CAP_F, LOW_I, LOW_R, LOW_S, LOW_T, COLON};

// earliestFirst:
static const char16_t gEarlierFirstPrefix[] = {LOW_E, LOW_A, LOW_R, LOW_L, LOW_I, LOW_E, LOW_S, LOW_T, CAP_F, LOW_I, LOW_R, LOW_S, LOW_T, COLON};


class FormattedDateIntervalData : public FormattedValueFieldPositionIteratorImpl {
public:
    FormattedDateIntervalData(UErrorCode& status) : FormattedValueFieldPositionIteratorImpl(5, status) {}
    virtual ~FormattedDateIntervalData();
};

FormattedDateIntervalData::~FormattedDateIntervalData() = default;

UPRV_FORMATTED_VALUE_SUBCLASS_AUTO_IMPL(FormattedDateInterval)


UOBJECT_DEFINE_RTTI_IMPLEMENTATION(DateIntervalFormat)

// Mutex, protects access to fDateFormat, fFromCalendar and fToCalendar.
//        Needed because these data members are modified by const methods of DateIntervalFormat.

static UMutex gFormatterMutex;

DateIntervalFormat* U_EXPORT2
DateIntervalFormat::createInstance(const UnicodeString& skeleton,
                                   UErrorCode& status) {
    return createInstance(skeleton, Locale::getDefault(), status);
}


DateIntervalFormat* U_EXPORT2
DateIntervalFormat::createInstance(const UnicodeString& skeleton,
                                   const Locale& locale,
                                   UErrorCode& status) {
#ifdef DTITVFMT_DEBUG
    char result[1000];
    char result_1[1000];
    char mesg[2000];
    skeleton.extract(0,  skeleton.length(), result, "UTF-8");
    UnicodeString pat;
    ((SimpleDateFormat*)dtfmt)->toPattern(pat);
    pat.extract(0,  pat.length(), result_1, "UTF-8");
    snprintf(mesg, sizeof(mesg), "skeleton: %s; pattern: %s\n", result, result_1);
    PRINTMESG(mesg)
#endif

    DateIntervalInfo* dtitvinf = new DateIntervalInfo(locale, status);
    if (dtitvinf == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
    }
    return create(locale, dtitvinf, &skeleton, status);
}



DateIntervalFormat* U_EXPORT2
DateIntervalFormat::createInstance(const UnicodeString& skeleton,
                                   const DateIntervalInfo& dtitvinf,
                                   UErrorCode& status) {
    return createInstance(skeleton, Locale::getDefault(), dtitvinf, status);
}


DateIntervalFormat* U_EXPORT2
DateIntervalFormat::createInstance(const UnicodeString& skeleton,
                                   const Locale& locale,
                                   const DateIntervalInfo& dtitvinf,
                                   UErrorCode& status) {
    DateIntervalInfo* ptn = dtitvinf.clone();
    return create(locale, ptn, &skeleton, status);
}


DateIntervalFormat::DateIntervalFormat()
:   fInfo(nullptr),
    fDateFormat(nullptr),
    fFromCalendar(nullptr),
    fToCalendar(nullptr),
    fLocale(Locale::getRoot()),
    fDatePattern(nullptr),
    fTimePattern(nullptr),
    fDateTimeFormat(nullptr),
    fCapitalizationContext(UDISPCTX_CAPITALIZATION_NONE)
{}


DateIntervalFormat::DateIntervalFormat(const DateIntervalFormat& itvfmt)
:   Format(itvfmt),
    fInfo(nullptr),
    fDateFormat(nullptr),
    fFromCalendar(nullptr),
    fToCalendar(nullptr),
    fLocale(itvfmt.fLocale),
    fDatePattern(nullptr),
    fTimePattern(nullptr),
    fDateTimeFormat(nullptr),
    fCapitalizationContext(UDISPCTX_CAPITALIZATION_NONE) {
    *this = itvfmt;
}


DateIntervalFormat&
DateIntervalFormat::operator=(const DateIntervalFormat& itvfmt) {
    if ( this != &itvfmt ) {
        delete fDateFormat;
        delete fInfo;
        delete fFromCalendar;
        delete fToCalendar;
        delete fDatePattern;
        delete fTimePattern;
        delete fDateTimeFormat;
        {
            Mutex lock(&gFormatterMutex);
            if ( itvfmt.fDateFormat ) {
                fDateFormat = itvfmt.fDateFormat->clone();
            } else {
                fDateFormat = nullptr;
            }
            if ( itvfmt.fFromCalendar ) {
                fFromCalendar = itvfmt.fFromCalendar->clone();
            } else {
                fFromCalendar = nullptr;
            }
            if ( itvfmt.fToCalendar ) {
                fToCalendar = itvfmt.fToCalendar->clone();
            } else {
                fToCalendar = nullptr;
            }
        }
        if ( itvfmt.fInfo ) {
            fInfo = itvfmt.fInfo->clone();
        } else {
            fInfo = nullptr;
        }
        fSkeleton = itvfmt.fSkeleton;
        int8_t i;
        for ( i = 0; i< DateIntervalInfo::kIPI_MAX_INDEX; ++i ) {
            fIntervalPatterns[i] = itvfmt.fIntervalPatterns[i];
        }
        fLocale = itvfmt.fLocale;
        fDatePattern    = (itvfmt.fDatePattern)?    itvfmt.fDatePattern->clone(): nullptr;
        fTimePattern    = (itvfmt.fTimePattern)?    itvfmt.fTimePattern->clone(): nullptr;
        fDateTimeFormat = (itvfmt.fDateTimeFormat)? itvfmt.fDateTimeFormat->clone(): nullptr;
        fCapitalizationContext = itvfmt.fCapitalizationContext;
    }
    return *this;
}


DateIntervalFormat::~DateIntervalFormat() {
    delete fInfo;
    delete fDateFormat;
    delete fFromCalendar;
    delete fToCalendar;
    delete fDatePattern;
    delete fTimePattern;
    delete fDateTimeFormat;
}


DateIntervalFormat*
DateIntervalFormat::clone() const {
    return new DateIntervalFormat(*this);
}


bool
DateIntervalFormat::operator==(const Format& other) const {
    if (typeid(*this) != typeid(other)) {return false;}
    const DateIntervalFormat* fmt = (DateIntervalFormat*)&other;
    if (this == fmt) {return true;}
    if (!Format::operator==(other)) {return false;}
    if ((fInfo != fmt->fInfo) && (fInfo == nullptr || fmt->fInfo == nullptr)) {return false;}
    if (fInfo && fmt->fInfo && (*fInfo != *fmt->fInfo )) {return false;}
    {
        Mutex lock(&gFormatterMutex);
        if (fDateFormat != fmt->fDateFormat && (fDateFormat == nullptr || fmt->fDateFormat == nullptr)) {return false;}
        if (fDateFormat && fmt->fDateFormat && (*fDateFormat != *fmt->fDateFormat)) {return false;}
    }
    // note: fFromCalendar and fToCalendar hold no persistent state, and therefore do not participate in operator ==.
    //       fDateFormat has the primary calendar for the DateIntervalFormat.
    if (fSkeleton != fmt->fSkeleton) {return false;}
    if (fDatePattern != fmt->fDatePattern && (fDatePattern == nullptr || fmt->fDatePattern == nullptr)) {return false;}
    if (fDatePattern && fmt->fDatePattern && (*fDatePattern != *fmt->fDatePattern)) {return false;}
    if (fTimePattern != fmt->fTimePattern && (fTimePattern == nullptr || fmt->fTimePattern == nullptr)) {return false;}
    if (fTimePattern && fmt->fTimePattern && (*fTimePattern != *fmt->fTimePattern)) {return false;}
    if (fDateTimeFormat != fmt->fDateTimeFormat && (fDateTimeFormat == nullptr || fmt->fDateTimeFormat == nullptr)) {return false;}
    if (fDateTimeFormat && fmt->fDateTimeFormat && (*fDateTimeFormat != *fmt->fDateTimeFormat)) {return false;}
    if (fLocale != fmt->fLocale) {return false;}

    for (int32_t i = 0; i< DateIntervalInfo::kIPI_MAX_INDEX; ++i ) {
        if (fIntervalPatterns[i].firstPart != fmt->fIntervalPatterns[i].firstPart) {return false;}
        if (fIntervalPatterns[i].secondPart != fmt->fIntervalPatterns[i].secondPart ) {return false;}
        if (fIntervalPatterns[i].laterDateFirst != fmt->fIntervalPatterns[i].laterDateFirst) {return false;}
    }
    if (fCapitalizationContext != fmt->fCapitalizationContext) {return false;}
    return true;
}


UnicodeString&
DateIntervalFormat::format(const Formattable& obj,
                           UnicodeString& appendTo,
                           FieldPosition& fieldPosition,
                           UErrorCode& status) const {
    if ( U_FAILURE(status) ) {
        return appendTo;
    }

    if ( obj.getType() == Formattable::kObject ) {
        const UObject* formatObj = obj.getObject();
        const DateInterval* interval = dynamic_cast<const DateInterval*>(formatObj);
        if (interval != nullptr) {
            return format(interval, appendTo, fieldPosition, status);
        }
    }
    status = U_ILLEGAL_ARGUMENT_ERROR;
    return appendTo;
}


UnicodeString&
DateIntervalFormat::format(const DateInterval* dtInterval,
                           UnicodeString& appendTo,
                           FieldPosition& fieldPosition,
                           UErrorCode& status) const {
    if ( U_FAILURE(status) ) {
        return appendTo;
    }
    if (fDateFormat == nullptr || fInfo == nullptr) {
        status = U_INVALID_STATE_ERROR;
        return appendTo;
    }

    FieldPositionOnlyHandler handler(fieldPosition);
    handler.setAcceptFirstOnly(true);
    int8_t ignore;

    Mutex lock(&gFormatterMutex);
    return formatIntervalImpl(*dtInterval, appendTo, ignore, handler, status);
}


FormattedDateInterval DateIntervalFormat::formatToValue(
        const DateInterval& dtInterval,
        UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return FormattedDateInterval(status);
    }
    // LocalPointer only sets OOM status if U_SUCCESS is true.
    LocalPointer<FormattedDateIntervalData> result(new FormattedDateIntervalData(status), status);
    if (U_FAILURE(status)) {
        return FormattedDateInterval(status);
    }
    UnicodeString string;
    int8_t firstIndex;
    auto handler = result->getHandler(status);
    handler.setCategory(UFIELD_CATEGORY_DATE);
    {
        Mutex lock(&gFormatterMutex);
        formatIntervalImpl(dtInterval, string, firstIndex, handler, status);
    }
    handler.getError(status);
    result->appendString(string, status);
    if (U_FAILURE(status)) {
        return FormattedDateInterval(status);
    }

    // Compute the span fields and sort them into place:
    if (firstIndex != -1) {
        result->addOverlapSpans(UFIELD_CATEGORY_DATE_INTERVAL_SPAN, firstIndex, status);
        if (U_FAILURE(status)) {
            return FormattedDateInterval(status);
        }
        result->sort();
    }

    return FormattedDateInterval(result.orphan());
}


UnicodeString&
DateIntervalFormat::format(Calendar& fromCalendar,
                           Calendar& toCalendar,
                           UnicodeString& appendTo,
                           FieldPosition& pos,
                           UErrorCode& status) const {
    FieldPositionOnlyHandler handler(pos);
    handler.setAcceptFirstOnly(true);
    int8_t ignore;

    Mutex lock(&gFormatterMutex);
    return formatImpl(fromCalendar, toCalendar, appendTo, ignore, handler, status);
}


FormattedDateInterval DateIntervalFormat::formatToValue(
        Calendar& fromCalendar,
        Calendar& toCalendar,
        UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return FormattedDateInterval(status);
    }
    // LocalPointer only sets OOM status if U_SUCCESS is true.
    LocalPointer<FormattedDateIntervalData> result(new FormattedDateIntervalData(status), status);
    if (U_FAILURE(status)) {
        return FormattedDateInterval(status);
    }
    UnicodeString string;
    int8_t firstIndex;
    auto handler = result->getHandler(status);
    handler.setCategory(UFIELD_CATEGORY_DATE);
    {
        Mutex lock(&gFormatterMutex);
        formatImpl(fromCalendar, toCalendar, string, firstIndex, handler, status);
    }
    handler.getError(status);
    result->appendString(string, status);
    if (U_FAILURE(status)) {
        return FormattedDateInterval(status);
    }

    // Compute the span fields and sort them into place:
    if (firstIndex != -1) {
        result->addOverlapSpans(UFIELD_CATEGORY_DATE_INTERVAL_SPAN, firstIndex, status);
        result->sort();
    }

    return FormattedDateInterval(result.orphan());
}


UnicodeString& DateIntervalFormat::formatIntervalImpl(
        const DateInterval& dtInterval,
        UnicodeString& appendTo,
        int8_t& firstIndex,
        FieldPositionHandler& fphandler,
        UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return appendTo;
    }
    if (fFromCalendar == nullptr || fToCalendar == nullptr) {
        status = U_INVALID_STATE_ERROR;
        return appendTo;
    }
    fFromCalendar->setTime(dtInterval.getFromDate(), status);
    fToCalendar->setTime(dtInterval.getToDate(), status);
    return formatImpl(*fFromCalendar, *fToCalendar, appendTo, firstIndex, fphandler, status);
}


// The following is only called from within the gFormatterMutex lock
UnicodeString&
DateIntervalFormat::formatImpl(Calendar& fromCalendar,
                           Calendar& toCalendar,
                           UnicodeString& appendTo,
                           int8_t& firstIndex,
                           FieldPositionHandler& fphandler,
                           UErrorCode& status) const {
    if ( U_FAILURE(status) ) {
        return appendTo;
    }

    // Initialize firstIndex to -1 (single date, no range)
    firstIndex = -1;

    // not support different calendar types and time zones
    //if ( fromCalendar.getType() != toCalendar.getType() ) {
    if ( !fromCalendar.isEquivalentTo(toCalendar) ) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return appendTo;
    }

    // First, find the largest different calendar field.
    UCalendarDateFields field = UCAL_FIELD_COUNT;

    if ( fromCalendar.get(UCAL_ERA,status) != toCalendar.get(UCAL_ERA,status)) {
        field = UCAL_ERA;
    } else if ( fromCalendar.get(UCAL_YEAR, status) !=
                toCalendar.get(UCAL_YEAR, status) ) {
        field = UCAL_YEAR;
    } else if ( fromCalendar.get(UCAL_MONTH, status) !=
                toCalendar.get(UCAL_MONTH, status) ) {
        field = UCAL_MONTH;
    } else if ( fromCalendar.get(UCAL_DATE, status) !=
                toCalendar.get(UCAL_DATE, status) ) {
        field = UCAL_DATE;
    } else if ( fromCalendar.get(UCAL_AM_PM, status) !=
                toCalendar.get(UCAL_AM_PM, status) ) {
        field = UCAL_AM_PM;
    } else if ( fromCalendar.get(UCAL_HOUR, status) !=
                toCalendar.get(UCAL_HOUR, status) ) {
        field = UCAL_HOUR;
    } else if ( fromCalendar.get(UCAL_MINUTE, status) !=
                toCalendar.get(UCAL_MINUTE, status) ) {
        field = UCAL_MINUTE;
    } else if ( fromCalendar.get(UCAL_SECOND, status) !=
                toCalendar.get(UCAL_SECOND, status) ) {
        field = UCAL_SECOND;
    } else if ( fromCalendar.get(UCAL_MILLISECOND, status) !=
                toCalendar.get(UCAL_MILLISECOND, status) ) {
        field = UCAL_MILLISECOND;
    }

    if ( U_FAILURE(status) ) {
        return appendTo;
    }
    UErrorCode tempStatus = U_ZERO_ERROR; // for setContext, ignored
    // Set up fDateFormat to handle the first or only part of the interval
    // (override later for any second part). Inside lock, OK to modify fDateFormat.
    fDateFormat->setContext(fCapitalizationContext, tempStatus);

    if ( field == UCAL_FIELD_COUNT ) {
        /* ignore the millisecond etc. small fields' difference.
         * use single date when all the above are the same.
         */
        return fDateFormat->_format(fromCalendar, appendTo, fphandler, status);
    }
    UBool fromToOnSameDay = (field==UCAL_AM_PM || field==UCAL_HOUR || field==UCAL_MINUTE || field==UCAL_SECOND || field==UCAL_MILLISECOND);

    // following call should not set wrong status,
    // all the pass-in fields are valid till here
    int32_t itvPtnIndex = DateIntervalInfo::calendarFieldToIntervalIndex(field,
                                                                        status);
    const PatternInfo& intervalPattern = fIntervalPatterns[itvPtnIndex];

    if ( intervalPattern.firstPart.isEmpty() &&
         intervalPattern.secondPart.isEmpty() ) {
        if ( fDateFormat->isFieldUnitIgnored(field) ) {
            /* the largest different calendar field is small than
             * the smallest calendar field in pattern,
             * return single date format.
             */
            return fDateFormat->_format(fromCalendar, appendTo, fphandler, status);
        }
        return fallbackFormat(fromCalendar, toCalendar, fromToOnSameDay, appendTo, firstIndex, fphandler, status);
    }
    // If the first part in interval pattern is empty,
    // the 2nd part of it saves the full-pattern used in fall-back.
    // For a 'real' interval pattern, the first part will never be empty.
    if ( intervalPattern.firstPart.isEmpty() ) {
        // fall back
        UnicodeString originalPattern;
        fDateFormat->toPattern(originalPattern);
        fDateFormat->applyPattern(intervalPattern.secondPart);
        appendTo = fallbackFormat(fromCalendar, toCalendar, fromToOnSameDay, appendTo, firstIndex, fphandler, status);
        fDateFormat->applyPattern(originalPattern);
        return appendTo;
    }
    Calendar* firstCal;
    Calendar* secondCal;
    if ( intervalPattern.laterDateFirst ) {
        firstCal = &toCalendar;
        secondCal = &fromCalendar;
        firstIndex = 1;
    } else {
        firstCal = &fromCalendar;
        secondCal = &toCalendar;
        firstIndex = 0;
    }
    // break the interval pattern into 2 parts,
    // first part should not be empty,
    UnicodeString originalPattern;
    fDateFormat->toPattern(originalPattern);
    fDateFormat->applyPattern(intervalPattern.firstPart);
    fDateFormat->_format(*firstCal, appendTo, fphandler, status);

    if ( !intervalPattern.secondPart.isEmpty() ) {
        fDateFormat->applyPattern(intervalPattern.secondPart);
        // No capitalization for second part of interval
        tempStatus = U_ZERO_ERROR;
        fDateFormat->setContext(UDISPCTX_CAPITALIZATION_NONE, tempStatus);
        fDateFormat->_format(*secondCal, appendTo, fphandler, status);
    }
    fDateFormat->applyPattern(originalPattern);
    return appendTo;
}



void
DateIntervalFormat::parseObject(const UnicodeString& /* source */,
                                Formattable& /* result */,
                                ParsePosition& /* parse_pos */) const {
    // parseObject(const UnicodeString&, Formattable&, UErrorCode&) const
    // will set status as U_INVALID_FORMAT_ERROR if
    // parse_pos is still 0
}




const DateIntervalInfo*
DateIntervalFormat::getDateIntervalInfo() const {
    return fInfo;
}


void
DateIntervalFormat::setDateIntervalInfo(const DateIntervalInfo& newItvPattern,
                                        UErrorCode& status) {
    delete fInfo;
    fInfo = new DateIntervalInfo(newItvPattern);
    if (fInfo == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    }

    // Delete patterns that get reset by initializePattern
    delete fDatePattern;
    fDatePattern = nullptr;
    delete fTimePattern;
    fTimePattern = nullptr;
    delete fDateTimeFormat;
    fDateTimeFormat = nullptr;

    if (fDateFormat) {
        initializePattern(status);
    }
}



const DateFormat*
DateIntervalFormat::getDateFormat() const {
    return fDateFormat;
}


void
DateIntervalFormat::adoptTimeZone(TimeZone* zone)
{
    if (fDateFormat != nullptr) {
        fDateFormat->adoptTimeZone(zone);
    }
    // The fDateFormat has the primary calendar for the DateIntervalFormat and has
    // ownership of any adopted TimeZone; fFromCalendar and fToCalendar are internal
    // work clones of that calendar (and should not also be given ownership of the
    // adopted TimeZone).
    if (fFromCalendar) {
        fFromCalendar->setTimeZone(*zone);
    }
    if (fToCalendar) {
        fToCalendar->setTimeZone(*zone);
    }
}

void
DateIntervalFormat::setTimeZone(const TimeZone& zone)
{
    if (fDateFormat != nullptr) {
        fDateFormat->setTimeZone(zone);
    }
    // The fDateFormat has the primary calendar for the DateIntervalFormat;
    // fFromCalendar and fToCalendar are internal work clones of that calendar.
    if (fFromCalendar) {
        fFromCalendar->setTimeZone(zone);
    }
    if (fToCalendar) {
        fToCalendar->setTimeZone(zone);
    }
}

const TimeZone&
DateIntervalFormat::getTimeZone() const
{
    if (fDateFormat != nullptr) {
        Mutex lock(&gFormatterMutex);
        return fDateFormat->getTimeZone();
    }
    // If fDateFormat is nullptr (unexpected), create default timezone.
    return *(TimeZone::createDefault());
}

void
DateIntervalFormat::setContext(UDisplayContext value, UErrorCode& status)
{
    if (U_FAILURE(status))
        return;
    if (static_cast<UDisplayContextType>(static_cast<uint32_t>(value) >> 8) == UDISPCTX_TYPE_CAPITALIZATION) {
        fCapitalizationContext = value;
    } else {
        status = U_ILLEGAL_ARGUMENT_ERROR;
    }
}

UDisplayContext
DateIntervalFormat::getContext(UDisplayContextType type, UErrorCode& status) const
{
    if (U_FAILURE(status))
        return static_cast<UDisplayContext>(0);
    if (type != UDISPCTX_TYPE_CAPITALIZATION) {
        status = U_ILLEGAL_ARGUMENT_ERROR;
        return static_cast<UDisplayContext>(0);
    }
    return fCapitalizationContext;
}

DateIntervalFormat::DateIntervalFormat(const Locale& locale,
                                       DateIntervalInfo* dtItvInfo,
                                       const UnicodeString* skeleton,
                                       UErrorCode& status)
:   fInfo(nullptr),
    fDateFormat(nullptr),
    fFromCalendar(nullptr),
    fToCalendar(nullptr),
    fLocale(locale),
    fDatePattern(nullptr),
    fTimePattern(nullptr),
    fDateTimeFormat(nullptr),
    fCapitalizationContext(UDISPCTX_CAPITALIZATION_NONE)
{
    LocalPointer<DateIntervalInfo> info(dtItvInfo, status);
    LocalPointer<SimpleDateFormat> dtfmt(static_cast<SimpleDateFormat *>(
            DateFormat::createInstanceForSkeleton(*skeleton, locale, status)), status);
    if (U_FAILURE(status)) {
        return;
    }

    if ( skeleton ) {
        fSkeleton = *skeleton;
    }
    fInfo = info.orphan();
    fDateFormat = dtfmt.orphan();
    if ( fDateFormat->getCalendar() ) {
        fFromCalendar = fDateFormat->getCalendar()->clone();
        fToCalendar = fDateFormat->getCalendar()->clone();
    }
    initializePattern(status);
}

DateIntervalFormat* U_EXPORT2
DateIntervalFormat::create(const Locale& locale,
                           DateIntervalInfo* dtitvinf,
                           const UnicodeString* skeleton,
                           UErrorCode& status) {
    DateIntervalFormat* f = new DateIntervalFormat(locale, dtitvinf,
                                                   skeleton, status);
    if ( f == nullptr ) {
        status = U_MEMORY_ALLOCATION_ERROR;
        delete dtitvinf;
    } else if ( U_FAILURE(status) ) {
        // safe to delete f, although nothing actually is saved
        delete f;
        f = nullptr;
    }
    return f;
}



/**
 * Initialize interval patterns locale to this formatter
 *
 * This code is a bit complicated since
 * 1. the interval patterns saved in resource bundle files are interval
 *    patterns based on date or time only.
 *    It does not have interval patterns based on both date and time.
 *    Interval patterns on both date and time are algorithm generated.
 *
 *    For example, it has interval patterns on skeleton "dMy" and "hm",
 *    but it does not have interval patterns on skeleton "dMyhm".
 *
 *    The rule to genearte interval patterns for both date and time skeleton are
 *    1) when the year, month, or day differs, concatenate the two original
 *    expressions with a separator between,
 *    For example, interval pattern from "Jan 10, 2007 10:10 am"
 *    to "Jan 11, 2007 10:10am" is
 *    "Jan 10, 2007 10:10 am - Jan 11, 2007 10:10am"
 *
 *    2) otherwise, present the date followed by the range expression
 *    for the time.
 *    For example, interval pattern from "Jan 10, 2007 10:10 am"
 *    to "Jan 10, 2007 11:10am" is
 *    "Jan 10, 2007 10:10 am - 11:10am"
 *
 * 2. even a pattern does not request a certion calendar field,
 *    the interval pattern needs to include such field if such fields are
 *    different between 2 dates.
 *    For example, a pattern/skeleton is "hm", but the interval pattern
 *    includes year, month, and date when year, month, and date differs.
 *
 * @param status          output param set to success/failure code on exit
 * @stable ICU 4.0
 */
void
DateIntervalFormat::initializePattern(UErrorCode& status) {
    if ( U_FAILURE(status) ) {
        return;
    }
    const Locale& locale = fDateFormat->getSmpFmtLocale();
    if ( fSkeleton.isEmpty() ) {
        UnicodeString fullPattern;
        fDateFormat->toPattern(fullPattern);
#ifdef DTITVFMT_DEBUG
    char result[1000];
    char result_1[1000];
    char mesg[2000];
    fSkeleton.extract(0,  fSkeleton.length(), result, "UTF-8");
    snprintf(mesg, sizeof(mesg), "in getBestSkeleton: fSkeleton: %s; \n", result);
    PRINTMESG(mesg)
#endif
        // fSkeleton is already set by createDateIntervalInstance()
        // or by createInstance(UnicodeString skeleton, .... )
        fSkeleton = DateTimePatternGenerator::staticGetSkeleton(
                fullPattern, status);
        if ( U_FAILURE(status) ) {
            return;
        }
    }

    // initialize the fIntervalPattern ordering
    int8_t i;
    for ( i = 0; i < DateIntervalInfo::kIPI_MAX_INDEX; ++i ) {
        fIntervalPatterns[i].laterDateFirst = fInfo->getDefaultOrder();
    }

    /* Check whether the skeleton is a combination of date and time.
     * For the complication reason 1 explained above.
     */
    UnicodeString dateSkeleton;
    UnicodeString timeSkeleton;
    UnicodeString normalizedTimeSkeleton;
    UnicodeString normalizedDateSkeleton;


    /* the difference between time skeleton and normalizedTimeSkeleton are:
     * 1. (Formerly, normalized time skeleton folded 'H' to 'h'; no longer true)
     * 2. (Formerly, 'a' was omitted in normalized time skeleton; this is now handled elsewhere)
     * 3. there is only one appearance for 'h' or 'H', 'm','v', 'z' in normalized
     *    time skeleton
     *
     * The difference between date skeleton and normalizedDateSkeleton are:
     * 1. both 'y' and 'd' appear only once in normalizeDateSkeleton
     * 2. 'E' and 'EE' are normalized into 'EEE'
     * 3. 'MM' is normalized into 'M'
     */
    UnicodeString convertedSkeleton = normalizeHourMetacharacters(fSkeleton);
    getDateTimeSkeleton(convertedSkeleton, dateSkeleton, normalizedDateSkeleton,
                        timeSkeleton, normalizedTimeSkeleton);

#ifdef DTITVFMT_DEBUG
    char result[1000];
    char result_1[1000];
    char mesg[2000];
    fSkeleton.extract(0,  fSkeleton.length(), result, "UTF-8");
    snprintf(mesg, sizeof(mesg), "in getBestSkeleton: fSkeleton: %s; \n", result);
    PRINTMESG(mesg)
#endif

    // move this up here since we need it for fallbacks
    if ( timeSkeleton.length() > 0 && dateSkeleton.length() > 0 ) {
        // Need the Date/Time pattern for concatenation of the date
        // with the time interval.
        // The date/time pattern ( such as {0} {1} ) is saved in
        // calendar, that is why need to get the CalendarData here.
        LocalUResourceBundlePointer dateTimePatternsRes(ures_open(nullptr, locale.getBaseName(), &status));
        ures_getByKey(dateTimePatternsRes.getAlias(), gCalendarTag,
                      dateTimePatternsRes.getAlias(), &status);
        ures_getByKeyWithFallback(dateTimePatternsRes.getAlias(), gGregorianTag,
                                  dateTimePatternsRes.getAlias(), &status);
        ures_getByKeyWithFallback(dateTimePatternsRes.getAlias(), gDateTimePatternsTag,
                                  dateTimePatternsRes.getAlias(), &status);

        int32_t dateTimeFormatLength;
        const char16_t* dateTimeFormat = ures_getStringByIndex(
                                            dateTimePatternsRes.getAlias(),
                                            static_cast<int32_t>(DateFormat::kDateTime),
                                            &dateTimeFormatLength, &status);
        if ( U_SUCCESS(status) && dateTimeFormatLength >= 3 ) {
            fDateTimeFormat = new UnicodeString(dateTimeFormat, dateTimeFormatLength);
            if (fDateTimeFormat == nullptr) {
                status = U_MEMORY_ALLOCATION_ERROR;
                return;
            }
        }
    }

    UBool found = setSeparateDateTimePtn(normalizedDateSkeleton,
                                         normalizedTimeSkeleton);

    // for skeletons with seconds, found is false and we enter this block
    if ( found == false ) {
        // use fallback
        // TODO: if user asks "m"(minute), but "d"(day) differ
        if ( timeSkeleton.length() != 0 ) {
            if ( dateSkeleton.length() == 0 ) {
                // prefix with yMd
                timeSkeleton.insert(0, gDateFormatSkeleton[DateFormat::kShort], -1);
                UnicodeString pattern = DateFormat::getBestPattern(
                        locale, timeSkeleton, status);
                if ( U_FAILURE(status) ) {
                    return;
                }
                // for fall back interval patterns,
                // the first part of the pattern is empty,
                // the second part of the pattern is the full-pattern
                // should be used in fall-back.
                setPatternInfo(UCAL_DATE, nullptr, &pattern, fInfo->getDefaultOrder());
                setPatternInfo(UCAL_MONTH, nullptr, &pattern, fInfo->getDefaultOrder());
                setPatternInfo(UCAL_YEAR, nullptr, &pattern, fInfo->getDefaultOrder());

                timeSkeleton.insert(0, CAP_G);
                pattern = DateFormat::getBestPattern(
                        locale, timeSkeleton, status);
                if ( U_FAILURE(status) ) {
                    return;
                }
                setPatternInfo(UCAL_ERA, nullptr, &pattern, fInfo->getDefaultOrder());
            } else {
                // TODO: fall back
            }
        } else {
            // TODO: fall back
        }
        return;
    } // end of skeleton not found
    // interval patterns for skeleton are found in resource
    if ( timeSkeleton.length() == 0 ) {
        // done
    } else if ( dateSkeleton.length() == 0 ) {
        // prefix with yMd
        timeSkeleton.insert(0, gDateFormatSkeleton[DateFormat::kShort], -1);
        UnicodeString pattern = DateFormat::getBestPattern(
                locale, timeSkeleton, status);
        if ( U_FAILURE(status) ) {
            return;
        }
        // for fall back interval patterns,
        // the first part of the pattern is empty,
        // the second part of the pattern is the full-pattern
        // should be used in fall-back.
        setPatternInfo(UCAL_DATE, nullptr, &pattern, fInfo->getDefaultOrder());
        setPatternInfo(UCAL_MONTH, nullptr, &pattern, fInfo->getDefaultOrder());
        setPatternInfo(UCAL_YEAR, nullptr, &pattern, fInfo->getDefaultOrder());

        timeSkeleton.insert(0, CAP_G);
        pattern = DateFormat::getBestPattern(
                locale, timeSkeleton, status);
        if ( U_FAILURE(status) ) {
            return;
        }
        setPatternInfo(UCAL_ERA, nullptr, &pattern, fInfo->getDefaultOrder());
    } else {
        /* if both present,
         * 1) when the era, year, month, or day differs,
         * concatenate the two original expressions with a separator between,
         * 2) otherwise, present the date followed by the
         * range expression for the time.
         */
        /*
         * 1) when the era, year, month, or day differs,
         * concatenate the two original expressions with a separator between,
         */
        // if field exists, use fall back
        UnicodeString skeleton = fSkeleton;
        if ( !fieldExistsInSkeleton(UCAL_DATE, dateSkeleton) ) {
            // prefix skeleton with 'd'
            skeleton.insert(0, LOW_D);
            setFallbackPattern(UCAL_DATE, skeleton, status);
        }
        if ( !fieldExistsInSkeleton(UCAL_MONTH, dateSkeleton) ) {
            // then prefix skeleton with 'M'
            skeleton.insert(0, CAP_M);
            setFallbackPattern(UCAL_MONTH, skeleton, status);
        }
        if ( !fieldExistsInSkeleton(UCAL_YEAR, dateSkeleton) ) {
            // then prefix skeleton with 'y'
            skeleton.insert(0, LOW_Y);
            setFallbackPattern(UCAL_YEAR, skeleton, status);
        }
        if ( !fieldExistsInSkeleton(UCAL_ERA, dateSkeleton) ) {
            // then prefix skeleton with 'G'
            skeleton.insert(0, CAP_G);
            setFallbackPattern(UCAL_ERA, skeleton, status);
        }

        /*
         * 2) otherwise, present the date followed by the
         * range expression for the time.
         */

        if ( fDateTimeFormat == nullptr ) {
            // earlier failure getting dateTimeFormat
            return;
        }

        UnicodeString datePattern = DateFormat::getBestPattern(
                locale, dateSkeleton, status);

        concatSingleDate2TimeInterval(*fDateTimeFormat, datePattern, UCAL_AM_PM, status);
        concatSingleDate2TimeInterval(*fDateTimeFormat, datePattern, UCAL_HOUR, status);
        concatSingleDate2TimeInterval(*fDateTimeFormat, datePattern, UCAL_MINUTE, status);
    }
}



UnicodeString
DateIntervalFormat::normalizeHourMetacharacters(const UnicodeString& skeleton) const {
    UnicodeString result = skeleton;
    
    char16_t hourMetachar = u'\0';
    char16_t dayPeriodChar = u'\0';
    int32_t hourFieldStart = 0;
    int32_t hourFieldLength = 0;
    int32_t dayPeriodStart = 0;
    int32_t dayPeriodLength = 0;
    for (int32_t i = 0; i < result.length(); i++) {
        char16_t c = result[i];
        if (c == LOW_J || c == CAP_J || c == CAP_C || c == LOW_H || c == CAP_H || c == LOW_K || c == CAP_K) {
            if (hourMetachar == u'\0') {
                hourMetachar = c;
                hourFieldStart = i;
            }
            ++hourFieldLength;
        } else if (c == LOW_A || c == LOW_B || c == CAP_B) {
            if (dayPeriodChar == u'\0') {
                dayPeriodChar = c;
                dayPeriodStart = i;
            }
            ++dayPeriodLength;
        } else {
            if (hourMetachar != u'\0' && dayPeriodChar != u'\0') {
                break;
            }
        }
    }
    
    if (hourMetachar != u'\0') {
        UErrorCode err = U_ZERO_ERROR;
        char16_t hourChar = CAP_H;
        UnicodeString convertedPattern = DateFormat::getBestPattern(fLocale, UnicodeString(hourMetachar), err);

        if (U_SUCCESS(err)) {
            // strip literal text from the pattern (so literal characters don't get mistaken for pattern
            // characters-- such as the 'h' in 'Uhr' in Germam)
            int32_t firstQuotePos;
            while ((firstQuotePos = convertedPattern.indexOf(u'\'')) != -1) {
                int32_t secondQuotePos = convertedPattern.indexOf(u'\'', firstQuotePos + 1);
                if (secondQuotePos == -1) {
                    secondQuotePos = firstQuotePos;
                }
                convertedPattern.replace(firstQuotePos, (secondQuotePos - firstQuotePos) + 1, UnicodeString());
            }
        
            if (convertedPattern.indexOf(LOW_H) != -1) {
                hourChar = LOW_H;
            } else if (convertedPattern.indexOf(CAP_K) != -1) {
                hourChar = CAP_K;
            } else if (convertedPattern.indexOf(LOW_K) != -1) {
                hourChar = LOW_K;
            }
            
            if (convertedPattern.indexOf(LOW_B) != -1) {
                dayPeriodChar = LOW_B;
            } else if (convertedPattern.indexOf(CAP_B) != -1) {
                dayPeriodChar = CAP_B;
            } else if (dayPeriodChar == u'\0') {
                dayPeriodChar = LOW_A;
            }
        }
        
        UnicodeString hourAndDayPeriod(hourChar);
        if (hourChar != CAP_H && hourChar != LOW_K) {
            int32_t newDayPeriodLength = 0;
            if (dayPeriodLength >= 5 || hourFieldLength >= 5) {
                newDayPeriodLength = 5;
            } else if (dayPeriodLength >= 3 || hourFieldLength >= 3) {
                newDayPeriodLength = 3;
            } else {
                newDayPeriodLength = 1;
            }
            for (int32_t i = 0; i < newDayPeriodLength; i++) {
                hourAndDayPeriod.append(dayPeriodChar);
            }
        }
        result.replace(hourFieldStart, hourFieldLength, hourAndDayPeriod);
        if (dayPeriodStart > hourFieldStart) {
            // before deleting the original day period field, adjust its position in case
            // we just changed the size of the hour field (and new day period field)
            dayPeriodStart += hourAndDayPeriod.length() - hourFieldLength;
        }
        result.remove(dayPeriodStart, dayPeriodLength);
    }
    return result;
}


void  U_EXPORT2
DateIntervalFormat::getDateTimeSkeleton(const UnicodeString& skeleton,
                                        UnicodeString& dateSkeleton,
                                        UnicodeString& normalizedDateSkeleton,
                                        UnicodeString& timeSkeleton,
                                        UnicodeString& normalizedTimeSkeleton) {
    // dateSkeleton follows the sequence of y*M*E*d*
    // timeSkeleton follows the sequence of hm*[v|z]?
    int32_t ECount = 0;
    int32_t dCount = 0;
    int32_t MCount = 0;
    int32_t yCount = 0;
    int32_t mCount = 0;
    int32_t vCount = 0;
    int32_t zCount = 0;
    char16_t hourChar = u'\0';
    int32_t i;

    for (i = 0; i < skeleton.length(); ++i) {
        char16_t ch = skeleton[i];
        switch ( ch ) {
          case CAP_E:
            dateSkeleton.append(ch);
            ++ECount;
            break;
          case LOW_D:
            dateSkeleton.append(ch);
            ++dCount;
            break;
          case CAP_M:
            dateSkeleton.append(ch);
            ++MCount;
            break;
          case LOW_Y:
            dateSkeleton.append(ch);
            ++yCount;
            break;
          case CAP_G:
          case CAP_Y:
          case LOW_U:
          case CAP_Q:
          case LOW_Q:
          case CAP_L:
          case LOW_L:
          case CAP_W:
          case LOW_W:
          case CAP_D:
          case CAP_F:
          case LOW_G:
          case LOW_E:
          case LOW_C:
          case CAP_U:
          case LOW_R:
            normalizedDateSkeleton.append(ch);
            dateSkeleton.append(ch);
            break;
          case LOW_H:
          case CAP_H:
          case LOW_K:
          case CAP_K:
            timeSkeleton.append(ch);
            if (hourChar == u'\0') {
                hourChar = ch;
            }
            break;
          case LOW_M:
            timeSkeleton.append(ch);
            ++mCount;
            break;
          case LOW_Z:
            ++zCount;
            timeSkeleton.append(ch);
            break;
          case LOW_V:
            ++vCount;
            timeSkeleton.append(ch);
            break;
          case LOW_A:
          case CAP_V:
          case CAP_Z:
          case LOW_J:
          case LOW_S:
          case CAP_S:
          case CAP_A:
          case LOW_B:
          case CAP_B:
            timeSkeleton.append(ch);
            normalizedTimeSkeleton.append(ch);
            break;
        }
    }

    /* generate normalized form for date*/
    if ( yCount != 0 ) {
        for (i = 0; i < yCount; ++i) {
            normalizedDateSkeleton.append(LOW_Y);
        }
    }
    if ( MCount != 0 ) {
        if ( MCount < 3 ) {
            normalizedDateSkeleton.append(CAP_M);
        } else {
            for ( int32_t j = 0; j < MCount && j < MAX_M_COUNT; ++j) {
                 normalizedDateSkeleton.append(CAP_M);
            }
        }
    }
    if ( ECount != 0 ) {
        if ( ECount <= 3 ) {
            normalizedDateSkeleton.append(CAP_E);
        } else {
            for ( int32_t j = 0; j < ECount && j < MAX_E_COUNT; ++j ) {
                 normalizedDateSkeleton.append(CAP_E);
            }
        }
    }
    if ( dCount != 0 ) {
        normalizedDateSkeleton.append(LOW_D);
    }

    /* generate normalized form for time */
    if ( hourChar != u'\0' ) {
        normalizedTimeSkeleton.append(hourChar);
    }
    if ( mCount != 0 ) {
        normalizedTimeSkeleton.append(LOW_M);
    }
    if ( zCount != 0 ) {
        normalizedTimeSkeleton.append(LOW_Z);
    }
    if ( vCount != 0 ) {
        normalizedTimeSkeleton.append(LOW_V);
    }
}


/**
 * Generate date or time interval pattern from resource,
 * and set them into the interval pattern locale to this formatter.
 *
 * It needs to handle the following:
 * 1. need to adjust field width.
 *    For example, the interval patterns saved in DateIntervalInfo
 *    includes "dMMMy", but not "dMMMMy".
 *    Need to get interval patterns for dMMMMy from dMMMy.
 *    Another example, the interval patterns saved in DateIntervalInfo
 *    includes "hmv", but not "hmz".
 *    Need to get interval patterns for "hmz' from 'hmv'
 *
 * 2. there might be no pattern for 'y' differ for skeleton "Md",
 *    in order to get interval patterns for 'y' differ,
 *    need to look for it from skeleton 'yMd'
 *
 * @param dateSkeleton   normalized date skeleton
 * @param timeSkeleton   normalized time skeleton
 * @return               whether the resource is found for the skeleton.
 *                       true if interval pattern found for the skeleton,
 *                       false otherwise.
 * @stable ICU 4.0
 */
UBool
DateIntervalFormat::setSeparateDateTimePtn(
                                 const UnicodeString& dateSkeleton,
                                 const UnicodeString& timeSkeleton) {
    const UnicodeString* skeleton;
    // if both date and time skeleton present,
    // the final interval pattern might include time interval patterns
    // ( when, am_pm, hour, minute differ ),
    // but not date interval patterns ( when year, month, day differ ).
    // For year/month/day differ, it falls back to fall-back pattern.
    if ( timeSkeleton.length() != 0  ) {
        skeleton = &timeSkeleton;
    } else {
        skeleton = &dateSkeleton;
    }

    /* interval patterns for skeleton "dMMMy" (but not "dMMMMy")
     * are defined in resource,
     * interval patterns for skeleton "dMMMMy" are calculated by
     * 1. get the best match skeleton for "dMMMMy", which is "dMMMy"
     * 2. get the interval patterns for "dMMMy",
     * 3. extend "MMM" to "MMMM" in above interval patterns for "dMMMMy"
     * getBestSkeleton() is step 1.
     */
    // best skeleton, and the difference information
    int8_t differenceInfo = 0;
    const UnicodeString* bestSkeleton = fInfo->getBestSkeleton(*skeleton,
                                                               differenceInfo);
    /* best skeleton could be nullptr.
       For example: in "ca" resource file,
       interval format is defined as following
           intervalFormats{
                fallback{"{0} - {1}"}
            }
       there is no skeletons/interval patterns defined,
       and the best skeleton match could be nullptr
     */
    if ( bestSkeleton == nullptr ) {
        return false;
    }

    // Set patterns for fallback use, need to do this
    // before returning if differenceInfo == -1
    UErrorCode status;
    if ( dateSkeleton.length() != 0) {
        status = U_ZERO_ERROR;
        fDatePattern = new UnicodeString(DateFormat::getBestPattern(
                fLocale, dateSkeleton, status));
        // no way to report OOM. :(
    }
    if ( timeSkeleton.length() != 0) {
        status = U_ZERO_ERROR;
        fTimePattern = new UnicodeString(DateFormat::getBestPattern(
                fLocale, timeSkeleton, status));
        // no way to report OOM. :(
    }

    // difference:
    // 0 means the best matched skeleton is the same as input skeleton
    // 1 means the fields are the same, but field width are different
    // 2 means the only difference between fields are v/z,
    // -1 means there are other fields difference
    // (this will happen, for instance, if the supplied skeleton has seconds,
    //  but no skeletons in the intervalFormats data do)
    if ( differenceInfo == -1 ) {
        // skeleton has different fields, not only  v/z difference
        return false;
    }

    if ( timeSkeleton.length() == 0 ) {
        UnicodeString extendedSkeleton;
        UnicodeString extendedBestSkeleton;
        // only has date skeleton
        setIntervalPattern(UCAL_DATE, skeleton, bestSkeleton, differenceInfo,
                           &extendedSkeleton, &extendedBestSkeleton);

        UBool extended = setIntervalPattern(UCAL_MONTH, skeleton, bestSkeleton,
                                     differenceInfo,
                                     &extendedSkeleton, &extendedBestSkeleton);

        if ( extended ) {
            bestSkeleton = &extendedBestSkeleton;
            skeleton = &extendedSkeleton;
        }
        setIntervalPattern(UCAL_YEAR, skeleton, bestSkeleton, differenceInfo,
                           &extendedSkeleton, &extendedBestSkeleton);
        setIntervalPattern(UCAL_ERA, skeleton, bestSkeleton, differenceInfo,
                           &extendedSkeleton, &extendedBestSkeleton);
     } else {
        setIntervalPattern(UCAL_MINUTE, skeleton, bestSkeleton, differenceInfo);
        setIntervalPattern(UCAL_HOUR, skeleton, bestSkeleton, differenceInfo);
        setIntervalPattern(UCAL_AM_PM, skeleton, bestSkeleton, differenceInfo);
    }
    return true;
}



void
DateIntervalFormat::setFallbackPattern(UCalendarDateFields field,
                                       const UnicodeString& skeleton,
                                       UErrorCode& status) {
    if ( U_FAILURE(status) ) {
        return;
    }
    UnicodeString pattern = DateFormat::getBestPattern(
            fLocale, skeleton, status);
    if ( U_FAILURE(status) ) {
        return;
    }
    setPatternInfo(field, nullptr, &pattern, fInfo->getDefaultOrder());
}




void
DateIntervalFormat::setPatternInfo(UCalendarDateFields field,
                                   const UnicodeString* firstPart,
                                   const UnicodeString* secondPart,
                                   UBool laterDateFirst) {
    // for fall back interval patterns,
    // the first part of the pattern is empty,
    // the second part of the pattern is the full-pattern
    // should be used in fall-back.
    UErrorCode status = U_ZERO_ERROR;
    // following should not set any wrong status.
    int32_t itvPtnIndex = DateIntervalInfo::calendarFieldToIntervalIndex(field,
                                                                        status);
    if ( U_FAILURE(status) ) {
        return;
    }
    PatternInfo& ptn = fIntervalPatterns[itvPtnIndex];
    if ( firstPart ) {
        ptn.firstPart = *firstPart;
    }
    if ( secondPart ) {
        ptn.secondPart = *secondPart;
    }
    ptn.laterDateFirst = laterDateFirst;
}

void
DateIntervalFormat::setIntervalPattern(UCalendarDateFields field,
                                       const UnicodeString& intervalPattern) {
    UBool order = fInfo->getDefaultOrder();
    setIntervalPattern(field, intervalPattern, order);
}


void
DateIntervalFormat::setIntervalPattern(UCalendarDateFields field,
                                       const UnicodeString& intervalPattern,
                                       UBool laterDateFirst) {
    const UnicodeString* pattern = &intervalPattern;
    UBool order = laterDateFirst;
    // check for "latestFirst:" or "earliestFirst:" prefix
    int8_t prefixLength = UPRV_LENGTHOF(gLaterFirstPrefix);
    int8_t earliestFirstLength = UPRV_LENGTHOF(gEarlierFirstPrefix);
    UnicodeString realPattern;
    if ( intervalPattern.startsWith(gLaterFirstPrefix, prefixLength) ) {
        order = true;
        intervalPattern.extract(prefixLength,
                                intervalPattern.length() - prefixLength,
                                realPattern);
        pattern = &realPattern;
    } else if ( intervalPattern.startsWith(gEarlierFirstPrefix,
                                           earliestFirstLength) ) {
        order = false;
        intervalPattern.extract(earliestFirstLength,
                                intervalPattern.length() - earliestFirstLength,
                                realPattern);
        pattern = &realPattern;
    }

    int32_t splitPoint = splitPatternInto2Part(*pattern);

    UnicodeString firstPart;
    UnicodeString secondPart;
    pattern->extract(0, splitPoint, firstPart);
    if ( splitPoint < pattern->length() ) {
        pattern->extract(splitPoint, pattern->length()-splitPoint, secondPart);
    }
    setPatternInfo(field, &firstPart, &secondPart, order);
}




/**
 * Generate interval pattern from existing resource
 *
 * It not only save the interval patterns,
 * but also return the extended skeleton and its best match skeleton.
 *
 * @param field           largest different calendar field
 * @param skeleton        skeleton
 * @param bestSkeleton    the best match skeleton which has interval pattern
 *                        defined in resource
 * @param differenceInfo  the difference between skeleton and best skeleton
 *         0 means the best matched skeleton is the same as input skeleton
 *         1 means the fields are the same, but field width are different
 *         2 means the only difference between fields are v/z,
 *        -1 means there are other fields difference
 *
 * @param extendedSkeleton      extended skeleton
 * @param extendedBestSkeleton  extended best match skeleton
 * @return                      whether the interval pattern is found
 *                              through extending skeleton or not.
 *                              true if interval pattern is found by
 *                              extending skeleton, false otherwise.
 * @stable ICU 4.0
 */
UBool
DateIntervalFormat::setIntervalPattern(UCalendarDateFields field,
                                       const UnicodeString* skeleton,
                                       const UnicodeString* bestSkeleton,
                                       int8_t differenceInfo,
                                       UnicodeString* extendedSkeleton,
                                       UnicodeString* extendedBestSkeleton) {
    UErrorCode status = U_ZERO_ERROR;
    // following getIntervalPattern() should not generate error status
    UnicodeString pattern;
    fInfo->getIntervalPattern(*bestSkeleton, field, pattern, status);
    if ( pattern.isEmpty() ) {
        // single date
        if ( SimpleDateFormat::isFieldUnitIgnored(*bestSkeleton, field) ) {
            // do nothing, format will handle it
            return false;
        }

        // for 24 hour system, interval patterns in resource file
        // might not include pattern when am_pm differ,
        // which should be the same as hour differ.
        // add it here for simplicity
        if ( field == UCAL_AM_PM ) {
            fInfo->getIntervalPattern(*bestSkeleton, UCAL_HOUR, pattern,status);
            if ( !pattern.isEmpty() ) {
                UBool suppressDayPeriodField = fSkeleton.indexOf(CAP_J) != -1;
                UnicodeString adjustIntervalPattern;
                adjustFieldWidth(*skeleton, *bestSkeleton, pattern, differenceInfo,
                                 suppressDayPeriodField, adjustIntervalPattern);
                setIntervalPattern(field, adjustIntervalPattern);
            }
            return false;
        }
        // else, looking for pattern when 'y' differ for 'dMMMM' skeleton,
        // first, get best match pattern "MMMd",
        // since there is no pattern for 'y' differs for skeleton 'MMMd',
        // need to look for it from skeleton 'yMMMd',
        // if found, adjust field width in interval pattern from
        // "MMM" to "MMMM".
        char16_t fieldLetter = fgCalendarFieldToPatternLetter[field];
        if ( extendedSkeleton ) {
            *extendedSkeleton = *skeleton;
            *extendedBestSkeleton = *bestSkeleton;
            extendedSkeleton->insert(0, fieldLetter);
            extendedBestSkeleton->insert(0, fieldLetter);
            // for example, looking for patterns when 'y' differ for
            // skeleton "MMMM".
            fInfo->getIntervalPattern(*extendedBestSkeleton,field,pattern,status);
            if ( pattern.isEmpty() && differenceInfo == 0 ) {
                // if there is no skeleton "yMMMM" defined,
                // look for the best match skeleton, for example: "yMMM"
                const UnicodeString* tmpBest = fInfo->getBestSkeleton(
                                        *extendedBestSkeleton, differenceInfo);
                if (tmpBest != nullptr && differenceInfo != -1) {
                    fInfo->getIntervalPattern(*tmpBest, field, pattern, status);
                    bestSkeleton = tmpBest;
                }
            }
        }
    }
    if ( !pattern.isEmpty() ) {
        UBool suppressDayPeriodField = fSkeleton.indexOf(CAP_J) != -1;
        if ( differenceInfo != 0 || suppressDayPeriodField) {
            UnicodeString adjustIntervalPattern;
            adjustFieldWidth(*skeleton, *bestSkeleton, pattern, differenceInfo,
                              suppressDayPeriodField, adjustIntervalPattern);
            setIntervalPattern(field, adjustIntervalPattern);
        } else {
            setIntervalPattern(field, pattern);
        }
        if ( extendedSkeleton && !extendedSkeleton->isEmpty() ) {
            return true;
        }
    }
    return false;
}



int32_t  U_EXPORT2
DateIntervalFormat::splitPatternInto2Part(const UnicodeString& intervalPattern) {
    UBool inQuote = false;
    char16_t prevCh = 0;
    int32_t count = 0;

    /* repeatedPattern used to record whether a pattern has already seen.
       It is a pattern applies to first calendar if it is first time seen,
       otherwise, it is a pattern applies to the second calendar
     */
    UBool patternRepeated[] =
    {
    //       A   B   C   D   E   F   G   H   I   J   K   L   M   N   O
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    //   P   Q   R   S   T   U   V   W   X   Y   Z
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0,  0, 0, 0,
    //       a   b   c   d   e   f   g   h   i   j   k   l   m   n   o
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    //   p   q   r   s   t   u   v   w   x   y   z
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
    };

    int8_t PATTERN_CHAR_BASE = 0x41;

    /* loop through the pattern string character by character looking for
     * the first repeated pattern letter, which breaks the interval pattern
     * into 2 parts.
     */
    int32_t i;
    UBool foundRepetition = false;
    for (i = 0; i < intervalPattern.length(); ++i) {
        char16_t ch = intervalPattern.charAt(i);

        if (ch != prevCh && count > 0) {
            // check the repeativeness of pattern letter
            UBool repeated = patternRepeated[prevCh - PATTERN_CHAR_BASE];
            if ( repeated == false ) {
                patternRepeated[prevCh - PATTERN_CHAR_BASE] = true;
            } else {
                foundRepetition = true;
                break;
            }
            count = 0;
        }
        if (ch == 0x0027 /*'*/) {
            // Consecutive single quotes are a single quote literal,
            // either outside of quotes or between quotes
            if ((i+1) < intervalPattern.length() &&
                intervalPattern.charAt(i+1) == 0x0027 /*'*/) {
                ++i;
            } else {
                inQuote = ! inQuote;
            }
        }
        else if (!inQuote && ((ch >= 0x0061 /*'a'*/ && ch <= 0x007A /*'z'*/)
                    || (ch >= 0x0041 /*'A'*/ && ch <= 0x005A /*'Z'*/))) {
            // ch is a date-time pattern character
            prevCh = ch;
            ++count;
        }
    }
    // check last pattern char, distinguish
    // "dd MM" ( no repetition ),
    // "d-d"(last char repeated ), and
    // "d-d MM" ( repetition found )
    if ( count > 0 && foundRepetition == false ) {
        if (patternRepeated[prevCh - PATTERN_CHAR_BASE] == false) {
            count = 0;
        }
    }
    return (i - count);
}

// The following is only called from fallbackFormat, i.e. within the gFormatterMutex lock
void DateIntervalFormat::fallbackFormatRange(
        Calendar& fromCalendar,
        Calendar& toCalendar,
        UnicodeString& appendTo,
        int8_t& firstIndex,
        FieldPositionHandler& fphandler,
        UErrorCode& status) const {
    UnicodeString fallbackPattern;
    fInfo->getFallbackIntervalPattern(fallbackPattern);
    SimpleFormatter sf(fallbackPattern, 2, 2, status);
    if (U_FAILURE(status)) {
        return;
    }
    int32_t offsets[2];
    UnicodeString patternBody = sf.getTextWithNoArguments(offsets, 2);

    UErrorCode tempStatus = U_ZERO_ERROR; // for setContext, ignored
    // TODO(ICU-20406): Use SimpleFormatter Iterator interface when available.
    if (offsets[0] < offsets[1]) {
        firstIndex = 0;
        appendTo.append(patternBody.tempSubStringBetween(0, offsets[0]));
        fDateFormat->_format(fromCalendar, appendTo, fphandler, status);
        appendTo.append(patternBody.tempSubStringBetween(offsets[0], offsets[1]));
        // No capitalization for second part of interval
        fDateFormat->setContext(UDISPCTX_CAPITALIZATION_NONE, tempStatus);
        fDateFormat->_format(toCalendar, appendTo, fphandler, status);
        appendTo.append(patternBody.tempSubStringBetween(offsets[1]));
    } else {
        firstIndex = 1;
        appendTo.append(patternBody.tempSubStringBetween(0, offsets[1]));
        fDateFormat->_format(toCalendar, appendTo, fphandler, status);
        appendTo.append(patternBody.tempSubStringBetween(offsets[1], offsets[0]));
        // No capitalization for second part of interval
        fDateFormat->setContext(UDISPCTX_CAPITALIZATION_NONE, tempStatus);
        fDateFormat->_format(fromCalendar, appendTo, fphandler, status);
        appendTo.append(patternBody.tempSubStringBetween(offsets[0]));
    }
}

// The following is only called from formatImpl, i.e. within the gFormatterMutex lock
UnicodeString&
DateIntervalFormat::fallbackFormat(Calendar& fromCalendar,
                                   Calendar& toCalendar,
                                   UBool fromToOnSameDay, // new
                                   UnicodeString& appendTo,
                                   int8_t& firstIndex,
                                   FieldPositionHandler& fphandler,
                                   UErrorCode& status) const {
    if ( U_FAILURE(status) ) {
        return appendTo;
    }

    UBool formatDatePlusTimeRange = (fromToOnSameDay && fDatePattern && fTimePattern);
    if (formatDatePlusTimeRange) {
        SimpleFormatter sf(*fDateTimeFormat, 2, 2, status);
        if (U_FAILURE(status)) {
            return appendTo;
        }
        int32_t offsets[2];
        UnicodeString patternBody = sf.getTextWithNoArguments(offsets, 2);

        UnicodeString fullPattern; // for saving the pattern in fDateFormat
        fDateFormat->toPattern(fullPattern); // save current pattern, restore later

        UErrorCode tempStatus = U_ZERO_ERROR; // for setContext, ignored
        // {0} is time range
        // {1} is single date portion
        // TODO(ICU-20406): Use SimpleFormatter Iterator interface when available.
        if (offsets[0] < offsets[1]) {
            appendTo.append(patternBody.tempSubStringBetween(0, offsets[0]));
            fDateFormat->applyPattern(*fTimePattern);
            fallbackFormatRange(fromCalendar, toCalendar, appendTo, firstIndex, fphandler, status);
            appendTo.append(patternBody.tempSubStringBetween(offsets[0], offsets[1]));
            fDateFormat->applyPattern(*fDatePattern);
            // No capitalization for second portion
            fDateFormat->setContext(UDISPCTX_CAPITALIZATION_NONE, tempStatus);
            fDateFormat->_format(fromCalendar, appendTo, fphandler, status);
            appendTo.append(patternBody.tempSubStringBetween(offsets[1]));
        } else {
            appendTo.append(patternBody.tempSubStringBetween(0, offsets[1]));
            fDateFormat->applyPattern(*fDatePattern);
            fDateFormat->_format(fromCalendar, appendTo, fphandler, status);
            appendTo.append(patternBody.tempSubStringBetween(offsets[1], offsets[0]));
            fDateFormat->applyPattern(*fTimePattern);
            // No capitalization for second portion
            fDateFormat->setContext(UDISPCTX_CAPITALIZATION_NONE, tempStatus);
            fallbackFormatRange(fromCalendar, toCalendar, appendTo, firstIndex, fphandler, status);
            appendTo.append(patternBody.tempSubStringBetween(offsets[0]));
        }

        // restore full pattern
        fDateFormat->applyPattern(fullPattern);
    } else {
        fallbackFormatRange(fromCalendar, toCalendar, appendTo, firstIndex, fphandler, status);
    }
    return appendTo;
}




UBool  U_EXPORT2
DateIntervalFormat::fieldExistsInSkeleton(UCalendarDateFields field,
                                          const UnicodeString& skeleton)
{
    const char16_t fieldChar = fgCalendarFieldToPatternLetter[field];
    return ( (skeleton.indexOf(fieldChar) == -1)?false:true ) ;
}



void  U_EXPORT2
DateIntervalFormat::adjustFieldWidth(const UnicodeString& inputSkeleton,
                 const UnicodeString& bestMatchSkeleton,
                 const UnicodeString& bestIntervalPattern,
                 int8_t differenceInfo,
                 UBool suppressDayPeriodField,
                 UnicodeString& adjustedPtn) {
    adjustedPtn = bestIntervalPattern;
    int32_t inputSkeletonFieldWidth[] =
    {
    //       A   B   C   D   E   F   G   H   I   J   K   L   M   N   O
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    //   P   Q   R   S   T   U   V   W   X   Y   Z
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0,  0, 0, 0,
    //       a   b   c   d   e   f   g   h   i   j   k   l   m   n   o
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    //   p   q   r   s   t   u   v   w   x   y   z
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
    };

    int32_t bestMatchSkeletonFieldWidth[] =
    {
    //       A   B   C   D   E   F   G   H   I   J   K   L   M   N   O
             0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    //   P   Q   R   S   T   U   V   W   X   Y   Z
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 0, 0,  0, 0, 0,
    //       a   b   c   d   e   f   g   h   i   j   k   l   m   n   o
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    //   p   q   r   s   t   u   v   w   x   y   z
         0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0
    };

    const int8_t PATTERN_CHAR_BASE = 0x41;

    DateIntervalInfo::parseSkeleton(inputSkeleton, inputSkeletonFieldWidth);
    DateIntervalInfo::parseSkeleton(bestMatchSkeleton, bestMatchSkeletonFieldWidth);
    if (suppressDayPeriodField) {
        // remove the 'a' and any NBSP/NNBSP on one side of it
        findReplaceInPattern(adjustedPtn, UnicodeString(u"\u00A0a",-1), UnicodeString());
        findReplaceInPattern(adjustedPtn, UnicodeString(u"\u202Fa",-1), UnicodeString());
        findReplaceInPattern(adjustedPtn, UnicodeString(u"a\u00A0",-1), UnicodeString());
        findReplaceInPattern(adjustedPtn, UnicodeString(u"a\u202F",-1), UnicodeString());
        findReplaceInPattern(adjustedPtn, UnicodeString(LOW_A), UnicodeString());
        // adjust interior double spaces, remove exterior whitespace
        findReplaceInPattern(adjustedPtn, UnicodeString("  "), UnicodeString(" "));
        adjustedPtn.trim();
    }
    if ( differenceInfo == 2 ) {
        if (inputSkeleton.indexOf(LOW_Z) != -1) {
             findReplaceInPattern(adjustedPtn, UnicodeString(LOW_V), UnicodeString(LOW_Z));
         }
         if (inputSkeleton.indexOf(CAP_K) != -1) {
             findReplaceInPattern(adjustedPtn, UnicodeString(LOW_H), UnicodeString(CAP_K));
         }
         if (inputSkeleton.indexOf(LOW_K) != -1) {
             findReplaceInPattern(adjustedPtn, UnicodeString(CAP_H), UnicodeString(LOW_K));
         }
         if (inputSkeleton.indexOf(LOW_B) != -1) {
             findReplaceInPattern(adjustedPtn, UnicodeString(LOW_A), UnicodeString(LOW_B));
         }
    }
    if (adjustedPtn.indexOf(LOW_A) != -1 && bestMatchSkeletonFieldWidth[LOW_A - PATTERN_CHAR_BASE] == 0) {
        bestMatchSkeletonFieldWidth[LOW_A - PATTERN_CHAR_BASE] = 1;
    }
    if (adjustedPtn.indexOf(LOW_B) != -1 && bestMatchSkeletonFieldWidth[LOW_B - PATTERN_CHAR_BASE] == 0) {
        bestMatchSkeletonFieldWidth[LOW_B - PATTERN_CHAR_BASE] = 1;
     }

    UBool inQuote = false;
    char16_t prevCh = 0;
    int32_t count = 0;

    // loop through the pattern string character by character
    int32_t adjustedPtnLength = adjustedPtn.length();
    int32_t i;
    for (i = 0; i < adjustedPtnLength; ++i) {
        char16_t ch = adjustedPtn.charAt(i);
        if (ch != prevCh && count > 0) {
            // check the repeativeness of pattern letter
            char16_t skeletonChar = prevCh;
            if ( skeletonChar ==  CAP_L ) {
                // there is no "L" (always be "M") in skeleton,
                // but there is "L" in pattern.
                // for skeleton "M+", the pattern might be "...L..."
                skeletonChar = CAP_M;
            }
            int32_t fieldCount = bestMatchSkeletonFieldWidth[skeletonChar - PATTERN_CHAR_BASE];
            int32_t inputFieldCount = inputSkeletonFieldWidth[skeletonChar - PATTERN_CHAR_BASE];
            if ( fieldCount == count && inputFieldCount > fieldCount ) {
                count = inputFieldCount - fieldCount;
                int32_t j;
                for ( j = 0; j < count; ++j ) {
                    adjustedPtn.insert(i, prevCh);
                }
                i += count;
                adjustedPtnLength += count;
            }
            count = 0;
        }
        if (ch == 0x0027 /*'*/) {
            // Consecutive single quotes are a single quote literal,
            // either outside of quotes or between quotes
            if ((i+1) < adjustedPtn.length() && adjustedPtn.charAt(i+1) == 0x0027 /* ' */) {
                ++i;
            } else {
                inQuote = ! inQuote;
            }
        }
        else if ( ! inQuote && ((ch >= 0x0061 /*'a'*/ && ch <= 0x007A /*'z'*/)
                    || (ch >= 0x0041 /*'A'*/ && ch <= 0x005A /*'Z'*/))) {
            // ch is a date-time pattern character
            prevCh = ch;
            ++count;
        }
    }
    if ( count > 0 ) {
        // last item
        // check the repeativeness of pattern letter
        char16_t skeletonChar = prevCh;
        if ( skeletonChar == CAP_L ) {
            // there is no "L" (always be "M") in skeleton,
            // but there is "L" in pattern.
            // for skeleton "M+", the pattern might be "...L..."
            skeletonChar = CAP_M;
        }
        int32_t fieldCount = bestMatchSkeletonFieldWidth[skeletonChar - PATTERN_CHAR_BASE];
        int32_t inputFieldCount = inputSkeletonFieldWidth[skeletonChar - PATTERN_CHAR_BASE];
        if ( fieldCount == count && inputFieldCount > fieldCount ) {
            count = inputFieldCount - fieldCount;
            int32_t j;
            for ( j = 0; j < count; ++j ) {
                adjustedPtn.append(prevCh);
            }
        }
    }
}

void
DateIntervalFormat::findReplaceInPattern(UnicodeString& targetString,
                                         const UnicodeString& strToReplace,
                                         const UnicodeString& strToReplaceWith) {
    int32_t firstQuoteIndex = targetString.indexOf(u'\'');
    if (firstQuoteIndex == -1) {
        targetString.findAndReplace(strToReplace, strToReplaceWith);
    } else {
        UnicodeString result;
        UnicodeString source = targetString;
        
        while (firstQuoteIndex >= 0) {
            int32_t secondQuoteIndex = source.indexOf(u'\'', firstQuoteIndex + 1);
            if (secondQuoteIndex == -1) {
                secondQuoteIndex = source.length() - 1;
            }
            
            UnicodeString unquotedText(source, 0, firstQuoteIndex);
            UnicodeString quotedText(source, firstQuoteIndex, secondQuoteIndex - firstQuoteIndex + 1);
            
            unquotedText.findAndReplace(strToReplace, strToReplaceWith);
            result += unquotedText;
            result += quotedText;
            
            source.remove(0, secondQuoteIndex + 1);
            firstQuoteIndex = source.indexOf(u'\'');
        }
        source.findAndReplace(strToReplace, strToReplaceWith);
        result += source;
        targetString = result;
    }
}



void
DateIntervalFormat::concatSingleDate2TimeInterval(UnicodeString& format,
                                              const UnicodeString& datePattern,
                                              UCalendarDateFields field,
                                              UErrorCode& status) {
    // following should not set wrong status
    int32_t itvPtnIndex = DateIntervalInfo::calendarFieldToIntervalIndex(field,
                                                                        status);
    if ( U_FAILURE(status) ) {
        return;
    }
    PatternInfo&  timeItvPtnInfo = fIntervalPatterns[itvPtnIndex];
    if ( !timeItvPtnInfo.firstPart.isEmpty() ) {
        UnicodeString timeIntervalPattern(timeItvPtnInfo.firstPart);
        timeIntervalPattern.append(timeItvPtnInfo.secondPart);
        UnicodeString combinedPattern;
        SimpleFormatter(format, 2, 2, status).
                format(timeIntervalPattern, datePattern, combinedPattern, status);
        if ( U_FAILURE(status) ) {
            return;
        }
        setIntervalPattern(field, combinedPattern, timeItvPtnInfo.laterDateFirst);
    }
    // else: fall back
    // it should not happen if the interval format defined is valid
}



const char16_t
DateIntervalFormat::fgCalendarFieldToPatternLetter[] =
{
    /*GyM*/ CAP_G, LOW_Y, CAP_M,
    /*wWd*/ LOW_W, CAP_W, LOW_D,
    /*DEF*/ CAP_D, CAP_E, CAP_F,
    /*ahH*/ LOW_A, LOW_H, CAP_H,
    /*msS*/ LOW_M, LOW_S, CAP_S, // MINUTE, SECOND, MILLISECOND
    /*z.Y*/ LOW_Z, SPACE, CAP_Y, // ZONE_OFFSET, DST_OFFSET, YEAR_WOY,
    /*eug*/ LOW_E, LOW_U, LOW_G, // DOW_LOCAL, EXTENDED_YEAR, JULIAN_DAY,
    /*A..*/ CAP_A, SPACE, SPACE, // MILLISECONDS_IN_DAY, IS_LEAP_MONTH, FIELD_COUNT
};



U_NAMESPACE_END

#endif
