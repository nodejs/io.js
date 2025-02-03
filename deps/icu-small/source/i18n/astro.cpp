// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/************************************************************************
 * Copyright (C) 1996-2012, International Business Machines Corporation
 * and others. All Rights Reserved.
 ************************************************************************
 *  2003-nov-07   srl       Port from Java
 */

#include "astro.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/calendar.h"
#include <math.h>
#include <float.h>
#include "unicode/putil.h"
#include "uhash.h"
#include "umutex.h"
#include "ucln_in.h"
#include "putilimp.h"
#include <stdio.h>  // for toString()

#if defined (PI) 
#undef PI
#endif

#ifdef U_DEBUG_ASTRO
# include "uresimp.h" // for debugging

static void debug_astro_loc(const char *f, int32_t l)
{
  fprintf(stderr, "%s:%d: ", f, l);
}

static void debug_astro_msg(const char *pat, ...)
{
  va_list ap;
  va_start(ap, pat);
  vfprintf(stderr, pat, ap);
  fflush(stderr);
}
#include "unicode/datefmt.h"
#include "unicode/ustring.h"
static const char * debug_astro_date(UDate d) {
  static char gStrBuf[1024];
  static DateFormat *df = nullptr;
  if(df == nullptr) {
    df = DateFormat::createDateTimeInstance(DateFormat::MEDIUM, DateFormat::MEDIUM, Locale::getUS());
    df->adoptTimeZone(TimeZone::getGMT()->clone());
  }
  UnicodeString str;
  df->format(d,str);
  u_austrncpy(gStrBuf,str.getTerminatedBuffer(),sizeof(gStrBuf)-1);
  return gStrBuf;
}

// must use double parens, i.e.:  U_DEBUG_ASTRO_MSG(("four is: %d",4));
#define U_DEBUG_ASTRO_MSG(x) {debug_astro_loc(__FILE__,__LINE__);debug_astro_msg x;}
#else
#define U_DEBUG_ASTRO_MSG(x)
#endif

static inline UBool isINVALID(double d) {
  return(uprv_isNaN(d));
}

static icu::UMutex ccLock;

U_CDECL_BEGIN
static UBool calendar_astro_cleanup() {
  return true;
}
U_CDECL_END

U_NAMESPACE_BEGIN

/**
 * The number of standard hours in one sidereal day.
 * Approximately 24.93.
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
#define SIDEREAL_DAY (23.93446960027)

/**
 * The number of sidereal hours in one mean solar day.
 * Approximately 24.07.
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
#define SOLAR_DAY  (24.065709816)

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
const double CalendarAstronomer::SYNODIC_MONTH  = 29.530588853;

/**
 * The average number of days it takes
 * for the moon to return to the same ecliptic longitude relative to the
 * stellar background.  This is referred to as the sidereal month.
 * It is shorter than the synodic month due to
 * the revolution of the earth around the sun.
 * Approximately 27.32.
 *
 * @see #SYNODIC_MONTH
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
#define SIDEREAL_MONTH  27.32166

/**
 * The average number number of days between successive vernal equinoxes.
 * Due to the precession of the earth's
 * axis, this is not precisely the same as the sidereal year.
 * Approximately 365.24
 *
 * @see #SIDEREAL_YEAR
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
#define TROPICAL_YEAR  365.242191

/**
 * The average number of days it takes
 * for the sun to return to the same position against the fixed stellar
 * background.  This is the duration of one orbit of the earth about the sun
 * as it would appear to an outside observer.
 * Due to the precession of the earth's
 * axis, this is not precisely the same as the tropical year.
 * Approximately 365.25.
 *
 * @see #TROPICAL_YEAR
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
#define SIDEREAL_YEAR  365.25636

//-------------------------------------------------------------------------
// Time-related constants
//-------------------------------------------------------------------------

/**
 * The number of milliseconds in one second.
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
#define SECOND_MS  U_MILLIS_PER_SECOND

/**
 * The number of milliseconds in one minute.
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
#define MINUTE_MS  U_MILLIS_PER_MINUTE

/**
 * The number of milliseconds in one hour.
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
#define HOUR_MS   U_MILLIS_PER_HOUR

/**
 * The number of milliseconds in one day.
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
#define DAY_MS U_MILLIS_PER_DAY

/**
 * The start of the julian day numbering scheme used by astronomers, which
 * is 1/1/4713 BC (Julian), 12:00 GMT.  This is given as the number of milliseconds
 * since 1/1/1970 AD (Gregorian), a negative number.
 * Note that julian day numbers and
 * the Julian calendar are <em>not</em> the same thing.  Also note that
 * julian days start at <em>noon</em>, not midnight.
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
#define JULIAN_EPOCH_MS  -210866760000000.0


/**
 * Milliseconds value for 0.0 January 2000 AD.
 */
