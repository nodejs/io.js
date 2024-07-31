// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2007-2013, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/basictz.h"
#include "gregoimp.h"
#include "uvector.h"
#include "cmemory.h"

U_NAMESPACE_BEGIN

#define MILLIS_PER_YEAR (365*24*60*60*1000.0)

BasicTimeZone::BasicTimeZone()
: TimeZone() {
}

BasicTimeZone::BasicTimeZone(const UnicodeString &id)
: TimeZone(id) {
}

BasicTimeZone::BasicTimeZone(const BasicTimeZone& source)
: TimeZone(source) {
}

BasicTimeZone::~BasicTimeZone() {
}

UBool
BasicTimeZone::hasEquivalentTransitions(const BasicTimeZone& tz, UDate start, UDate end,
                                        UBool ignoreDstAmount, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return false;
    }
    if (hasSameRules(tz)) {
        return true;
    }
    // Check the offsets at the start time
    int32_t raw1, raw2, dst1, dst2;
    getOffset(start, false, raw1, dst1, status);
    if (U_FAILURE(status)) {
        return false;
    }
    tz.getOffset(start, false, raw2, dst2, status);
    if (U_FAILURE(status)) {
        return false;
    }
    if (ignoreDstAmount) {
        if ((raw1 + dst1 != raw2 + dst2)
            || (dst1 != 0 && dst2 == 0)
            || (dst1 == 0 && dst2 != 0)) {
            return false;
        }
    } else {
        if (raw1 != raw2 || dst1 != dst2) {
            return false;
        }            
    }
    // Check transitions in the range
    UDate time = start;
    TimeZoneTransition tr1, tr2;
    while (true) {
        UBool avail1 = getNextTransition(time, false, tr1);
        UBool avail2 = tz.getNextTransition(time, false, tr2);

        if (ignoreDstAmount) {
            // Skip a transition which only differ the amount of DST savings
            while (true) {
                if (avail1
                        && tr1.getTime() <= end
                        && (tr1.getFrom()->getRawOffset() + tr1.getFrom()->getDSTSavings()
                                == tr1.getTo()->getRawOffset() + tr1.getTo()->getDSTSavings())
                        && (tr1.getFrom()->getDSTSavings() != 0 && tr1.getTo()->getDSTSavings() != 0)) {
                    getNextTransition(tr1.getTime(), false, tr1);
                } else {
                    break;
                }
            }
            while (true) {
                if (avail2
                        && tr2.getTime() <= end
                        && (tr2.getFrom()->getRawOffset() + tr2.getFrom()->getDSTSavings()
                                == tr2.getTo()->getRawOffset() + tr2.getTo()->getDSTSavings())
                        && (tr2.getFrom()->getDSTSavings() != 0 && tr2.getTo()->getDSTSavings() != 0)) {
                    tz.getNextTransition(tr2.getTime(), false, tr2);
                } else {
                    break;
                }
            }
        }

        UBool inRange1 = (avail1 && tr1.getTime() <= end);
        UBool inRange2 = (avail2 && tr2.getTime() <= end);
        if (!inRange1 && !inRange2) {
            // No more transition in the range
            break;
        }
        if (!inRange1 || !inRange2) {
            return false;
        }
        if (tr1.getTime() != tr2.getTime()) {
            return false;
        }
        if (ignoreDstAmount) {
            if (tr1.getTo()->getRawOffset() + tr1.getTo()->getDSTSavings()
                        != tr2.getTo()->getRawOffset() + tr2.getTo()->getDSTSavings()
                    || (tr1.getTo()->getDSTSavings() != 0 &&  tr2.getTo()->getDSTSavings() == 0)
                    || (tr1.getTo()->getDSTSavings() == 0 &&  tr2.getTo()->getDSTSavings() != 0)) {
                return false;
            }
        } else {
            if (tr1.getTo()->getRawOffset() != tr2.getTo()->getRawOffset() ||
                tr1.getTo()->getDSTSavings() != tr2.getTo()->getDSTSavings()) {
                return false;
            }
        }
        time = tr1.getTime();
    }
    return true;
}

