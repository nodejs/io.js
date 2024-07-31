// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/************************************************************************
 * Copyright (C) 1996-2008, International Business Machines Corporation *
 * and others. All Rights Reserved.                                     *
 ************************************************************************
 *  2003-nov-07   srl       Port from Java
 */

#ifndef ASTRO_H
#define ASTRO_H

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "gregoimp.h"  // for Math
#include "unicode/unistr.h"

U_NAMESPACE_BEGIN

/**
 * <code>CalendarAstronomer</code> is a class that can perform the calculations to
 * determine the positions of the sun and moon, the time of sunrise and
 * sunset, and other astronomy-related data.  The calculations it performs
 * are in some cases quite complicated, and this utility class saves you
 * the trouble of worrying about them.
 * <p>
 * The measurement of time is a very important part of astronomy.  Because
 * astronomical bodies are constantly in motion, observations are only valid
 * at a given moment in time.  Accordingly, each <code>CalendarAstronomer</code>
 * object has a <code>time</code> property that determines the date
 * and time for which its calculations are performed.  You can set and
 * retrieve this property with {@link #setTime setTime}, {@link #getTime getTime}
 * and related methods.
 * <p>
 * Almost all of the calculations performed by this class, or by any
 * astronomer, are approximations to various degrees of accuracy.  The
 * calculations in this class are mostly modelled after those described
 * in the book
 * <a href="http://www.amazon.com/exec/obidos/ISBN=0521356997" target="_top">
 * Practical Astronomy With Your Calculator</a>, by Peter J.
 * Duffett-Smith, Cambridge University Press, 1990.  This is an excellent
 * book, and if you want a greater understanding of how these calculations
 * are performed it a very good, readable starting point.
 * <p>
 * <strong>WARNING:</strong> This class is very early in its development, and
 * it is highly likely that its API will change to some degree in the future.
 * At the moment, it basically does just enough to support {@link IslamicCalendar}
 * and {@link ChineseCalendar}.
 *
 * @author Laura Werner
 * @author Alan Liu
 * @internal
 */
class U_I18N_API CalendarAstronomer : public UMemory {
public:
  // some classes

public:
  /**
   * Represents the position of an object in the sky relative to the ecliptic,
   * the plane of the earth's orbit around the Sun.
   * This is a spherical coordinate system in which the latitude
   * specifies the position north or south of the plane of the ecliptic.
   * The longitude specifies the position along the ecliptic plane
   * relative to the "First Point of Aries", which is the Sun's position in the sky
   * at the Vernal Equinox.
   * <p>
   * Note that Ecliptic objects are immutable and cannot be modified
   * once they are constructed.  This allows them to be passed and returned by
   * value without worrying about whether other code will modify them.
   *
   * @see CalendarAstronomer.Equatorial
   * @internal
   */
  class U_I18N_API Ecliptic : public UMemory {
  public:
    /**
     * Constructs an Ecliptic coordinate object.
     * <p>
     * @param lat The ecliptic latitude, measured in radians.
     * @param lon The ecliptic longitude, measured in radians.
     * @internal
     */
    Ecliptic(double lat = 0, double lon = 0) {
      latitude = lat;
      longitude = lon;
    }

    /**
     * Setter for Ecliptic Coordinate object
     * @param lat The ecliptic latitude, measured in radians.
     * @param lon The ecliptic longitude, measured in radians.
     * @internal
     */
    void set(double lat, double lon) {
      latitude = lat;
      longitude = lon;
    }

    /**
     * Return a string representation of this object
     * @internal
     */
    UnicodeString toString() const;

    /**
     * The ecliptic latitude, in radians.  This specifies an object's
     * position north or south of the plane of the ecliptic,
     * with positive angles representing north.
     * @internal
     */
    double latitude;

    /**
     * The ecliptic longitude, in radians.
     * This specifies an object's position along the ecliptic plane
     * relative to the "First Point of Aries", which is the Sun's position
     * in the sky at the Vernal Equinox,
     * with positive angles representing east.
     * <p>
     * A bit of trivia: the first point of Aries is currently in the
     * constellation Pisces, due to the precession of the earth's axis.
     * @internal
     */
    double longitude;
  };

