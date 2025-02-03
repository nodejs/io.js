// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
* Copyright (c) 2003-2013, International Business Machines
* Corporation and others.  All Rights Reserved.
**********************************************************************
* Author: Alan Liu
* Created: July 21 2003
* Since: ICU 2.8
**********************************************************************
*/
#ifndef OLSONTZ_H
#define OLSONTZ_H

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/basictz.h"
#include "umutex.h"

struct UResourceBundle;

U_NAMESPACE_BEGIN

class SimpleTimeZone;

/**
 * A time zone based on the Olson tz database.  Olson time zones change
 * behavior over time.  The raw offset, rules, presence or absence of
 * daylight savings time, and even the daylight savings amount can all
 * vary.
 *
 * This class uses a resource bundle named "zoneinfo".  Zoneinfo is a
 * table containing different kinds of resources.  In several places,
 * zones are referred to using integers.  A zone's integer is a number
 * from 0..n-1, where n is the number of zones, with the zones sorted
 * in lexicographic order.
 *
 * 1. Zones.  These have keys corresponding to the Olson IDs, e.g.,
 * "Asia/Shanghai".  Each resource describes the behavior of the given
 * zone.  Zones come in two different formats.
 *
 *   a. Zone (table).  A zone is a table resource contains several
 *   type of resources below:
 *  
 *   - typeOffsets:intvector (Required)
 *  
 *   Sets of UTC raw/dst offset pairs in seconds.  Entries at
 *   2n represents raw offset and 2n+1 represents dst offset
 *   paired with the raw offset at 2n.  The very first pair represents
 *   the initial zone offset (before the first transition) always.
 *
 *   - trans:intvector (Optional) 
 *  
 *   List of transition times represented by 32bit seconds from the
 *   epoch (1970-01-01T00:00Z) in ascending order.
 *  
 *   - transPre32/transPost32:intvector (Optional)
 *  
 *   List of transition times before/after 32bit minimum seconds.
 *   Each time is represented by a pair of 32bit integer.
 * 
 *   - typeMap:bin (Optional)
 *  
 *   Array of bytes representing the mapping between each transition
 *   time (transPre32/trans/transPost32) and its corresponding offset
 *   data (typeOffsets).
 *  
 *   - finalRule:string (Optional)
 *  
 *   If a recurrent transition rule is applicable to a zone forever
 *   after the final transition time, finalRule represents the rule
 *   in Rules data.
 *  
 *   - finalRaw:int (Optional)
 *   
 *   When finalRule is available, finalRaw is required and specifies
 *   the raw (base) offset of the rule.
 *   
 *   - finalYear:int (Optional)
 *   
 *   When finalRule is available, finalYear is required and specifies
 *   the start year of the rule.
 *   
 *   - links:intvector (Optional)
 *   
 *   When this zone data is shared with other zones, links specifies
 *   all zones including the zone itself.  Each zone is referenced by
 *   integer index.
 * 
 *  b. Link (int, length 1).  A link zone is an int resource.  The
 *  integer is the zone number of the target zone.  The key of this
 *  resource is an alternate name for the target zone.  This data
 *  is corresponding to Link data in the tz database.
 *
 *
 * 2. Rules.  These have keys corresponding to the Olson rule IDs,
 * with an underscore prepended, e.g., "_EU".  Each resource describes
 * the behavior of the given rule using an intvector, containing the
 * onset list, the cessation list, and the DST savings.  The onset and
 * cessation lists consist of the month, dowim, dow, time, and time
 * mode.  The end result is that the 11 integers describing the rule
 * can be passed directly into the SimpleTimeZone 13-argument
 * constructor (the other two arguments will be the raw offset, taken
 * from the complex zone element 5, and the ID string, which is not
 * used), with the times and the DST savings multiplied by 1000 to
 * scale from seconds to milliseconds.
 *
 * 3. Regions.  An array specifies mapping between zones and regions.
 * Each item is either a 2-letter ISO country code or "001"
 * (UN M.49 - World).  This data is generated from "zone.tab"
 * in the tz database.
 */
class U_I18N_API OlsonTimeZone: public BasicTimeZone {
 public:
    /**
     * Construct from a resource bundle.
     * @param top the top-level zoneinfo resource bundle.  This is used
     * to lookup the rule that `res' may refer to, if there is one.
     * @param res the resource bundle of the zone to be constructed
     * @param tzid the time zone ID
     * @param ec input-output error code
     */
    OlsonTimeZone(const UResourceBundle* top,
                  const UResourceBundle* res,
                  const UnicodeString& tzid,
                  UErrorCode& ec);

    /**
     * Copy constructor
     */
    OlsonTimeZone(const OlsonTimeZone& other);

    /**
     * Destructor
     */
    virtual ~OlsonTimeZone();

    /**
     * Assignment operator
     */
    OlsonTimeZone& operator=(const OlsonTimeZone& other);

    /**
     * Returns true if the two TimeZone objects are equal.
     */
    virtual bool operator==(const TimeZone& other) const override;

    /**
     * TimeZone API.
     */
    virtual OlsonTimeZone* clone() const override;

