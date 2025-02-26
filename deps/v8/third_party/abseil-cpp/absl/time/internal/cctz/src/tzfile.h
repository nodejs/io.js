/* Layout and location of TZif files.  */

#ifndef TZFILE_H

#define TZFILE_H

/*
** This file is in the public domain, so clarified as of
** 1996-06-05 by Arthur David Olson.
*/

/*
** This header is for use ONLY with the time conversion code.
** There is no guarantee that it will remain unchanged,
** or that it will remain at all.
** Do NOT copy it to any system include directory.
** Thank you!
*/

/*
** Information about time zone files.
*/

#ifndef TZDEFRULES
#define TZDEFRULES "posixrules"
#endif /* !defined TZDEFRULES */

/* See Internet RFC 9636 for more details about the following format.  */

/*
** Each file begins with. . .
*/

#define TZ_MAGIC "TZif"

struct tzhead {
  char tzh_magic[4];      /* TZ_MAGIC */
  char tzh_version[1];    /* '\0' or '2'-'4' as of 2021 */
  char tzh_reserved[15];  /* reserved; must be zero */
  char tzh_ttisutcnt[4];  /* coded number of trans. time flags */
  char tzh_ttisstdcnt[4]; /* coded number of trans. time flags */
  char tzh_leapcnt[4];    /* coded number of leap seconds */
  char tzh_timecnt[4];    /* coded number of transition times */
  char tzh_typecnt[4];    /* coded number of local time types */
  char tzh_charcnt[4];    /* coded number of abbr. chars */
};

/*
** . . .followed by. . .
**
**	tzh_timecnt (char [4])s		coded transition times a la time(2)
**	tzh_timecnt (unsigned char)s	types of local time starting at above
**	tzh_typecnt repetitions of
**		one (char [4])		coded UT offset in seconds
**		one (unsigned char)	used to set tm_isdst
**		one (unsigned char)	that's an abbreviation list index
**	tzh_charcnt (char)s		'\0'-terminated zone abbreviations
**	tzh_leapcnt repetitions of
**		one (char [4])		coded leap second transition times
**		one (char [4])		total correction after above
**	tzh_ttisstdcnt (char)s		indexed by type; if 1, transition
**					time is standard time, if 0,
**					transition time is local (wall clock)
**					time; if absent, transition times are
**					assumed to be local time
**	tzh_ttisutcnt (char)s		indexed by type; if 1, transition
**					time is UT, if 0, transition time is
**					local time; if absent, transition
**					times are assumed to be local time.
**					When this is 1, the corresponding
**					std/wall indicator must also be 1.
*/

/*
** If tzh_version is '2' or greater, the above is followed by a second instance
** of tzhead and a second instance of the data in which each coded transition
** time uses 8 rather than 4 chars,
** then a POSIX.1-2017 proleptic TZ string for use in handling
** instants after the last transition time stored in the file
** (with nothing between the newlines if there is no POSIX.1-2017
** representation for such instants).
**
** If tz_version is '3' or greater, the TZ string can be any POSIX.1-2024
** proleptic TZ string, which means the above is extended as follows.
** First, the TZ string's hour offset may range from -167
** through 167 as compared to the range 0 through 24 required
** by POSIX.1-2017 and earlier.
** Second, its DST start time may be January 1 at 00:00 and its stop
** time December 31 at 24:00 plus the difference between DST and
** standard time, indicating DST all year.
*/

/*
** In the current implementation, "tzset()" refuses to deal with files that
** exceed any of the limits below.
*/

#ifndef TZ_MAX_TIMES
/* This must be at least 242 for Europe/London with 'zic -b fat'.  */
#define TZ_MAX_TIMES 2000
#endif /* !defined TZ_MAX_TIMES */

#ifndef TZ_MAX_TYPES
/* This must be at least 18 for Europe/Vilnius with 'zic -b fat'.  */
#define TZ_MAX_TYPES 256 /* Limited by what (unsigned char)'s can hold */
#endif                   /* !defined TZ_MAX_TYPES */

#ifndef TZ_MAX_CHARS
/* This must be at least 40 for America/Anchorage.  */
#define TZ_MAX_CHARS 50 /* Maximum number of abbreviation characters */
                        /* (limited by what unsigned chars can hold) */
#endif                  /* !defined TZ_MAX_CHARS */

#ifndef TZ_MAX_LEAPS
/* This must be at least 27 for leap seconds from 1972 through mid-2023.
   There's a plan to discontinue leap seconds by 2035.  */
#define TZ_MAX_LEAPS 50 /* Maximum number of leap second corrections */
#endif                  /* !defined TZ_MAX_LEAPS */

#endif /* !defined TZFILE_H */
