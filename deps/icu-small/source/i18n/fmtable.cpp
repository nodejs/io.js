// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 1997-2016, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
* File FMTABLE.CPP
*
* Modification History:
*
*   Date        Name        Description
*   03/25/97    clhuang     Initial Implementation.
********************************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include <cstdlib>
#include <math.h>
#include "unicode/fmtable.h"
#include "unicode/ustring.h"
#include "unicode/measure.h"
#include "unicode/curramt.h"
#include "unicode/uformattable.h"
#include "charstr.h"
#include "cmemory.h"
#include "cstring.h"
#include "fmtableimp.h"
#include "number_decimalquantity.h"

// *****************************************************************************
// class Formattable
// *****************************************************************************

U_NAMESPACE_BEGIN

UOBJECT_DEFINE_RTTI_IMPLEMENTATION(Formattable)

using number::impl::DecimalQuantity;


//-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// NOTE: As of 3.0, there are limitations to the UObject API.  It does
// not (yet) support cloning, operator=, nor operator==.  To
// work around this, I implement some simple inlines here.  Later
// these can be modified or removed.  [alan]

// NOTE: These inlines assume that all fObjects are in fact instances
// of the Measure class, which is true as of 3.0.  [alan]

// Return true if *a == *b.
static inline UBool objectEquals(const UObject* a, const UObject* b) {
    // LATER: return *a == *b;
    return *((const Measure*) a) == *b;
}

// Return a clone of *a.
static inline UObject* objectClone(const UObject* a) {
    // LATER: return a->clone();
    return ((const Measure*) a)->clone();
}

// Return true if *a is an instance of Measure.
static inline UBool instanceOfMeasure(const UObject* a) {
    return dynamic_cast<const Measure*>(a) != nullptr;
}

/**
 * Creates a new Formattable array and copies the values from the specified
 * original.
 * @param array the original array
 * @param count the original array count
 * @return the new Formattable array.
 */
static Formattable* createArrayCopy(const Formattable* array, int32_t count) {
    Formattable *result = new Formattable[count];
    if (result != nullptr) {
        for (int32_t i=0; i<count; ++i)
            result[i] = array[i]; // Don't memcpy!
    }
    return result;
}

//-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

/**
 * Set 'ec' to 'err' only if 'ec' is not already set to a failing UErrorCode.
 */
static void setError(UErrorCode& ec, UErrorCode err) {
    if (U_SUCCESS(ec)) {
        ec = err;
    }
}

//
//  Common initialization code, shared by constructors.
//  Put everything into a known state.
//
void  Formattable::init() {
    fValue.fInt64 = 0;
    fType = kLong;
    fDecimalStr = nullptr;
    fDecimalQuantity = nullptr;
    fBogus.setToBogus(); 
}

// -------------------------------------
// default constructor.
// Creates a formattable object with a long value 0.

Formattable::Formattable() {
    init();
}

// -------------------------------------
// Creates a formattable object with a Date instance.

Formattable::Formattable(UDate date, ISDATE /*isDate*/)
{
    init();
    fType = kDate;
    fValue.fDate = date;
}

// -------------------------------------
// Creates a formattable object with a double value.

Formattable::Formattable(double value)
{
    init();
    fType = kDouble;
    fValue.fDouble = value;
}

// -------------------------------------
// Creates a formattable object with an int32_t value.

Formattable::Formattable(int32_t value)
{
    init();
    fValue.fInt64 = value;
}

// -------------------------------------
// Creates a formattable object with an int64_t value.

Formattable::Formattable(int64_t value)
{
    init();
    fType = kInt64;
    fValue.fInt64 = value;
}

// -------------------------------------
// Creates a formattable object with a decimal number value from a string.

Formattable::Formattable(StringPiece number, UErrorCode &status) {
    init();
    setDecimalNumber(number, status);
}


// -------------------------------------
// Creates a formattable object with a UnicodeString instance.

Formattable::Formattable(const UnicodeString& stringToCopy)
{
    init();
    fType = kString;
    fValue.fString = new UnicodeString(stringToCopy);
}