#define EPOCH_2000_MS  946598400000.0

//-------------------------------------------------------------------------
// Assorted private data used for conversions
//-------------------------------------------------------------------------

// My own copies of these so compilers are more likely to optimize them away
const double CalendarAstronomer::PI = 3.14159265358979323846;

#define CalendarAstronomer_PI2  (CalendarAstronomer::PI*2.0)
#define RAD_HOUR  ( 12 / CalendarAstronomer::PI )     // radians -> hours
#define DEG_RAD ( CalendarAstronomer::PI / 180 )      // degrees -> radians
#define RAD_DEG  ( 180 / CalendarAstronomer::PI )     // radians -> degrees

/***
 * Given 'value', add or subtract 'range' until 0 <= 'value' < range.
 * The modulus operator.
 */
inline static double normalize(double value, double range)  {
    return value - range * ClockMath::floorDivide(value, range);
}

/**
 * Normalize an angle so that it's in the range 0 - 2pi.
 * For positive angles this is just (angle % 2pi), but the Java
 * mod operator doesn't work that way for negative numbers....
 */
inline static double norm2PI(double angle)  {
    return normalize(angle, CalendarAstronomer::PI * 2.0);
}

/**
 * Normalize an angle into the range -PI - PI
 */
inline static  double normPI(double angle)  {
    return normalize(angle + CalendarAstronomer::PI, CalendarAstronomer::PI * 2.0) - CalendarAstronomer::PI;
}

//-------------------------------------------------------------------------
// Constructors
//-------------------------------------------------------------------------