    /**
     * TimeZone API.
     */
    static UClassID U_EXPORT2 getStaticClassID();

    /**
     * TimeZone API.
     */
    virtual UClassID getDynamicClassID() const override;
    
    /**
     * TimeZone API.  Do not call this; prefer getOffset(UDate,...).
     */
    virtual int32_t getOffset(uint8_t era, int32_t year, int32_t month,
                              int32_t day, uint8_t dayOfWeek,
                              int32_t millis, UErrorCode& ec) const override;

    /**
     * TimeZone API.  Do not call this; prefer getOffset(UDate,...).
     */
    virtual int32_t getOffset(uint8_t era, int32_t year, int32_t month,
                              int32_t day, uint8_t dayOfWeek,
                              int32_t millis, int32_t monthLength,
                              UErrorCode& ec) const override;

    /**
     * TimeZone API.
     */
    virtual void getOffset(UDate date, UBool local, int32_t& rawOffset,
                   int32_t& dstOffset, UErrorCode& ec) const override;

    /**
     * BasicTimeZone API.
     */
    virtual void getOffsetFromLocal(
        UDate date, UTimeZoneLocalOption nonExistingTimeOpt,
        UTimeZoneLocalOption duplicatedTimeOpt,
        int32_t& rawOffset, int32_t& dstOffset, UErrorCode& status) const override;

    /**
     * TimeZone API.  This method has no effect since objects of this
     * class are quasi-immutable (the base class allows the ID to be
     * changed).
     */
    virtual void setRawOffset(int32_t offsetMillis) override;

    /**
     * TimeZone API.  For a historical zone, the raw offset can change
     * over time, so this API is not useful.  In order to approximate
     * expected behavior, this method returns the raw offset for the
     * current moment in time.
     */
    virtual int32_t getRawOffset() const override;

    /**
     * TimeZone API.  For a historical zone, whether DST is used or
     * not varies over time.  In order to approximate expected
     * behavior, this method returns true if DST is observed at any
     * point in the current year.
     */
    virtual UBool useDaylightTime() const override;

    /**
     * TimeZone API.
     */
    virtual UBool inDaylightTime(UDate date, UErrorCode& ec) const override;

    /**
     * TimeZone API.
     */
    virtual int32_t getDSTSavings() const override;

    /**
     * TimeZone API.  Also comare historic transitions.
     */
    virtual UBool hasSameRules(const TimeZone& other) const override;

    /**
     * BasicTimeZone API.
     * Gets the first time zone transition after the base time.
     * @param base      The base time.
     * @param inclusive Whether the base time is inclusive or not.
     * @param result    Receives the first transition after the base time.
     * @return  true if the transition is found.
     */
    virtual UBool getNextTransition(UDate base, UBool inclusive, TimeZoneTransition& result) const override;

    /**
     * BasicTimeZone API.
     * Gets the most recent time zone transition before the base time.
     * @param base      The base time.
     * @param inclusive Whether the base time is inclusive or not.
     * @param result    Receives the most recent transition before the base time.
     * @return  true if the transition is found.
     */
    virtual UBool getPreviousTransition(UDate base, UBool inclusive, TimeZoneTransition& result) const override;

    /**
     * BasicTimeZone API.
     * Returns the number of <code>TimeZoneRule</code>s which represents time transitions,
     * for this time zone, that is, all <code>TimeZoneRule</code>s for this time zone except
     * <code>InitialTimeZoneRule</code>.  The return value range is 0 or any positive value.
     * @param status    Receives error status code.
     * @return The number of <code>TimeZoneRule</code>s representing time transitions.
     */
    virtual int32_t countTransitionRules(UErrorCode& status) const override;

    /**
     * Gets the <code>InitialTimeZoneRule</code> and the set of <code>TimeZoneRule</code>
     * which represent time transitions for this time zone.  On successful return,
     * the argument initial points to non-nullptr <code>InitialTimeZoneRule</code> and
     * the array trsrules is filled with 0 or multiple <code>TimeZoneRule</code>
     * instances up to the size specified by trscount.  The results are referencing the
     * rule instance held by this time zone instance.  Therefore, after this time zone
     * is destructed, they are no longer available.
     * @param initial       Receives the initial timezone rule
     * @param trsrules      Receives the timezone transition rules
     * @param trscount      On input, specify the size of the array 'transitions' receiving
     *                      the timezone transition rules.  On output, actual number of
     *                      rules filled in the array will be set.
     * @param status        Receives error status code.
     */
    virtual void getTimeZoneRules(const InitialTimeZoneRule*& initial,
        const TimeZoneRule* trsrules[], int32_t& trscount, UErrorCode& status) const override;

    /**
     * Internal API returning the canonical ID of this zone.
     * This ID won't be affected by setID().
     */
    const char16_t *getCanonicalID() const;

private:
    /**
     * Default constructor.  Creates a time zone with an empty ID and
     * a fixed GMT offset of zero.
     */
    OlsonTimeZone();

private:

    void constructEmpty();