  /**
   * Represents the position of an
   * object in the sky relative to the plane of the earth's equator.
   * The <i>Right Ascension</i> specifies the position east or west
   * along the equator, relative to the sun's position at the vernal
   * equinox.  The <i>Declination</i> is the position north or south
   * of the equatorial plane.
   * <p>
   * Note that Equatorial objects are immutable and cannot be modified
   * once they are constructed.  This allows them to be passed and returned by
   * value without worrying about whether other code will modify them.
   *
   * @see CalendarAstronomer.Ecliptic
   * @internal
   */
  class U_I18N_API Equatorial : public UMemory {
  public:
    /**
     * Constructs an Equatorial coordinate object.
     * <p>
     * @param asc The right ascension, measured in radians.
     * @param dec The declination, measured in radians.
     * @internal
     */
    Equatorial(double asc = 0, double dec = 0)
      : ascension(asc), declination(dec) { }

    /**
     * Setter
     * @param asc The right ascension, measured in radians.
     * @param dec The declination, measured in radians.
     * @internal
     */
    void set(double asc, double dec) {
      ascension = asc;
      declination = dec;
    }

    /**
     * Return a string representation of this object, with the
     * angles measured in degrees.
     * @internal
     */
    UnicodeString toString() const;

    /**
     * Return a string representation of this object with the right ascension
     * measured in hours, minutes, and seconds.
     * @internal
     */
    //String toHmsString() {
    //return radToHms(ascension) + "," + radToDms(declination);
    //}

    /**
     * The right ascension, in radians.
     * This is the position east or west along the equator
     * relative to the sun's position at the vernal equinox,
     * with positive angles representing East.
     * @internal
     */
    double ascension;

    /**
     * The declination, in radians.
     * This is the position north or south of the equatorial plane,
     * with positive angles representing north.
     * @internal
     */
    double declination;
  };

public:
  //-------------------------------------------------------------------------
  // Assorted private data used for conversions
  //-------------------------------------------------------------------------

  // My own copies of these so compilers are more likely to optimize them away
  static const double PI;

  /**
   * The average number of solar days from one new moon to the next.  This is the time
   * it takes for the moon to return the same ecliptic longitude as the sun.
   * It is longer than the sidereal month because the sun's longitude increases
   * during the year due to the revolution of the earth around the sun.
   * Approximately 29.53.
   *
   * @see #SIDEREAL_MONTH
   * @internal
   * @deprecated ICU 2.4. This class may be removed or modified.
   */
  static const double SYNODIC_MONTH;

  //-------------------------------------------------------------------------
  // Constructors
  //-------------------------------------------------------------------------

  /**
   * Construct a new <code>CalendarAstronomer</code> object that is initialized to
   * the current date and time.
   * @internal
   */
  CalendarAstronomer();

  /**
   * Construct a new <code>CalendarAstronomer</code> object that is initialized to
   * the specified date and time.
   * @internal
   */
  CalendarAstronomer(UDate d);

  /**
   * Destructor
   * @internal
   */
  ~CalendarAstronomer();

  //-------------------------------------------------------------------------
  // Time and date getters and setters
  //-------------------------------------------------------------------------

  /**
   * Set the current date and time of this <code>CalendarAstronomer</code> object.  All
   * astronomical calculations are performed based on this time setting.
   *
   * @param aTime the date and time, expressed as the number of milliseconds since
   *              1/1/1970 0:00 GMT (Gregorian).
   *
   * @see #getTime
   * @internal
   */
  void setTime(UDate aTime);

  /**
   * Get the current time of this <code>CalendarAstronomer</code> object,
   * represented as the number of milliseconds since
   * 1/1/1970 AD 0:00 GMT (Gregorian).
   *
   * @see #setTime
   * @internal
   */
  UDate getTime();

  /**
   * Get the current time of this <code>CalendarAstronomer</code> object,
   * expressed as a "julian day number", which is the number of elapsed
   * days since 1/1/4713 BC (Julian), 12:00 GMT.
   *
   * @see #JULIAN_EPOCH_MS
   * @internal
   */
  double getJulianDay();

public:
  /**
   * Convert from ecliptic to equatorial coordinates.
   *
   * @param eclipLong     The ecliptic longitude
   * @param eclipLat      The ecliptic latitude
   *
   * @return              The corresponding point in equatorial coordinates.
   * @internal
   */
  Equatorial& eclipticToEquatorial(Equatorial& result, double eclipLong, double eclipLat);

  //-------------------------------------------------------------------------
  // The Sun
  //-------------------------------------------------------------------------

