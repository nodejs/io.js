// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2007-2013, International Business Machines Corporation and    *
* others. All Rights Reserved.                                                *
*******************************************************************************
*/
#ifndef ZONEMETA_H
#define ZONEMETA_H

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/unistr.h"
#include "hash.h"

U_NAMESPACE_BEGIN

struct OlsonToMetaMappingEntry : public UMemory {
    const char16_t *mzid; // const because it's a reference to a resource bundle string.
    UDate from;
    UDate to;
};

class UVector;
class TimeZone;

class U_I18N_API ZoneMeta {
public:
    /**
     * Return the canonical id for this tzid defined by CLDR, which might be the id itself.
     * If the given system tzid is not known, U_ILLEGAL_ARGUMENT_ERROR is set in the status.
     *
     * Note: this internal API supports all known system IDs and "Etc/Unknown" (which is
     * NOT a system ID).
     */
    static UnicodeString& U_EXPORT2 getCanonicalCLDRID(const UnicodeString &tzid, UnicodeString &systemID, UErrorCode& status);

    /**
     * Return the canonical id for this tzid defined by CLDR, which might be the id itself.
     * This overload method returns a persistent const char16_t*, which is guaranteed to persist
     * (a pointer to a resource). If the given system tzid is not known, U_ILLEGAL_ARGUMENT_ERROR
     * is set in the status.
     * @param tzid Zone ID
     * @param status Receives the status
     * @return The canonical ID for the input time zone ID
     */
    static const char16_t* U_EXPORT2 getCanonicalCLDRID(const UnicodeString &tzid, UErrorCode& status);

    /*
     * Convenient method returning CLDR canonical ID for the given time zone
     */
    static const char16_t* U_EXPORT2 getCanonicalCLDRID(const TimeZone& tz);

    /**
     * Returns primary IANA zone ID for the input zone ID, which might be the id itself.
     * If the given system tzid is not known, U_ILLEGAL_ARGUMENT_ERROR is set in the status.
     *
     * @param tzid      Zone ID
     * @param ianaID    Output IANA ID
     * @param status    Receives the status
     * @return  A primary IANA zone ID equivalent to the input zone ID.
     */
    static UnicodeString& U_EXPORT2 getIanaID(const UnicodeString& tzid, UnicodeString& ianaID, UErrorCode& status);

    /**
     * Return the canonical country code for this tzid.  If we have none, or if the time zone
     * is not associated with a country, return bogus string.
     * @param tzid Zone ID
     * @param country [output] Country code
     * @param isPrimary [output] true if the zone is the primary zone for the country
     * @return A reference to the result country
     */
    static UnicodeString& U_EXPORT2 getCanonicalCountry(const UnicodeString &tzid, UnicodeString &country, UBool *isPrimary = nullptr);

    /**
     * Returns a CLDR metazone ID for the given Olson tzid and time.
     */
    static UnicodeString& U_EXPORT2 getMetazoneID(const UnicodeString &tzid, UDate date, UnicodeString &result);
    /**
     * Returns an Olson ID for the ginve metazone and region
     */
    static UnicodeString& U_EXPORT2 getZoneIdByMetazone(const UnicodeString &mzid, const UnicodeString &region, UnicodeString &result);

    static const UVector* U_EXPORT2 getMetazoneMappings(const UnicodeString &tzid);

    static const UVector* U_EXPORT2 getAvailableMetazoneIDs();

    /**
     * Returns the pointer to the persistent time zone ID string, or nullptr if the given tzid is not in the
     * tz database. This method is useful when you maintain persistent zone IDs without duplication.
     */
    static const char16_t* U_EXPORT2 findTimeZoneID(const UnicodeString& tzid);

    /**
     * Returns the pointer to the persistent meta zone ID string, or nullptr if the given mzid is not available.
     * This method is useful when you maintain persistent meta zone IDs without duplication.
     */
    static const char16_t* U_EXPORT2 findMetaZoneID(const UnicodeString& mzid);

    /**
     * Creates a custom zone for the offset
     * @param offset GMT offset in milliseconds
     * @return A custom TimeZone for the offset with normalized time zone id
     */
    static TimeZone* createCustomTimeZone(int32_t offset);

    /**
     * Returns the time zone's short ID (null terminated) for the zone.
     * For example, "uslax" for zone "America/Los_Angeles".
     * @param tz the time zone
     * @return the short ID of the time zone, or null if the short ID is not available.
     */
    static const char16_t* U_EXPORT2 getShortID(const TimeZone& tz);

    /**
     * Returns the time zone's short ID (null terminated) for the zone ID.
     * For example, "uslax" for zone ID "America/Los_Angeles".
     * @param tz the time zone ID
     * @return the short ID of the time zone ID, or null if the short ID is not available.
     */
    static const char16_t* U_EXPORT2 getShortID(const UnicodeString& id);

private:
    ZoneMeta() = delete; // Prevent construction.
    static UVector* createMetazoneMappings(const UnicodeString &tzid);
    static UnicodeString& formatCustomID(uint8_t hour, uint8_t min, uint8_t sec, UBool negative, UnicodeString& id);
    static const char16_t* getShortIDFromCanonical(const char16_t* canonicalID);
};

U_NAMESPACE_END

#endif /* #if !UCONFIG_NO_FORMATTING */
#endif // ZONEMETA_H