    void getHistoricalOffset(UDate date, UBool local,
        int32_t NonExistingTimeOpt, int32_t DuplicatedTimeOpt,
        int32_t& rawoff, int32_t& dstoff) const;

    int16_t transitionCount() const;

    int64_t transitionTimeInSeconds(int16_t transIdx) const;
    double transitionTime(int16_t transIdx) const;

    /*
     * Following 3 methods return an offset at the given transition time index.
     * When the index is negative, return the initial offset.
     */
    int32_t zoneOffsetAt(int16_t transIdx) const;
    int32_t rawOffsetAt(int16_t transIdx) const;
    int32_t dstOffsetAt(int16_t transIdx) const;

    /*
     * Following methods return the initial offset.
     */
    int32_t initialRawOffset() const;
    int32_t initialDstOffset() const;

    /**
     * Number of transitions in each time range
     */
    int16_t transitionCountPre32;
    int16_t transitionCount32;
    int16_t transitionCountPost32;

    /**
     * Time of each transition in seconds from 1970 epoch before 32bit second range (<= 1900).
     * Each transition in this range is represented by a pair of int32_t.
     * Length is transitionCount int32_t's.  nullptr if no transitions in this range.
     */
    const int32_t *transitionTimesPre32; // alias into res; do not delete

    /**
     * Time of each transition in seconds from 1970 epoch in 32bit second range.
     * Length is transitionCount int32_t's.  nullptr if no transitions in this range.
     */
    const int32_t *transitionTimes32; // alias into res; do not delete

    /**
     * Time of each transition in seconds from 1970 epoch after 32bit second range (>= 2038).
     * Each transition in this range is represented by a pair of int32_t.
     * Length is transitionCount int32_t's.  nullptr if no transitions in this range.
     */
    const int32_t *transitionTimesPost32; // alias into res; do not delete

    /**
     * Number of types, 1..255
     */
    int16_t typeCount;

    /**
     * Offset from GMT in seconds for each type.
     * Length is typeCount int32_t's.  At least one type (a pair of int32_t)
     * is required.
     */
    const int32_t *typeOffsets; // alias into res; do not delete

    /**
     * Type description data, consisting of transitionCount uint8_t
     * type indices (from 0..typeCount-1).
     * Length is transitionCount int16_t's.  nullptr if no transitions.
     */
    const uint8_t *typeMapData; // alias into res; do not delete

    /**
     * A SimpleTimeZone that governs the behavior for date >= finalMillis.
     */
    SimpleTimeZone *finalZone; // owned, may be nullptr

    /**
     * For date >= finalMillis, the finalZone will be used.
     */
    double finalStartMillis;

    /**
     * For year >= finalYear, the finalZone will be used.
     */
    int32_t finalStartYear;

    /*
     * Canonical (CLDR) ID of this zone
     */
    const char16_t *canonicalID;

    /* BasicTimeZone support */
    void clearTransitionRules();
    void deleteTransitionRules();
    void checkTransitionRules(UErrorCode& status) const;

  public:    // Internal, for access from plain C code
    void initTransitionRules(UErrorCode& status);
  private:

    InitialTimeZoneRule *initialRule;
    TimeZoneTransition  *firstTZTransition;
    int16_t             firstTZTransitionIdx;
    TimeZoneTransition  *firstFinalTZTransition;
    TimeArrayTimeZoneRule   **historicRules;
    int16_t             historicRuleCount;
    SimpleTimeZone      *finalZoneWithStartYear; // hack
    UInitOnce           transitionRulesInitOnce {};
};

inline int16_t
OlsonTimeZone::transitionCount() const {
    return transitionCountPre32 + transitionCount32 + transitionCountPost32;
}

inline double
OlsonTimeZone::transitionTime(int16_t transIdx) const {
    return static_cast<double>(transitionTimeInSeconds(transIdx)) * U_MILLIS_PER_SECOND;
}

inline int32_t
OlsonTimeZone::zoneOffsetAt(int16_t transIdx) const {
    int16_t typeIdx = (transIdx >= 0 ? typeMapData[transIdx] : 0) << 1;
    return typeOffsets[typeIdx] + typeOffsets[typeIdx + 1];
}

inline int32_t
OlsonTimeZone::rawOffsetAt(int16_t transIdx) const {
    int16_t typeIdx = (transIdx >= 0 ? typeMapData[transIdx] : 0) << 1;
    return typeOffsets[typeIdx];
}

inline int32_t
OlsonTimeZone::dstOffsetAt(int16_t transIdx) const {
    int16_t typeIdx = (transIdx >= 0 ? typeMapData[transIdx] : 0) << 1;
    return typeOffsets[typeIdx + 1];
}

inline int32_t
OlsonTimeZone::initialRawOffset() const {
    return typeOffsets[0];
}

inline int32_t
OlsonTimeZone::initialDstOffset() const {
    return typeOffsets[1];
}

inline const char16_t*
OlsonTimeZone::getCanonicalID() const {
    return canonicalID;
}


U_NAMESPACE_END

#endif // !UCONFIG_NO_FORMATTING
#endif // OLSONTZ_H

//eof