/**
 * Construct a new <code>CalendarAstronomer</code> object that is initialized to
 * the current date and time.
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
CalendarAstronomer::CalendarAstronomer():
  fTime(Calendar::getNow()), moonPosition(0,0), moonPositionSet(false) {
  clearCache();
}

/**
 * Construct a new <code>CalendarAstronomer</code> object that is initialized to
 * the specified date and time.
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
CalendarAstronomer::CalendarAstronomer(UDate d): fTime(d), moonPosition(0,0), moonPositionSet(false) {
  clearCache();
}

CalendarAstronomer::~CalendarAstronomer()
{
}

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
 * @see #setDate
 * @see #getTime
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
void CalendarAstronomer::setTime(UDate aTime) {
    fTime = aTime;
    clearCache();
}

/**
 * Get the current time of this <code>CalendarAstronomer</code> object,
 * represented as the number of milliseconds since
 * 1/1/1970 AD 0:00 GMT (Gregorian).
 *
 * @see #setTime
 * @see #getDate
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
UDate CalendarAstronomer::getTime() {
    return fTime;
}

/**
 * Get the current time of this <code>CalendarAstronomer</code> object,
 * expressed as a "julian day number", which is the number of elapsed
 * days since 1/1/4713 BC (Julian), 12:00 GMT.
 *
 * @see #setJulianDay
 * @see #JULIAN_EPOCH_MS
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
double CalendarAstronomer::getJulianDay() {
    if (isINVALID(julianDay)) {
        julianDay = (fTime - JULIAN_EPOCH_MS) / static_cast<double>(DAY_MS);
    }
    return julianDay;
}

//-------------------------------------------------------------------------
// Coordinate transformations, all based on the current time of this object
//-------------------------------------------------------------------------

/**
 * Convert from ecliptic to equatorial coordinates.
 *
 * @param eclipLong     The ecliptic longitude
 * @param eclipLat      The ecliptic latitude
 *
 * @return              The corresponding point in equatorial coordinates.
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
CalendarAstronomer::Equatorial& CalendarAstronomer::eclipticToEquatorial(CalendarAstronomer::Equatorial& result, double eclipLong, double eclipLat)
{
    // See page 42 of "Practical Astronomy with your Calculator",
    // by Peter Duffet-Smith, for details on the algorithm.

    double obliq = eclipticObliquity();
    double sinE = ::sin(obliq);
    double cosE = cos(obliq);

    double sinL = ::sin(eclipLong);
    double cosL = cos(eclipLong);

    double sinB = ::sin(eclipLat);
    double cosB = cos(eclipLat);
    double tanB = tan(eclipLat);

    result.set(atan2(sinL*cosE - tanB*sinE, cosL),
        asin(sinB*cosE + cosB*sinE*sinL) );
    return result;
}

//-------------------------------------------------------------------------
// The Sun
//-------------------------------------------------------------------------

//
// Parameters of the Sun's orbit as of the epoch Jan 0.0 1990
// Angles are in radians (after multiplying by CalendarAstronomer::PI/180)
//
#define JD_EPOCH  2447891.5 // Julian day of epoch

#define SUN_ETA_G    (279.403303 * CalendarAstronomer::PI/180) // Ecliptic longitude at epoch
#define SUN_OMEGA_G  (282.768422 * CalendarAstronomer::PI/180) // Ecliptic longitude of perigee
#define SUN_E         0.016713          // Eccentricity of orbit
//double sunR0        1.495585e8        // Semi-major axis in KM
//double sunTheta0    (0.533128 * CalendarAstronomer::PI/180) // Angular diameter at R0

// The following three methods, which compute the sun parameters
// given above for an arbitrary epoch (whatever time the object is
// set to), make only a small difference as compared to using the
// above constants.  E.g., Sunset times might differ by ~12
// seconds.  Furthermore, the eta-g computation is befuddled by
// Duffet-Smith's incorrect coefficients (p.86).  I've corrected
// the first-order coefficient but the others may be off too - no
// way of knowing without consulting another source.

//  /**
//   * Return the sun's ecliptic longitude at perigee for the current time.
//   * See Duffett-Smith, p. 86.
//   * @return radians
//   */
//  private double getSunOmegaG() {
//      double T = getJulianCentury();
//      return (281.2208444 + (1.719175 + 0.000452778*T)*T) * DEG_RAD;
//  }

//  /**
//   * Return the sun's ecliptic longitude for the current time.
//   * See Duffett-Smith, p. 86.
//   * @return radians
//   */
//  private double getSunEtaG() {
//      double T = getJulianCentury();
//      //return (279.6966778 + (36000.76892 + 0.0003025*T)*T) * DEG_RAD;
//      //
//      // The above line is from Duffett-Smith, and yields manifestly wrong
//      // results.  The below constant is derived empirically to match the
//      // constant he gives for the 1990 EPOCH.
//      //
//      return (279.6966778 + (-0.3262541582718024 + 0.0003025*T)*T) * DEG_RAD;
//  }

//  /**
//   * Return the sun's eccentricity of orbit for the current time.
//   * See Duffett-Smith, p. 86.
//   * @return double
//   */
//  private double getSunE() {
//      double T = getJulianCentury();
//      return 0.01675104 - (0.0000418 + 0.000000126*T)*T;
//  }

/**
 * Find the "true anomaly" (longitude) of an object from
 * its mean anomaly and the eccentricity of its orbit.  This uses
 * an iterative solution to Kepler's equation.
 *
 * @param meanAnomaly   The object's longitude calculated as if it were in
 *                      a regular, circular orbit, measured in radians
 *                      from the point of perigee.
 *
 * @param eccentricity  The eccentricity of the orbit
 *
 * @return The true anomaly (longitude) measured in radians
 */
