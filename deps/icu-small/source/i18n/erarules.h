// © 2018 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html

#ifndef ERARULES_H_
#define ERARULES_H_

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include "unicode/localpointer.h"
#include "unicode/uobject.h"
#include "cmemory.h"

U_NAMESPACE_BEGIN

// Export an explicit template instantiation of LocalMemory used as a data member of EraRules.
// When building DLLs for Windows this is required even though no direct access leaks out of the i18n library.
// See digitlst.h, pluralaffix.h, datefmt.h, and others for similar examples.
#if U_PF_WINDOWS <= U_PLATFORM && U_PLATFORM <= U_PF_CYGWIN
template class U_I18N_API LocalPointerBase<int32_t>;
template class U_I18N_API LocalMemory<int32_t>;
#endif

class U_I18N_API EraRules : public UMemory {
public:
    ~EraRules();

    static EraRules* createInstance(const char *calType, UBool includeTentativeEra, UErrorCode& status);

    /**
     * Gets number of effective eras
     * @return  number of effective eras
     */
    inline int32_t getNumberOfEras() const {
        return numEras;
    }

    /**
     * Gets start date of an era
     * @param eraIdx    Era index
     * @param fields    Receives date fields. The result includes values of year, month,
     *                  day of month in this order. When an era has no start date, the result
     *                  will be January 1st in year whose value is minimum integer.
     * @param status    Receives status.
     */
    void getStartDate(int32_t eraIdx, int32_t (&fields)[3], UErrorCode& status) const;

    /**
     * Gets start year of an era
     * @param eraIdx    Era index
     * @param status    Receives status.
     * @return The first year of an era. When a era has no start date, minimum int32
     *          value is returned.
     */
    int32_t getStartYear(int32_t eraIdx, UErrorCode& status) const;

    /**
     * Returns era index for the specified year/month/day.
     * @param year  Year
     * @param month Month (1-base)
     * @param day   Day of month
     * @param status    Receives status
     * @return  era index (or 0, when the specified date is before the first era)
     */
    int32_t getEraIndex(int32_t year, int32_t month, int32_t day, UErrorCode& status) const;

    /**
     * Gets the current era index. This is calculated only once for an instance of
     * EraRules. The current era calculation is based on the default time zone at
     * the time of instantiation.
     *
     * @return era index of current era (or 0, when current date is before the first era)
     */
    inline int32_t getCurrentEraIndex() const {
        return currentEra;
    }

private:
    EraRules(LocalMemory<int32_t>& eraStartDates, int32_t numEra);

    void initCurrentEra();

    LocalMemory<int32_t> startDates;
    int32_t numEras;
    int32_t currentEra;
};

U_NAMESPACE_END
#endif /* #if !UCONFIG_NO_FORMATTING */
#endif /* ERARULES_H_ */