// -------------------------------------
// Creates a formattable object with a UnicodeString* value.
// (adopting semantics)

Formattable::Formattable(UnicodeString* stringToAdopt)
{
    init();
    fType = kString;
    fValue.fString = stringToAdopt;
}

Formattable::Formattable(UObject* objectToAdopt)
{
    init();
    fType = kObject;
    fValue.fObject = objectToAdopt;
}

// -------------------------------------

Formattable::Formattable(const Formattable* arrayToCopy, int32_t count)
    :   UObject(), fType(kArray)
{
    init();
    fType = kArray;
    fValue.fArrayAndCount.fArray = createArrayCopy(arrayToCopy, count);
    fValue.fArrayAndCount.fCount = count;
}

// -------------------------------------
// copy constructor


Formattable::Formattable(const Formattable &source)
     :  UObject(*this)
{
    init();
    *this = source;
}

// -------------------------------------
// assignment operator

Formattable&
Formattable::operator=(const Formattable& source)
{
    if (this != &source)
    {
        // Disposes the current formattable value/setting.
        dispose();

        // Sets the correct data type for this value.
        fType = source.fType;
        switch (fType)
        {
        case kArray:
            // Sets each element in the array one by one and records the array count.
            fValue.fArrayAndCount.fCount = source.fValue.fArrayAndCount.fCount;
            fValue.fArrayAndCount.fArray = createArrayCopy(source.fValue.fArrayAndCount.fArray,
                                                           source.fValue.fArrayAndCount.fCount);
            break;
        case kString:
            // Sets the string value.
            fValue.fString = new UnicodeString(*source.fValue.fString);
            break;
        case kDouble:
            // Sets the double value.
            fValue.fDouble = source.fValue.fDouble;
            break;
        case kLong:
        case kInt64:
            // Sets the long value.
            fValue.fInt64 = source.fValue.fInt64;
            break;
        case kDate:
            // Sets the Date value.
            fValue.fDate = source.fValue.fDate;
            break;
        case kObject:
            fValue.fObject = objectClone(source.fValue.fObject);
            break;
        }

        UErrorCode status = U_ZERO_ERROR;
        if (source.fDecimalQuantity != nullptr) {
          fDecimalQuantity = new DecimalQuantity(*source.fDecimalQuantity);
        }
        if (source.fDecimalStr != nullptr) {
            fDecimalStr = new CharString(*source.fDecimalStr, status);
            if (U_FAILURE(status)) {
                delete fDecimalStr;
                fDecimalStr = nullptr;
            }
        }
    }
    return *this;
}

// -------------------------------------

bool
Formattable::operator==(const Formattable& that) const
{
    int32_t i;

    if (this == &that) return true;

    // Returns false if the data types are different.
    if (fType != that.fType) return false;

    // Compares the actual data values.
    bool equal = true;
    switch (fType) {
    case kDate:
        equal = (fValue.fDate == that.fValue.fDate);
        break;
    case kDouble:
        equal = (fValue.fDouble == that.fValue.fDouble);
        break;
    case kLong:
    case kInt64:
        equal = (fValue.fInt64 == that.fValue.fInt64);
        break;
    case kString:
        equal = (*(fValue.fString) == *(that.fValue.fString));
        break;
    case kArray:
        if (fValue.fArrayAndCount.fCount != that.fValue.fArrayAndCount.fCount) {
            equal = false;
            break;
        }
        // Checks each element for equality.
        for (i=0; i<fValue.fArrayAndCount.fCount; ++i) {
            if (fValue.fArrayAndCount.fArray[i] != that.fValue.fArrayAndCount.fArray[i]) {
                equal = false;
                break;
            }
        }
        break;
    case kObject:
        if (fValue.fObject == nullptr || that.fValue.fObject == nullptr) {
            equal = false;
        } else {
            equal = objectEquals(fValue.fObject, that.fValue.fObject);
        }
        break;
    }

    // TODO:  compare digit lists if numeric.
    return equal;
}

// -------------------------------------