static double trueAnomaly(double meanAnomaly, double eccentricity)
{
    // First, solve Kepler's equation iteratively
    // Duffett-Smith, p.90
    double delta;
    double E = meanAnomaly;
    do {
        delta = E - eccentricity * ::sin(E) - meanAnomaly;
        E = E - delta / (1 - eccentricity * ::cos(E));
    }
    while (uprv_fabs(delta) > 1e-5); // epsilon = 1e-5 rad

    return 2.0 * ::atan( ::tan(E/2) * ::sqrt( (1+eccentricity)
                                             /(1-eccentricity) ) );
}

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
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
double CalendarAstronomer::getSunLongitude()
{
    // See page 86 of "Practical Astronomy with your Calculator",
    // by Peter Duffet-Smith, for details on the algorithm.

    if (isINVALID(sunLongitude)) {
        getSunLongitude(getJulianDay(), sunLongitude, meanAnomalySun);
    }
    return sunLongitude;
}

/**
 * TODO Make this public when the entire class is package-private.
 */
/*public*/ void CalendarAstronomer::getSunLongitude(double jDay, double &longitude, double &meanAnomaly)
{
    // See page 86 of "Practical Astronomy with your Calculator",
    // by Peter Duffet-Smith, for details on the algorithm.

    double day = jDay - JD_EPOCH;       // Days since epoch

    // Find the angular distance the sun in a fictitious
    // circular orbit has travelled since the epoch.
    double epochAngle = norm2PI(CalendarAstronomer_PI2/TROPICAL_YEAR*day);

    // The epoch wasn't at the sun's perigee; find the angular distance
    // since perigee, which is called the "mean anomaly"
    meanAnomaly = norm2PI(epochAngle + SUN_ETA_G - SUN_OMEGA_G);

    // Now find the "true anomaly", e.g. the real solar longitude
    // by solving Kepler's equation for an elliptical orbit
    // NOTE: The 3rd ed. of the book lists omega_g and eta_g in different
    // equations; omega_g is to be correct.
    longitude =  norm2PI(trueAnomaly(meanAnomaly, SUN_E) + SUN_OMEGA_G);
}