void
BasicTimeZone::getSimpleRulesNear(UDate date, InitialTimeZoneRule*& initial,
        AnnualTimeZoneRule*& std, AnnualTimeZoneRule*& dst, UErrorCode& status) const {
    initial = nullptr;
    std = nullptr;
    dst = nullptr;
    if (U_FAILURE(status)) {
        return;
    }
    int32_t initialRaw, initialDst;
    UnicodeString initialName;

    AnnualTimeZoneRule *ar1 = nullptr;
    AnnualTimeZoneRule *ar2 = nullptr;
    UnicodeString name;

    UBool avail;
    TimeZoneTransition tr;
    // Get the next transition
    avail = getNextTransition(date, false, tr);
    if (avail) {
        tr.getFrom()->getName(initialName);
        initialRaw = tr.getFrom()->getRawOffset();
        initialDst = tr.getFrom()->getDSTSavings();

        // Check if the next transition is either DST->STD or STD->DST and
        // within roughly 1 year from the specified date
        UDate nextTransitionTime = tr.getTime();
        if (((tr.getFrom()->getDSTSavings() == 0 && tr.getTo()->getDSTSavings() != 0)
              || (tr.getFrom()->getDSTSavings() != 0 && tr.getTo()->getDSTSavings() == 0))
            && (date + MILLIS_PER_YEAR > nextTransitionTime)) {
 
            int32_t year, month, dom, dow, doy, mid;
            UDate d;

            // Get local wall time for the next transition time
            Grego::timeToFields(nextTransitionTime + initialRaw + initialDst,
                year, month, dom, dow, doy, mid);
            int32_t weekInMonth = Grego::dayOfWeekInMonth(year, month, dom);
            // Create DOW rule
            DateTimeRule *dtr = new DateTimeRule(month, weekInMonth, dow, mid, DateTimeRule::WALL_TIME);
            tr.getTo()->getName(name);

            // Note:  SimpleTimeZone does not support raw offset change.
            // So we always use raw offset of the given time for the rule,
            // even raw offset is changed.  This will result that the result
            // zone to return wrong offset after the transition.
            // When we encounter such case, we do not inspect next next
            // transition for another rule.
            ar1 = new AnnualTimeZoneRule(name, initialRaw, tr.getTo()->getDSTSavings(),
                dtr, year, AnnualTimeZoneRule::MAX_YEAR);

            if (tr.getTo()->getRawOffset() == initialRaw) {
                // Get the next next transition
                avail = getNextTransition(nextTransitionTime, false, tr);
                if (avail) {
                    // Check if the next next transition is either DST->STD or STD->DST
                    // and within roughly 1 year from the next transition
                    if (((tr.getFrom()->getDSTSavings() == 0 && tr.getTo()->getDSTSavings() != 0)
                          || (tr.getFrom()->getDSTSavings() != 0 && tr.getTo()->getDSTSavings() == 0))
                         && nextTransitionTime + MILLIS_PER_YEAR > tr.getTime()) {

                        // Get local wall time for the next transition time
                        Grego::timeToFields(tr.getTime() + tr.getFrom()->getRawOffset() + tr.getFrom()->getDSTSavings(),
                            year, month, dom, dow, doy, mid);
                        weekInMonth = Grego::dayOfWeekInMonth(year, month, dom);
                        // Generate another DOW rule
                        dtr = new DateTimeRule(month, weekInMonth, dow, mid, DateTimeRule::WALL_TIME);
                        tr.getTo()->getName(name);
                        ar2 = new AnnualTimeZoneRule(name, tr.getTo()->getRawOffset(), tr.getTo()->getDSTSavings(),
                            dtr, year - 1, AnnualTimeZoneRule::MAX_YEAR);

                        // Make sure this rule can be applied to the specified date
                        avail = ar2->getPreviousStart(date, tr.getFrom()->getRawOffset(), tr.getFrom()->getDSTSavings(), true, d);
                        if (!avail || d > date
                                || initialRaw != tr.getTo()->getRawOffset()
                                || initialDst != tr.getTo()->getDSTSavings()) {
                            // We cannot use this rule as the second transition rule
                            delete ar2;
                            ar2 = nullptr;
                        }
                    }
                }
            }
            if (ar2 == nullptr) {
                // Try previous transition
                avail = getPreviousTransition(date, true, tr);
                if (avail) {
                    // Check if the previous transition is either DST->STD or STD->DST.
                    // The actual transition time does not matter here.
                    if ((tr.getFrom()->getDSTSavings() == 0 && tr.getTo()->getDSTSavings() != 0)
                        || (tr.getFrom()->getDSTSavings() != 0 && tr.getTo()->getDSTSavings() == 0)) {

                        // Generate another DOW rule
                        Grego::timeToFields(tr.getTime() + tr.getFrom()->getRawOffset() + tr.getFrom()->getDSTSavings(),
                            year, month, dom, dow, doy, mid);
                        weekInMonth = Grego::dayOfWeekInMonth(year, month, dom);
                        dtr = new DateTimeRule(month, weekInMonth, dow, mid, DateTimeRule::WALL_TIME);
                        tr.getTo()->getName(name);

                        // second rule raw/dst offsets should match raw/dst offsets
                        // at the given time
                        ar2 = new AnnualTimeZoneRule(name, initialRaw, initialDst,
                            dtr, ar1->getStartYear() - 1, AnnualTimeZoneRule::MAX_YEAR);

                        // Check if this rule start after the first rule after the specified date
                        avail = ar2->getNextStart(date, tr.getFrom()->getRawOffset(), tr.getFrom()->getDSTSavings(), false, d);
                        if (!avail || d <= nextTransitionTime) {
                            // We cannot use this rule as the second transition rule
                            delete ar2;
                            ar2 = nullptr;
                        }
                    }
                }
            }
            if (ar2 == nullptr) {
                // Cannot find a good pair of AnnualTimeZoneRule
                delete ar1;
                ar1 = nullptr;
            } else {
                // The initial rule should represent the rule before the previous transition
                ar1->getName(initialName);
                initialRaw = ar1->getRawOffset();
                initialDst = ar1->getDSTSavings();
            }
        }
    }
    else {
        // Try the previous one
        avail = getPreviousTransition(date, true, tr);
        if (avail) {
            tr.getTo()->getName(initialName);
            initialRaw = tr.getTo()->getRawOffset();
            initialDst = tr.getTo()->getDSTSavings();
        } else {
            // No transitions in the past.  Just use the current offsets
            getOffset(date, false, initialRaw, initialDst, status);
            if (U_FAILURE(status)) {
                return;
            }
        }
    }
    // Set the initial rule
    initial = new InitialTimeZoneRule(initialName, initialRaw, initialDst);

    // Set the standard and daylight saving rules
    if (ar1 != nullptr && ar2 != nullptr) {
        if (ar1->getDSTSavings() != 0) {
            dst = ar1;
            std = ar2;
        } else {
            std = ar1;
            dst = ar2;
        }
    }
}