Formattable::~Formattable()
{
    dispose();
}

// -------------------------------------

void Formattable::dispose()
{
    // Deletes the data value if necessary.
    switch (fType) {
    case kString:
        delete fValue.fString;
        break;
    case kArray:
        delete[] fValue.fArrayAndCount.fArray;
        break;
    case kObject:
        delete fValue.fObject;
        break;
    default:
        break;
    }

    fType = kLong;
    fValue.fInt64 = 0;

    delete fDecimalStr;
    fDecimalStr = nullptr;

    delete fDecimalQuantity;
    fDecimalQuantity = nullptr;
}

Formattable *
Formattable::clone() const {
    return new Formattable(*this);
}

// -------------------------------------
// Gets the data type of this Formattable object. 
Formattable::Type
Formattable::getType() const
{
    return fType;
}

UBool
Formattable::isNumeric() const {
    switch (fType) {
    case kDouble:
    case kLong:
    case kInt64:
        return true;
    default:
        return false;
    }
}

// -------------------------------------
int32_t
//Formattable::getLong(UErrorCode* status) const
Formattable::getLong(UErrorCode& status) const
{
    if (U_FAILURE(status)) {
        return 0;
    }
        
    switch (fType) {
    case Formattable::kLong: 
        return static_cast<int32_t>(fValue.fInt64);
    case Formattable::kInt64:
        if (fValue.fInt64 > INT32_MAX) {
            status = U_INVALID_FORMAT_ERROR;
            return INT32_MAX;
        } else if (fValue.fInt64 < INT32_MIN) {
            status = U_INVALID_FORMAT_ERROR;
            return INT32_MIN;
        } else {
            return static_cast<int32_t>(fValue.fInt64);
        }
    case Formattable::kDouble:
        if (fValue.fDouble > INT32_MAX) {
            status = U_INVALID_FORMAT_ERROR;
            return INT32_MAX;
        } else if (fValue.fDouble < INT32_MIN) {
            status = U_INVALID_FORMAT_ERROR;
            return INT32_MIN;
        } else {
            return static_cast<int32_t>(fValue.fDouble); // loses fraction
        }
    case Formattable::kObject:
        if (fValue.fObject == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return 0;
        }
        // TODO Later replace this with instanceof call
        if (instanceOfMeasure(fValue.fObject)) {
            return ((const Measure*) fValue.fObject)->
                getNumber().getLong(status);
        }
        U_FALLTHROUGH;
    default:
        status = U_INVALID_FORMAT_ERROR;
        return 0;
    }
}

// -------------------------------------
// Maximum int that can be represented exactly in a double.  (53 bits)
//    Larger ints may be rounded to a near-by value as not all are representable.
// TODO:  move this constant elsewhere, possibly configure it for different
//        floating point formats, if any non-standard ones are still in use.
static const int64_t U_DOUBLE_MAX_EXACT_INT = 9007199254740992LL;

int64_t
Formattable::getInt64(UErrorCode& status) const
{
    if (U_FAILURE(status)) {
        return 0;
    }
        
    switch (fType) {
    case Formattable::kLong: 
    case Formattable::kInt64: 
        return fValue.fInt64;
    case Formattable::kDouble:
        if (fValue.fDouble > static_cast<double>(U_INT64_MAX)) {
            status = U_INVALID_FORMAT_ERROR;
            return U_INT64_MAX;
        } else if (fValue.fDouble < static_cast<double>(U_INT64_MIN)) {
            status = U_INVALID_FORMAT_ERROR;
            return U_INT64_MIN;
        } else if (fabs(fValue.fDouble) > U_DOUBLE_MAX_EXACT_INT && fDecimalQuantity != nullptr) {
            if (fDecimalQuantity->fitsInLong(true)) {
                return fDecimalQuantity->toLong();
            } else {
                // Unexpected
                status = U_INVALID_FORMAT_ERROR;
                return fDecimalQuantity->isNegative() ? U_INT64_MIN : U_INT64_MAX;
            }
        } else {
            return static_cast<int64_t>(fValue.fDouble);
        } 
    case Formattable::kObject:
        if (fValue.fObject == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return 0;
        }
        if (instanceOfMeasure(fValue.fObject)) {
            return ((const Measure*) fValue.fObject)->
                getNumber().getInt64(status);
        }
        U_FALLTHROUGH;
    default:
        status = U_INVALID_FORMAT_ERROR;
        return 0;
    }
}