/**
 * Constant representing the winter solstice.
 * For use with {@link #getSunTime getSunTime}.
 * Note: In this case, "winter" refers to the northern hemisphere's seasons.
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
double CalendarAstronomer::WINTER_SOLSTICE() {
    return  ((CalendarAstronomer::PI*3)/2);
}

CalendarAstronomer::AngleFunc::~AngleFunc() {}

/**
 * Find the next time at which the sun's ecliptic longitude will have
 * the desired value.
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
class SunTimeAngleFunc : public CalendarAstronomer::AngleFunc {
public:
    virtual ~SunTimeAngleFunc();
    virtual double eval(CalendarAstronomer& a) override { return a.getSunLongitude(); }
};

SunTimeAngleFunc::~SunTimeAngleFunc() {}

UDate CalendarAstronomer::getSunTime(double desired, UBool next)
{
    SunTimeAngleFunc func;
    return timeOfAngle( func,
                        desired,
                        TROPICAL_YEAR,
                        MINUTE_MS,
                        next);
}

//-------------------------------------------------------------------------
// The Moon
//-------------------------------------------------------------------------

#define moonL0  (318.351648 * CalendarAstronomer::PI/180 )   // Mean long. at epoch
#define moonP0 ( 36.340410 * CalendarAstronomer::PI/180 )   // Mean long. of perigee
#define moonN0 ( 318.510107 * CalendarAstronomer::PI/180 )   // Mean long. of node
#define moonI  (   5.145366 * CalendarAstronomer::PI/180 )   // Inclination of orbit
#define moonE  (   0.054900 )            // Eccentricity of orbit

// These aren't used right now
#define moonA  (   3.84401e5 )           // semi-major axis (km)
#define moonT0 (   0.5181 * CalendarAstronomer::PI/180 )     // Angular size at distance A
#define moonPi (   0.9507 * CalendarAstronomer::PI/180 )     // Parallax at distance A

/**
 * The position of the moon at the time set on this
 * object, in equatorial coordinates.
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
const CalendarAstronomer::Equatorial& CalendarAstronomer::getMoonPosition()
{
    //
    // See page 142 of "Practical Astronomy with your Calculator",
    // by Peter Duffet-Smith, for details on the algorithm.
    //
    if (moonPositionSet == false) {
        // Calculate the solar longitude.  Has the side effect of
        // filling in "meanAnomalySun" as well.
        getSunLongitude();

        //
        // Find the # of days since the epoch of our orbital parameters.
        // TODO: Convert the time of day portion into ephemeris time
        //
        double day = getJulianDay() - JD_EPOCH;       // Days since epoch

        // Calculate the mean longitude and anomaly of the moon, based on
        // a circular orbit.  Similar to the corresponding solar calculation.
        double meanLongitude = norm2PI(13.1763966*PI/180*day + moonL0);
        double meanAnomalyMoon = norm2PI(meanLongitude - 0.1114041*PI/180 * day - moonP0);

        //
        // Calculate the following corrections:
        //  Evection:   the sun's gravity affects the moon's eccentricity
        //  Annual Eqn: variation in the effect due to earth-sun distance
        //  A3:         correction factor (for ???)
        //
        double evection = 1.2739*PI/180 * ::sin(2 * (meanLongitude - sunLongitude)
            - meanAnomalyMoon);
        double annual   = 0.1858*PI/180 * ::sin(meanAnomalySun);
        double a3       = 0.3700*PI/180 * ::sin(meanAnomalySun);

        meanAnomalyMoon += evection - annual - a3;

        //
        // More correction factors:
        //  center  equation of the center correction
        //  a4      yet another error correction (???)
        //
        // TODO: Skip the equation of the center correction and solve Kepler's eqn?
        //
        double center = 6.2886*PI/180 * ::sin(meanAnomalyMoon);
        double a4 =     0.2140*PI/180 * ::sin(2 * meanAnomalyMoon);

        // Now find the moon's corrected longitude
        double moonLongitude = meanLongitude + evection + center - annual + a4;

        //
        // And finally, find the variation, caused by the fact that the sun's
        // gravitational pull on the moon varies depending on which side of
        // the earth the moon is on
        //
        double variation = 0.6583*CalendarAstronomer::PI/180 * ::sin(2*(moonLongitude - sunLongitude));

        moonLongitude += variation;

        //
        // What we've calculated so far is the moon's longitude in the plane
        // of its own orbit.  Now map to the ecliptic to get the latitude
        // and longitude.  First we need to find the longitude of the ascending
        // node, the position on the ecliptic where it is crossed by the moon's
        // orbit as it crosses from the southern to the northern hemisphere.
        //
        double nodeLongitude = norm2PI(moonN0 - 0.0529539*PI/180 * day);

        nodeLongitude -= 0.16*PI/180 * ::sin(meanAnomalySun);

        double y = ::sin(moonLongitude - nodeLongitude);
        double x = cos(moonLongitude - nodeLongitude);

        moonEclipLong = ::atan2(y*cos(moonI), x) + nodeLongitude;
        double moonEclipLat = ::asin(y * ::sin(moonI));

        eclipticToEquatorial(moonPosition, moonEclipLong, moonEclipLat);
        moonPositionSet = true;
    }
    return moonPosition;
}

/**
 * The "age" of the moon at the time specified in this object.
 * This is really the angle between the
 * current ecliptic longitudes of the sun and the moon,
 * measured in radians.
 *
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
double CalendarAstronomer::getMoonAge() {
    // See page 147 of "Practical Astronomy with your Calculator",
    // by Peter Duffet-Smith, for details on the algorithm.
    //
    // Force the moon's position to be calculated.  We're going to use
    // some the intermediate results cached during that calculation.
    //
    getMoonPosition();

    return norm2PI(moonEclipLong - sunLongitude);
}

/**
 * Constant representing a new moon.
 * For use with {@link #getMoonTime getMoonTime}
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
CalendarAstronomer::MoonAge CalendarAstronomer::NEW_MOON() {
    return  CalendarAstronomer::MoonAge(0);
}

/**
 * Constant representing the moon's last quarter.
 * For use with {@link #getMoonTime getMoonTime}
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */

class MoonTimeAngleFunc : public CalendarAstronomer::AngleFunc {
public:
    virtual ~MoonTimeAngleFunc();
    virtual double eval(CalendarAstronomer& a) override { return a.getMoonAge(); }
};

MoonTimeAngleFunc::~MoonTimeAngleFunc() {}

/*const CalendarAstronomer::MoonAge CalendarAstronomer::LAST_QUARTER() {
  return  CalendarAstronomer::MoonAge((CalendarAstronomer::PI*3)/2);
}*/

/**
 * Find the next or previous time at which the moon will be in the
 * desired phase.
 * <p>
 * @param desired   The desired phase of the moon.
 * @param next      <tt>true</tt> if the next occurrence of the phase
 *                  is desired, <tt>false</tt> for the previous occurrence.
 * @internal
 * @deprecated ICU 2.4. This class may be removed or modified.
 */
UDate CalendarAstronomer::getMoonTime(const CalendarAstronomer::MoonAge& desired, UBool next) {
    MoonTimeAngleFunc func;
    return timeOfAngle( func,
                        desired.value,
                        SYNODIC_MONTH,
                        MINUTE_MS,
                        next);
}

//-------------------------------------------------------------------------
// Interpolation methods for finding the time at which a given event occurs
//-------------------------------------------------------------------------

UDate CalendarAstronomer::timeOfAngle(AngleFunc& func, double desired,
                                      double periodDays, double epsilon, UBool next)
{
    // Find the value of the function at the current time
    double lastAngle = func.eval(*this);

    // Find out how far we are from the desired angle
    double deltaAngle = norm2PI(desired - lastAngle) ;

    // Using the average period, estimate the next (or previous) time at
    // which the desired angle occurs.
    double deltaT =  (deltaAngle + (next ? 0.0 : - CalendarAstronomer_PI2 )) * (periodDays*DAY_MS) / CalendarAstronomer_PI2;

    double lastDeltaT = deltaT; // Liu
    UDate startTime = fTime; // Liu

    setTime(fTime + uprv_ceil(deltaT));

    // Now iterate until we get the error below epsilon.  Throughout
    // this loop we use normPI to get values in the range -Pi to Pi,
    // since we're using them as correction factors rather than absolute angles.
    do {
        // Evaluate the function at the time we've estimated
        double angle = func.eval(*this);

        // Find the # of milliseconds per radian at this point on the curve
        double factor = uprv_fabs(deltaT / normPI(angle-lastAngle));

        // Correct the time estimate based on how far off the angle is
        deltaT = normPI(desired - angle) * factor;

        // HACK:
        //
        // If abs(deltaT) begins to diverge we need to quit this loop.
        // This only appears to happen when attempting to locate, for
        // example, a new moon on the day of the new moon.  E.g.:
        //
        // This result is correct:
        // newMoon(7508(Mon Jul 23 00:00:00 CST 1990,false))=
        //   Sun Jul 22 10:57:41 CST 1990
        //
        // But attempting to make the same call a day earlier causes deltaT
        // to diverge:
        // CalendarAstronomer.timeOfAngle() diverging: 1.348508727575625E9 ->
        //   1.3649828540224032E9
        // newMoon(7507(Sun Jul 22 00:00:00 CST 1990,false))=
        //   Sun Jul 08 13:56:15 CST 1990
        //
        // As a temporary solution, we catch this specific condition and
        // adjust our start time by one eighth period days (either forward
        // or backward) and try again.
        // Liu 11/9/00
        if (uprv_fabs(deltaT) > uprv_fabs(lastDeltaT)) {
            double delta = uprv_ceil (periodDays * DAY_MS / 8.0);
            setTime(startTime + (next ? delta : -delta));
            return timeOfAngle(func, desired, periodDays, epsilon, next);
        }

        lastDeltaT = deltaT;
        lastAngle = angle;

        setTime(fTime + uprv_ceil(deltaT));
    }
    while (uprv_fabs(deltaT) > epsilon);

    return fTime;
}

