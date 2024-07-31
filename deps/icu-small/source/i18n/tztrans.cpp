// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2007-2012, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*/

#include "utypeinfo.h"  // for 'typeid' to work

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/tzrule.h"
#include "unicode/tztrans.h"

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(TimeZoneTransition)

TimeZoneTransition::TimeZoneTransition(UDate time, const TimeZoneRule& from, const TimeZoneRule& to)
: UObject(), fTime(time), fFrom(from.clone()), fTo(to.clone()) {
}

TimeZoneTransition::TimeZoneTransition()
: UObject(), fTime(0), fFrom(nullptr), fTo(nullptr) {
}

TimeZoneTransition::TimeZoneTransition(const TimeZoneTransition& source)
: UObject(), fTime(source.fTime), fFrom(nullptr), fTo(nullptr) {
      if (source.fFrom != nullptr) {
          fFrom = source.fFrom->clone();
      }

      if (source.fTo != nullptr) {
          fTo = source.fTo->clone();
      }
}

TimeZoneTransition::~TimeZoneTransition() {
    delete fFrom;
    delete fTo;
}

TimeZoneTransition*
TimeZoneTransition::clone() const {
    return new TimeZoneTransition(*this);
}

TimeZoneTransition&
TimeZoneTransition::operator=(const TimeZoneTransition& right) {
    if (this != &right) {
        fTime = right.fTime;
        setFrom(*right.fFrom);
        setTo(*right.fTo);
    }
    return *this;
}

bool
TimeZoneTransition::operator==(const TimeZoneTransition& that) const {
    if (this == &that) {
        return true;
    }
    if (typeid(*this) != typeid(that)) {
        return false;
    }
    if (fTime != that.fTime) {
        return false;
    }
    if ((fFrom == nullptr && that.fFrom == nullptr)
        || (fFrom != nullptr && that.fFrom != nullptr && *fFrom == *(that.fFrom))) {
        if ((fTo == nullptr && that.fTo == nullptr)
            || (fTo != nullptr && that.fTo != nullptr && *fTo == *(that.fTo))) {
            return true;
        }
    }
    return false;
}

bool
TimeZoneTransition::operator!=(const TimeZoneTransition& that) const {
    return !operator==(that);
}

void
TimeZoneTransition::setTime(UDate time) {
    fTime = time;
}

void
TimeZoneTransition::setFrom(const TimeZoneRule& from) {
    delete fFrom;
    fFrom = from.clone();
}

void
TimeZoneTransition::adoptFrom(TimeZoneRule* from) {
    delete fFrom;
    fFrom = from;
}

void
TimeZoneTransition::setTo(const TimeZoneRule& to) {
    delete fTo;
    fTo = to.clone();
}

void
TimeZoneTransition::adoptTo(TimeZoneRule* to) {
    delete fTo;
    fTo = to;
}

UDate
TimeZoneTransition::getTime() const {
    return fTime;
}

const TimeZoneRule*
TimeZoneTransition::getTo() const {
    return fTo;
}

const TimeZoneRule*
TimeZoneTransition::getFrom() const {
    return fFrom;
}

U_NAMESPACE_END

#endif

//eof