// -------------------------------------
double
Formattable::getDouble(UErrorCode& status) const
{
    if (U_FAILURE(status)) {
        return 0;
    }
        
    switch (fType) {
    case Formattable::kLong: 
    case Formattable::kInt64: // loses precision
        return static_cast<double>(fValue.fInt64);
    case Formattable::kDouble:
        return fValue.fDouble;
    case Formattable::kObject:
        if (fValue.fObject == nullptr) {
            status = U_MEMORY_ALLOCATION_ERROR;
            return 0;
        }
        // TODO Later replace this with instanceof call
        if (instanceOfMeasure(fValue.fObject)) {
            return ((const Measure*) fValue.fObject)->
                getNumber().getDouble(status);
        }
        U_FALLTHROUGH;
    default:
        status = U_INVALID_FORMAT_ERROR;
        return 0;
    }
}

const UObject*
Formattable::getObject() const {
    return (fType == kObject) ? fValue.fObject : nullptr;
}

// -------------------------------------
// Sets the value to a double value d.

void
Formattable::setDouble(double d)
{
    dispose();
    fType = kDouble;
    fValue.fDouble = d;
}

// -------------------------------------
// Sets the value to a long value l.

void
Formattable::setLong(int32_t l)
{
    dispose();
    fType = kLong;
    fValue.fInt64 = l;
}

// -------------------------------------
// Sets the value to an int64 value ll.

void
Formattable::setInt64(int64_t ll)
{
    dispose();
    fType = kInt64;
    fValue.fInt64 = ll;
}

// -------------------------------------
// Sets the value to a Date instance d.

void
Formattable::setDate(UDate d)
{
    dispose();
    fType = kDate;
    fValue.fDate = d;
}

// -------------------------------------
// Sets the value to a string value stringToCopy.

void
Formattable::setString(const UnicodeString& stringToCopy)
{
    dispose();
    fType = kString;
    fValue.fString = new UnicodeString(stringToCopy);
}

// -------------------------------------
// Sets the value to an array of Formattable objects.

void
Formattable::setArray(const Formattable* array, int32_t count)
{
    dispose();
    fType = kArray;
    fValue.fArrayAndCount.fArray = createArrayCopy(array, count);
    fValue.fArrayAndCount.fCount = count;
}

// -------------------------------------
// Adopts the stringToAdopt value.

void
Formattable::adoptString(UnicodeString* stringToAdopt)
{
    dispose();
    fType = kString;
    fValue.fString = stringToAdopt;
}

// -------------------------------------
// Adopts the array value and its count.

void
Formattable::adoptArray(Formattable* array, int32_t count)
{
    dispose();
    fType = kArray;
    fValue.fArrayAndCount.fArray = array;
    fValue.fArrayAndCount.fCount = count;
}

void
Formattable::adoptObject(UObject* objectToAdopt) {
    dispose();
    fType = kObject;
    fValue.fObject = objectToAdopt;
}

// -------------------------------------
UnicodeString& 
Formattable::getString(UnicodeString& result, UErrorCode& status) const 
{
    if (fType != kString) {
        setError(status, U_INVALID_FORMAT_ERROR);
        result.setToBogus();
    } else {
        if (fValue.fString == nullptr) {
            setError(status, U_MEMORY_ALLOCATION_ERROR);
        } else {
            result = *fValue.fString;
        }
    }
    return result;
}

// -------------------------------------
const UnicodeString& 
Formattable::getString(UErrorCode& status) const 
{
    if (fType != kString) {
        setError(status, U_INVALID_FORMAT_ERROR);
        return *getBogus();
    }
    if (fValue.fString == nullptr) {
        setError(status, U_MEMORY_ALLOCATION_ERROR);
        return *getBogus();
    }
    return *fValue.fString;
}