/**
 * Return the obliquity of the ecliptic (the angle between the ecliptic
 * and the earth's equator) at the current time.  This varies due to
 * the precession of the earth's axis.
 *
 * @return  the obliquity of the ecliptic relative to the equator,
 *          measured in radians.
 */
double CalendarAstronomer::eclipticObliquity() {
    const double epoch = 2451545.0;     // 2000 AD, January 1.5

    double T = (getJulianDay() - epoch) / 36525;

    double eclipObliquity = 23.439292
        - 46.815/3600 * T
        - 0.0006/3600 * T*T
        + 0.00181/3600 * T*T*T;

    return eclipObliquity * DEG_RAD;
}


//-------------------------------------------------------------------------
// Private data
//-------------------------------------------------------------------------
void CalendarAstronomer::clearCache() {
    const double INVALID = uprv_getNaN();

    julianDay       = INVALID;
    sunLongitude    = INVALID;
    meanAnomalySun  = INVALID;
    moonEclipLong   = INVALID;

    moonPositionSet = false;
}

// Debugging functions
UnicodeString CalendarAstronomer::Ecliptic::toString() const
{
#ifdef U_DEBUG_ASTRO
    char tmp[800];
    snprintf(tmp, sizeof(tmp), "[%.5f,%.5f]", longitude*RAD_DEG, latitude*RAD_DEG);
    return UnicodeString(tmp, "");
#else
    return {};
#endif
}

UnicodeString CalendarAstronomer::Equatorial::toString() const
{
#ifdef U_DEBUG_ASTRO
    char tmp[400];
    snprintf(tmp, sizeof(tmp), "%f,%f",
        (ascension*RAD_DEG), (declination*RAD_DEG));
    return UnicodeString(tmp, "");
#else
    return {};
#endif
}


// =============== Calendar Cache ================

void CalendarCache::createCache(CalendarCache** cache, UErrorCode& status) {
    ucln_i18n_registerCleanup(UCLN_I18N_ASTRO_CALENDAR, calendar_astro_cleanup);
    if(cache == nullptr) {
        status = U_MEMORY_ALLOCATION_ERROR;
    } else {
        *cache = new CalendarCache(32, status);
        if(U_FAILURE(status)) {
            delete *cache;
            *cache = nullptr;
        }
    }
}

int32_t CalendarCache::get(CalendarCache** cache, int32_t key, UErrorCode &status) {
    int32_t res;

    if(U_FAILURE(status)) {
        return 0;
    }
    umtx_lock(&ccLock);

    if(*cache == nullptr) {
        createCache(cache, status);
        if(U_FAILURE(status)) {
            umtx_unlock(&ccLock);
            return 0;
        }
    }

    res = uhash_igeti((*cache)->fTable, key);
    U_DEBUG_ASTRO_MSG(("%p: GET: [%d] == %d\n", (*cache)->fTable, key, res));

    umtx_unlock(&ccLock);
    return res;
}

void CalendarCache::put(CalendarCache** cache, int32_t key, int32_t value, UErrorCode &status) {
    if(U_FAILURE(status)) {
        return;
    }
    umtx_lock(&ccLock);

    if(*cache == nullptr) {
        createCache(cache, status);
        if(U_FAILURE(status)) {
            umtx_unlock(&ccLock);
            return;
        }
    }

    uhash_iputi((*cache)->fTable, key, value, &status);
    U_DEBUG_ASTRO_MSG(("%p: PUT: [%d] := %d\n", (*cache)->fTable, key, value));

    umtx_unlock(&ccLock);
}

CalendarCache::CalendarCache(int32_t size, UErrorCode &status) {
    fTable = uhash_openSize(uhash_hashLong, uhash_compareLong, nullptr, size, &status);
    U_DEBUG_ASTRO_MSG(("%p: Opening.\n", fTable));
}

CalendarCache::~CalendarCache() {
    if(fTable != nullptr) {
        U_DEBUG_ASTRO_MSG(("%p: Closing.\n", fTable));
        uhash_close(fTable);
    }
}

U_NAMESPACE_END

#endif //  !UCONFIG_NO_FORMATTING
