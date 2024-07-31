// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
**********************************************************************
* Copyright (c) 2004-2014, International Business Machines
* Corporation and others.  All Rights Reserved.
**********************************************************************
* Author: Alan Liu
* Created: April 26, 2004
* Since: ICU 3.0
**********************************************************************
*/
#include "utypeinfo.h"  // for 'typeid' to work

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/measure.h"
#include "unicode/measunit.h"

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(Measure)

Measure::Measure() : unit(nullptr) {}

Measure::Measure(const Formattable& _number, MeasureUnit* adoptedUnit,
                 UErrorCode& ec) :
    number(_number), unit(adoptedUnit) {
    if (U_SUCCESS(ec) &&
        (!number.isNumeric() || adoptedUnit == nullptr)) {
        ec = U_ILLEGAL_ARGUMENT_ERROR;
    }
}

Measure::Measure(const Measure& other) :
    UObject(other), unit(nullptr) {
    *this = other;
}

Measure& Measure::operator=(const Measure& other) {
    if (this != &other) {
        delete unit;
        number = other.number;
        if (other.unit != nullptr) {
            unit = other.unit->clone();
        } else {
            unit = nullptr;
        }
    }
    return *this;
}

Measure *Measure::clone() const {
    return new Measure(*this);
}

Measure::~Measure() {
    delete unit;
}

bool Measure::operator==(const UObject& other) const {
    if (this == &other) {  // Same object, equal
        return true;
    }
    if (typeid(*this) != typeid(other)) { // Different types, not equal
        return false;
    }
    const Measure &m = static_cast<const Measure&>(other);
    return number == m.number &&
        ((unit == nullptr) == (m.unit == nullptr)) &&
        (unit == nullptr || *unit == *m.unit);
}

U_NAMESPACE_END

#endif // !UCONFIG_NO_FORMATTING