// -------------------------------------
UnicodeString& 
Formattable::getString(UErrorCode& status) 
{
    if (fType != kString) {
        setError(status, U_INVALID_FORMAT_ERROR);
        return *getBogus();
    }
    if (fValue.fString == nullptr) {
    	setError(status, U_MEMORY_ALLOCATION_ERROR);
    	return *getBogus();
    }
    return *fValue.fString;
}

// -------------------------------------
const Formattable* 
Formattable::getArray(int32_t& count, UErrorCode& status) const 
{
    if (fType != kArray) {
        setError(status, U_INVALID_FORMAT_ERROR);
        count = 0;
        return nullptr;
    }
    count = fValue.fArrayAndCount.fCount; 
    return fValue.fArrayAndCount.fArray;
}

// -------------------------------------
// Gets the bogus string, ensures mondo bogosity.

UnicodeString*
Formattable::getBogus() const 
{
    return const_cast<UnicodeString*>(&fBogus); /* cast away const :-( */
}


// --------------------------------------
StringPiece Formattable::getDecimalNumber(UErrorCode &status) {
    if (U_FAILURE(status)) {
        return "";
    }
    if (fDecimalStr != nullptr) {
      return fDecimalStr->toStringPiece();
    }

    CharString *decimalStr = internalGetCharString(status);
    if(decimalStr == nullptr) {
      return ""; // getDecimalNumber returns "" for error cases
    } else {
      return decimalStr->toStringPiece();
    }
}

CharString *Formattable::internalGetCharString(UErrorCode &status) {
    if(fDecimalStr == nullptr) {
      if (fDecimalQuantity == nullptr) {
        // No decimal number for the formattable yet.  Which means the value was
        // set directly by the user as an int, int64 or double.  If the value came
        // from parsing, or from the user setting a decimal number, fDecimalNum
        // would already be set.
        //
        LocalPointer<DecimalQuantity> dq(new DecimalQuantity(), status);
        if (U_FAILURE(status)) { return nullptr; }
        populateDecimalQuantity(*dq, status);
        if (U_FAILURE(status)) { return nullptr; }
        fDecimalQuantity = dq.orphan();
      }

      fDecimalStr = new CharString();
      if (fDecimalStr == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
        return nullptr;
      }
      // Older ICUs called uprv_decNumberToString here, which is not exactly the same as
      // DecimalQuantity::toScientificString(). The biggest difference is that uprv_decNumberToString does
      // not print scientific notation for magnitudes greater than -5 and smaller than some amount (+5?).
      if (fDecimalQuantity->isInfinite()) {
        fDecimalStr->append("Infinity", status);
      } else if (fDecimalQuantity->isNaN()) {
        fDecimalStr->append("NaN", status);
      } else if (fDecimalQuantity->isZeroish()) {
        fDecimalStr->append("0", -1, status);
      } else if (fType==kLong || fType==kInt64 || // use toPlainString for integer types
                  (fDecimalQuantity->getMagnitude() != INT32_MIN && std::abs(fDecimalQuantity->getMagnitude()) < 5)) {
        fDecimalStr->appendInvariantChars(fDecimalQuantity->toPlainString(), status);
      } else {
        fDecimalStr->appendInvariantChars(fDecimalQuantity->toScientificString(), status);
      }
    }
    return fDecimalStr;
}

void
Formattable::populateDecimalQuantity(number::impl::DecimalQuantity& output, UErrorCode& status) const {
    if (fDecimalQuantity != nullptr) {
        output = *fDecimalQuantity;
        return;
    }

    switch (fType) {
        case kDouble:
            output.setToDouble(this->getDouble());
            output.roundToInfinity();
            break;
        case kLong:
            output.setToInt(this->getLong());
            break;
        case kInt64:
            output.setToLong(this->getInt64());
            break;
        default:
            // The formattable's value is not a numeric type.
            status = U_INVALID_STATE_ERROR;
    }
}