void
BasicTimeZone::getTimeZoneRulesAfter(UDate start, InitialTimeZoneRule*& initial,
                                     UVector*& transitionRules, UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }

    const InitialTimeZoneRule *orgini;
    TimeZoneTransition tzt;
    bool avail;
    int32_t ruleCount;
    TimeZoneRule *r = nullptr;
    UnicodeString name;
    int32_t i;
    UDate time, t;
    UDate firstStart;
    UBool bFinalStd = false, bFinalDst = false;

    initial = nullptr;
    transitionRules = nullptr;

    // Original transition rules
    ruleCount = countTransitionRules(status);
    if (U_FAILURE(status)) {
        return;
    }
    LocalPointer<UVector> orgRules(
        new UVector(uprv_deleteUObject, nullptr, ruleCount, status), status);
    if (U_FAILURE(status)) {
        return;
    }
    LocalMemory<const TimeZoneRule *> orgtrs(
        static_cast<const TimeZoneRule **>(uprv_malloc(sizeof(TimeZoneRule*)*ruleCount)));
    if (orgtrs.isNull()) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    getTimeZoneRules(orgini, &orgtrs[0], ruleCount, status);
    if (U_FAILURE(status)) {
        return;
    }
    for (i = 0; i < ruleCount; i++) {
        LocalPointer<TimeZoneRule> lpRule(orgtrs[i]->clone(), status);
        orgRules->adoptElement(lpRule.orphan(), status);
        if (U_FAILURE(status)) {
            return;
        }
    }

    avail = getPreviousTransition(start, true, tzt);
    if (!avail) {
        // No need to filter out rules only applicable to time before the start
        initial = orgini->clone();
        if (initial == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return;
        }
        transitionRules = orgRules.orphan();
        return;
    }

    LocalMemory<bool> done(static_cast<bool *>(uprv_malloc(sizeof(bool)*ruleCount)));
    if (done.isNull()) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return;
    }
    LocalPointer<UVector> filteredRules(
        new UVector(uprv_deleteUObject, nullptr, status), status);
    if (U_FAILURE(status)) {
        return;
    }

    // Create initial rule
    tzt.getTo()->getName(name);
    LocalPointer<InitialTimeZoneRule> res_initial(
        new InitialTimeZoneRule(name, tzt.getTo()->getRawOffset(), tzt.getTo()->getDSTSavings()), status);
    if (U_FAILURE(status)) {
        return;
    }

    // Mark rules which does not need to be processed
    for (i = 0; i < ruleCount; i++) {
        r = (TimeZoneRule*)orgRules->elementAt(i);
        avail = r->getNextStart(start, res_initial->getRawOffset(), res_initial->getDSTSavings(), false, time);
        done[i] = !avail;
    }

    time = start;
    while (!bFinalStd || !bFinalDst) {
        avail = getNextTransition(time, false, tzt);
        if (!avail) {
            break;
        }
        UDate updatedTime = tzt.getTime();
        if (updatedTime == time) {
            // Can get here if rules for start & end of daylight time have exactly
            // the same time.  
            // TODO:  fix getNextTransition() to prevent it?
            status = U_INVALID_STATE_ERROR;
            return;
        }
        time = updatedTime;
 
        const TimeZoneRule *toRule = tzt.getTo();
        for (i = 0; i < ruleCount; i++) {
            r = (TimeZoneRule*)orgRules->elementAt(i);
            if (*r == *toRule) {
                break;
            }
        }
        if (i >= ruleCount) {
            // This case should never happen
            status = U_INVALID_STATE_ERROR;
            return;
        }
        if (done[i]) {
            continue;
        }
        const TimeArrayTimeZoneRule *tar = dynamic_cast<const TimeArrayTimeZoneRule *>(toRule);
        const AnnualTimeZoneRule *ar;
        if (tar != nullptr) {
            // Get the previous raw offset and DST savings before the very first start time
            TimeZoneTransition tzt0;
            t = start;
            while (true) {
                avail = getNextTransition(t, false, tzt0);
                if (!avail) {
                    break;
                }
                if (*(tzt0.getTo()) == *tar) {
                    break;
                }
                t = tzt0.getTime();
            }
            if (avail) {
                // Check if the entire start times to be added
                tar->getFirstStart(tzt.getFrom()->getRawOffset(), tzt.getFrom()->getDSTSavings(), firstStart);
                if (firstStart > start) {
                    // Just add the rule as is
                    LocalPointer<TimeArrayTimeZoneRule> lpTar(tar->clone(), status);
                    filteredRules->adoptElement(lpTar.orphan(), status);
                    if (U_FAILURE(status)) {
                        return;
                    }
                } else {
                    // Collect transitions after the start time
                    int32_t startTimes;
                    DateTimeRule::TimeRuleType timeType;
                    int32_t idx;

                    startTimes = tar->countStartTimes();
                    timeType = tar->getTimeType();
                    for (idx = 0; idx < startTimes; idx++) {
                        tar->getStartTimeAt(idx, t);
                        if (timeType == DateTimeRule::STANDARD_TIME) {
                            t -= tzt.getFrom()->getRawOffset();
                        }
                        if (timeType == DateTimeRule::WALL_TIME) {
                            t -= tzt.getFrom()->getDSTSavings();
                        }
                        if (t > start) {
                            break;
                        }
                    }
                    if (U_FAILURE(status)) {
                        return;
                    }
                    int32_t asize = startTimes - idx;
                    if (asize > 0) {
                        LocalMemory<UDate> newTimes(static_cast<UDate *>(uprv_malloc(sizeof(UDate) * asize)));
                        if (newTimes.isNull()) {
                            status = U_MEMORY_ALLOCATION_ERROR;
                            return;
                        }
                        for (int32_t newidx = 0; newidx < asize; newidx++) {
                            tar->getStartTimeAt(idx + newidx, newTimes[newidx]);
                        }
                        tar->getName(name);
                        LocalPointer<TimeArrayTimeZoneRule> newTar(new TimeArrayTimeZoneRule(
                                name, tar->getRawOffset(), tar->getDSTSavings(), &newTimes[0], asize, timeType), status);
                        filteredRules->adoptElement(newTar.orphan(), status);
                        if (U_FAILURE(status)) {
                            return;
                        }
                    }
                }
            }
        } else if ((ar = dynamic_cast<const AnnualTimeZoneRule *>(toRule)) != nullptr) {
            ar->getFirstStart(tzt.getFrom()->getRawOffset(), tzt.getFrom()->getDSTSavings(), firstStart);
            if (firstStart == tzt.getTime()) {
                // Just add the rule as is
                LocalPointer<AnnualTimeZoneRule> arClone(ar->clone(), status);
                filteredRules->adoptElement(arClone.orphan(), status);
                if (U_FAILURE(status)) {
                    return;
                }
            } else {
                // Calculate the transition year
                int32_t year, month, dom, dow, doy, mid;
                Grego::timeToFields(tzt.getTime(), year, month, dom, dow, doy, mid);
                // Re-create the rule
                ar->getName(name);
                LocalPointer<AnnualTimeZoneRule> newAr(new AnnualTimeZoneRule(name, ar->getRawOffset(), ar->getDSTSavings(),
                    *(ar->getRule()), year, ar->getEndYear()), status);
                filteredRules->adoptElement(newAr.orphan(), status);
                if (U_FAILURE(status)) {
                    return;
                }
            }
            // check if this is a final rule
            if (ar->getEndYear() == AnnualTimeZoneRule::MAX_YEAR) {
                // After bot final standard and dst rules are processed,
                // exit this while loop.
                if (ar->getDSTSavings() == 0) {
                    bFinalStd = true;
                } else {
                    bFinalDst = true;
                }
            }
        }
        done[i] = true;
    }

    // Set the results
    initial = res_initial.orphan();
    transitionRules = filteredRules.orphan();
}

void
BasicTimeZone::getOffsetFromLocal(UDate /*date*/, UTimeZoneLocalOption /*nonExistingTimeOpt*/,
                                  UTimeZoneLocalOption /*duplicatedTimeOpt*/,
                                  int32_t& /*rawOffset*/, int32_t& /*dstOffset*/,
                                  UErrorCode& status) const {
    if (U_FAILURE(status)) {
        return;
    }
    status = U_UNSUPPORTED_ERROR;
}

void BasicTimeZone::getOffsetFromLocal(UDate date, int32_t nonExistingTimeOpt, int32_t duplicatedTimeOpt,
                                       int32_t& rawOffset, int32_t& dstOffset,
                                       UErrorCode& status) const {
    getOffsetFromLocal(date, (UTimeZoneLocalOption)nonExistingTimeOpt,
                       (UTimeZoneLocalOption)duplicatedTimeOpt, rawOffset, dstOffset, status);
}

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