  /**
   * The longitude of the sun at the time specified by this object.
   * The longitude is measured in radians along the ecliptic
   * from the "first point of Aries," the point at which the ecliptic
   * crosses the earth's equatorial plane at the vernal equinox.
   * <p>
   * Currently, this method uses an approximation of the two-body Kepler's
   * equation for the earth and the sun.  It does not take into account the
   * perturbations caused by the other planets, the moon, etc.
   * @internal
   */
  double getSunLongitude();

  /**
   * TODO Make this public when the entire class is package-private.
   */
  /*public*/ void getSunLongitude(double julianDay, double &longitude, double &meanAnomaly);

public:
  /**
   * Constant representing the winter solstice.
   * For use with {@link #getSunTime getSunTime}.
   * Note: In this case, "winter" refers to the northern hemisphere's seasons.
   * @internal
   */
  static double WINTER_SOLSTICE();

  /**
   * Find the next time at which the sun's ecliptic longitude will have
   * the desired value.
   * @internal
   */
  UDate getSunTime(double desired, UBool next);

  //-------------------------------------------------------------------------
  // The Moon
  //-------------------------------------------------------------------------

  /**
   * The position of the moon at the time set on this
   * object, in equatorial coordinates.
   * @internal
   * @return const reference to internal field of calendar astronomer. Do not use outside of the lifetime of this astronomer.
   */
  const Equatorial& getMoonPosition();

  /**
   * The "age" of the moon at the time specified in this object.
   * This is really the angle between the
   * current ecliptic longitudes of the sun and the moon,
   * measured in radians.
   *
   * @see #getMoonPhase
   * @internal
   */
  double getMoonAge();

  class U_I18N_API MoonAge : public UMemory {
  public:
    MoonAge(double l)
      :  value(l) { }
    void set(double l) { value = l; }
    double value;
  };

  /**
   * Constant representing a new moon.
   * For use with {@link #getMoonTime getMoonTime}
   * @internal
   */
  static MoonAge NEW_MOON();

  /**
   * Find the next or previous time at which the Moon's ecliptic
   * longitude will have the desired value.
   * <p>
   * @param desired   The desired longitude.
   * @param next      <tt>true</tt> if the next occurrence of the phase
   *                  is desired, <tt>false</tt> for the previous occurrence.
   * @internal
   */
  UDate getMoonTime(const MoonAge& desired, UBool next);

  //-------------------------------------------------------------------------
  // Interpolation methods for finding the time at which a given event occurs
  //-------------------------------------------------------------------------

public:
  class AngleFunc : public UMemory {
  public:
    virtual double eval(CalendarAstronomer&) = 0;
    virtual ~AngleFunc();
  };
  friend class AngleFunc;

private:
  UDate timeOfAngle(AngleFunc& func, double desired,
                    double periodDays, double epsilon, UBool next);

  //-------------------------------------------------------------------------
  // Other utility methods
  //-------------------------------------------------------------------------
private:

  /**
   * Return the obliquity of the ecliptic (the angle between the ecliptic
   * and the earth's equator) at the current time.  This varies due to
   * the precession of the earth's axis.
   *
   * @return  the obliquity of the ecliptic relative to the equator,
   *          measured in radians.
   */
  double eclipticObliquity();

  //-------------------------------------------------------------------------
  // Private data
  //-------------------------------------------------------------------------
private:
  /**
   * Current time in milliseconds since 1/1/1970 AD
   * @see java.util.Date#getTime
   */
  UDate fTime;

  // The following fields are used to cache calculated results for improved
  // performance.  These values all depend on the current time setting
  // of this object, so the clearCache method is provided.
  double    julianDay;
  double    sunLongitude;
  double    meanAnomalySun;
  double    moonEclipLong;

  void clearCache();

  Equatorial  moonPosition;
  UBool       moonPositionSet;

  /**
   * @internal
   */
//  UDate local(UDate localMillis);
};

U_NAMESPACE_END

struct UHashtable;

U_NAMESPACE_BEGIN

/**
 * Cache of month -> julian day
 * @internal
 */
class CalendarCache : public UMemory {
public:
  static int32_t get(CalendarCache** cache, int32_t key, UErrorCode &status);
  static void put(CalendarCache** cache, int32_t key, int32_t value, UErrorCode &status);
  virtual ~CalendarCache();
private:
  CalendarCache(int32_t size, UErrorCode& status);
  static void createCache(CalendarCache** cache, UErrorCode& status);
  /**
   * not implemented
   */
  CalendarCache();
  UHashtable *fTable;
};

U_NAMESPACE_END

#endif
#endif