// ---------------------------------------
void
Formattable::adoptDecimalQuantity(DecimalQuantity *dq) {
    delete fDecimalQuantity;
    fDecimalQuantity = dq;
    if (dq == nullptr) { // allow adoptDigitList(nullptr) to clear
        return;
    }

    // Set the value into the Union of simple type values.
    // Cannot use the set() functions because they would delete the fDecimalNum value.
    if (fDecimalQuantity->fitsInLong()) {
        fValue.fInt64 = fDecimalQuantity->toLong();
        if (fValue.fInt64 <= INT32_MAX && fValue.fInt64 >= INT32_MIN) {
            fType = kLong;
        } else {
            fType = kInt64;
        }
    } else {
        fType = kDouble;
        fValue.fDouble = fDecimalQuantity->toDouble();
    }
}


// ---------------------------------------
void
Formattable::setDecimalNumber(StringPiece numberString, UErrorCode &status) {
    if (U_FAILURE(status)) {
        return;
    }
    dispose();

    auto* dq = new DecimalQuantity();
    dq->setToDecNumber(numberString, status);
    adoptDecimalQuantity(dq);

    // Note that we do not hang on to the caller's input string.
    // If we are asked for the string, we will regenerate one from fDecimalQuantity.
}

#if 0
//----------------------------------------------------
// console I/O
//----------------------------------------------------
#ifdef _DEBUG

#include <iostream>
using namespace std;

#include "unicode/datefmt.h"
#include "unistrm.h"

class FormattableStreamer /* not : public UObject because all methods are static */ {
public:
    static void streamOut(ostream& stream, const Formattable& obj);

private:
    FormattableStreamer() {} // private - forbid instantiation
};

// This is for debugging purposes only.  This will send a displayable
// form of the Formattable object to the output stream.

void
FormattableStreamer::streamOut(ostream& stream, const Formattable& obj)
{
    static DateFormat *defDateFormat = 0;

    UnicodeString buffer;
    switch(obj.getType()) {
        case Formattable::kDate : 
            // Creates a DateFormat instance for formatting the
            // Date instance.
            if (defDateFormat == 0) {
                defDateFormat = DateFormat::createInstance();
            }
            defDateFormat->format(obj.getDate(), buffer);
            stream << buffer;
            break;
        case Formattable::kDouble :
            // Output the double as is.
            stream << obj.getDouble() << 'D';
            break;
        case Formattable::kLong :
            // Output the double as is.
            stream << obj.getLong() << 'L';
            break;
        case Formattable::kString:
            // Output the double as is.  Please see UnicodeString console
            // I/O routine for more details.
            stream << '"' << obj.getString(buffer) << '"';
            break;
        case Formattable::kArray:
            int32_t i, count;
            const Formattable* array;
            array = obj.getArray(count);
            stream << '[';
            // Recursively calling the console I/O routine for each element in the array.
            for (i=0; i<count; ++i) {
                FormattableStreamer::streamOut(stream, array[i]);
                stream << ( (i==(count-1)) ? "" : ", " );
            }
            stream << ']';
            break;
        default:
            // Not a recognizable Formattable object.
            stream << "INVALID_Formattable";
    }
    stream.flush();
}
#endif

#endif

U_NAMESPACE_END

/* ---- UFormattable implementation ---- */

U_NAMESPACE_USE

U_CAPI UFormattable* U_EXPORT2
ufmt_open(UErrorCode *status) {
  if( U_FAILURE(*status) ) {
    return nullptr;
  }
  UFormattable *fmt = (new Formattable())->toUFormattable();

  if( fmt == nullptr ) {
    *status = U_MEMORY_ALLOCATION_ERROR;
  }
  return fmt;
}

U_CAPI void U_EXPORT2
ufmt_close(UFormattable *fmt) {
  Formattable *obj = Formattable::fromUFormattable(fmt);

  delete obj;
}

