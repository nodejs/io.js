// © 2022 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
#ifndef ISO8601CAL_H
#define ISO8601CAL_H

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/calendar.h"
#include "unicode/gregocal.h"
#include "unicode/timezone.h"

U_NAMESPACE_BEGIN

/**
 * Concrete class which provides the ISO8601 calendar.
 * <P>
 * <code>ISO8601Calendar</code> is a subclass of <code>GregorianCalendar</code>
 * that the first day of a week is Monday and the minimal days in the first
 * week of a year or month is four days.
 * <p>
 * The ISO8601 calendar is identical to the Gregorian calendar in all respects
 * except for the first day of week and the minimal days in the first week
 * of a year. 
 * @internal
 */
class ISO8601Calendar : public GregorianCalendar {
 public:
  //-------------------------------------------------------------------------
  // Constructors...
  //-------------------------------------------------------------------------

  /**
   * Constructs a DangiCalendar based on the current time in the default time zone
   * with the given locale.
   *
   * @param aLocale  The given locale.
   * @param success  Indicates the status of ISO8601Calendar object construction.
   *                 Returns U_ZERO_ERROR if constructed successfully.
   * @internal
   */
  ISO8601Calendar(const Locale& aLocale, UErrorCode &success);

  /**
   * Copy Constructor
   * @internal
   */
  ISO8601Calendar(const ISO8601Calendar& other) = default;

  /**
   * Destructor.
   * @internal
   */
  virtual ~ISO8601Calendar();

  /**
   * Clone.
   * @internal
   */
  virtual ISO8601Calendar* clone() const override;

  // UObject stuff
 public: 
  /**
   * @return   The class ID for this object. All objects of a given class have the
   *           same class ID. Objects of other classes have different class IDs.
   * @internal
   */
  virtual UClassID getDynamicClassID() const override;

  /**
   * Return the class ID for this class. This is useful only for comparing to a return
   * value from getDynamicClassID(). For example:
   *
   *      Base* polymorphic_pointer = createPolymorphicObject();
   *      if (polymorphic_pointer->getDynamicClassID() ==
   *          Derived::getStaticClassID()) ...
   *
   * @return   The class ID for all objects of this class.
   * @internal
   */
  U_I18N_API static UClassID U_EXPORT2 getStaticClassID();

  /**
   * return the calendar type, "iso8601".
   *
   * @return calendar type
   * @internal
   */
  virtual const char * getType() const override;

 protected:
  virtual bool isEra0CountingBackward() const override { return false; }

 private:
 
  ISO8601Calendar(); // default constructor not implemented
};

U_NAMESPACE_END

#endif
#endif