U_CAPI UFormattableType U_EXPORT2
ufmt_getType(const UFormattable *fmt, UErrorCode *status) {
  if(U_FAILURE(*status)) {
    return (UFormattableType)UFMT_COUNT;
  }
  const Formattable *obj = Formattable::fromUFormattable(fmt);
  return (UFormattableType)obj->getType();
}


U_CAPI UBool U_EXPORT2
ufmt_isNumeric(const UFormattable *fmt) {
  const Formattable *obj = Formattable::fromUFormattable(fmt);
  return obj->isNumeric();
}

U_CAPI UDate U_EXPORT2
ufmt_getDate(const UFormattable *fmt, UErrorCode *status) {
  const Formattable *obj = Formattable::fromUFormattable(fmt);

  return obj->getDate(*status);
}

U_CAPI double U_EXPORT2
ufmt_getDouble(UFormattable *fmt, UErrorCode *status) {
  Formattable *obj = Formattable::fromUFormattable(fmt);

  return obj->getDouble(*status);
}

U_CAPI int32_t U_EXPORT2
ufmt_getLong(UFormattable *fmt, UErrorCode *status) {
  Formattable *obj = Formattable::fromUFormattable(fmt);

  return obj->getLong(*status);
}


U_CAPI const void *U_EXPORT2
ufmt_getObject(const UFormattable *fmt, UErrorCode *status) {
  const Formattable *obj = Formattable::fromUFormattable(fmt);

  const void *ret = obj->getObject();
  if( ret==nullptr &&
      (obj->getType() != Formattable::kObject) &&
      U_SUCCESS( *status )) {
    *status = U_INVALID_FORMAT_ERROR;
  }
  return ret;
}

U_CAPI const char16_t* U_EXPORT2
ufmt_getUChars(UFormattable *fmt, int32_t *len, UErrorCode *status) {
  Formattable *obj = Formattable::fromUFormattable(fmt);

  // avoid bogosity by checking the type first.
  if( obj->getType() != Formattable::kString ) {
    if( U_SUCCESS(*status) ){
      *status = U_INVALID_FORMAT_ERROR;
    }
    return nullptr;
  }

  // This should return a valid string
  UnicodeString &str = obj->getString(*status);
  if( U_SUCCESS(*status) && len != nullptr ) {
    *len = str.length();
  }
  return str.getTerminatedBuffer();
}

U_CAPI int32_t U_EXPORT2
ufmt_getArrayLength(const UFormattable* fmt, UErrorCode *status) {
  const Formattable *obj = Formattable::fromUFormattable(fmt);

  int32_t count;
  (void)obj->getArray(count, *status);
  return count;
}

U_CAPI UFormattable * U_EXPORT2
ufmt_getArrayItemByIndex(UFormattable* fmt, int32_t n, UErrorCode *status) {
  Formattable *obj = Formattable::fromUFormattable(fmt);
  int32_t count;
  (void)obj->getArray(count, *status);
  if(U_FAILURE(*status)) {
    return nullptr;
  } else if(n<0 || n>=count) {
    setError(*status, U_INDEX_OUTOFBOUNDS_ERROR);
    return nullptr;
  } else {
    return (*obj)[n].toUFormattable(); // returns non-const Formattable
  }
}

U_CAPI const char * U_EXPORT2
ufmt_getDecNumChars(UFormattable *fmt, int32_t *len, UErrorCode *status) {
  if(U_FAILURE(*status)) {
    return "";
  }
  Formattable *obj = Formattable::fromUFormattable(fmt);
  CharString *charString = obj->internalGetCharString(*status);
  if(U_FAILURE(*status)) {
    return "";
  }
  if(charString == nullptr) {
    *status = U_MEMORY_ALLOCATION_ERROR;
    return "";
  } else {
    if(len!=nullptr) {
      *len = charString->length();
    }
    return charString->data();
  }
}

U_CAPI int64_t U_EXPORT2
ufmt_getInt64(UFormattable *fmt, UErrorCode *status) {
  Formattable *obj = Formattable::fromUFormattable(fmt);
  return obj->getInt64(*status);
}

#endif /* #if !UCONFIG_NO_FORMATTING */

//eof
