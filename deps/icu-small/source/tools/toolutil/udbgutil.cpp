// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/********************************************************************
 * COPYRIGHT:
 * Copyright (c) 2007-2016, International Business Machines Corporation and
 * others. All Rights Reserved.
 ********************************************************************/

#include "udbgutil.h"
#include <string.h>
#include "ustr_imp.h"
#include "cmemory.h"
#include "cstring.h"
#include "putilimp.h"
#include "unicode/ulocdata.h"
#include "unicode/ucnv.h"
#include "unicode/unistr.h"
#include "cstr.h"

/*
To add a new enum type
      (For example: UShoeSize  with values USHOE_WIDE=0, USHOE_REGULAR, USHOE_NARROW, USHOE_COUNT)

    0. Make sure that all lines you add are protected with appropriate uconfig guards,
        such as '#if !UCONFIG_NO_SHOES'.
    1. udbgutil.h:  add  UDBG_UShoeSize to the UDebugEnumType enum before UDBG_ENUM_COUNT
      ( The subsequent steps involve this file, udbgutil.cpp )
    2. Find the marker "Add new enum types above this line"
    3. Before that marker, add a #include of any header file you need.
    4. Each enum type has three things in this section:  a #define, a count_, and an array of Fields.
       It may help to copy and paste a previous definition.
    5. In the case of the USHOE_... strings above, "USHOE_" is common to all values- six characters
         " #define LEN_USHOE 6 "
       6 characters will strip off "USHOE_" leaving enum values of WIDE, REGULAR, and NARROW.
    6. Define the 'count_' variable, with the number of enum values. If the enum has a _MAX or _COUNT value,
        that can be helpful for automatically defining the count. Otherwise define it manually.
        " static const int32_t count_UShoeSize = USHOE_COUNT; "
    7. Define the field names, in order.
        " static const Field names_UShoeSize[] =  {
        "  FIELD_NAME_STR( LEN_USHOE, USHOE_WIDE ),
        "  FIELD_NAME_STR( LEN_USHOE, USHOE_REGULAR ),
        "  FIELD_NAME_STR( LEN_USHOE, USHOE_NARROW ),
        " };
      ( The following command  was usedfor converting ucol.h into partially correct entities )
      grep "^[  ]*UCOL" < unicode/ucol.h  |
         sed -e 's%^[  ]*\([A-Z]*\)_\([A-Z_]*\).*%   FIELD_NAME_STR( LEN_\1, \1_\2 ),%g'
    8. Now, a bit farther down, add the name of the enum itself to the end of names_UDebugEnumType
          ( UDebugEnumType is an enum, too!)
        names_UDebugEnumType[] { ...
            " FIELD_NAME_STR( LEN_UDBG, UDBG_UShoeSize ),   "
    9. Find the function _udbg_enumCount  and add the count macro:
            " COUNT_CASE(UShoeSize)
   10. Find the function _udbg_enumFields  and add the field macro:
            " FIELD_CASE(UShoeSize)
   11. verify that your test code, and Java data generation, works properly.
*/

/**
 * Structure representing an enum value
 */
struct Field {
    int32_t prefix;   /**< how many characters to remove in the prefix - i.e. UCHAR_ = 5 */
	const char *str;  /**< The actual string value */
	int32_t num;      /**< The numeric value */
};

/**
 * Define another field name. Used in an array of Field s
 * @param y the common prefix length (i.e. 6 for "USHOE_" )
 * @param x the actual enum value - it will be copied in both string and symbolic form.
 * @see Field
 */
#define FIELD_NAME_STR(y,x)  { y, #x, x }


// TODO: Currently, this whole functionality goes away with UCONFIG_NO_FORMATTING. Should be split up.
#if !UCONFIG_NO_FORMATTING

// Calendar
#include "unicode/ucal.h"

// 'UCAL_' = 5
#define LEN_UCAL 5 /* UCAL_ */
static const int32_t count_UCalendarDateFields = UCAL_FIELD_COUNT;
static const Field names_UCalendarDateFields[] =
{
    FIELD_NAME_STR( LEN_UCAL, UCAL_ERA ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_YEAR ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_MONTH ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_WEEK_OF_YEAR ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_WEEK_OF_MONTH ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_DATE ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_DAY_OF_YEAR ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_DAY_OF_WEEK ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_DAY_OF_WEEK_IN_MONTH ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_AM_PM ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_HOUR ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_HOUR_OF_DAY ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_MINUTE ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_SECOND ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_MILLISECOND ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_ZONE_OFFSET ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_DST_OFFSET ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_YEAR_WOY ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_DOW_LOCAL ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_EXTENDED_YEAR ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_JULIAN_DAY ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_MILLISECONDS_IN_DAY ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_IS_LEAP_MONTH ),
    FIELD_NAME_STR( LEN_UCAL, UCAL_ORDINAL_MONTH ),
};


static const int32_t count_UCalendarMonths = UCAL_UNDECIMBER+1;
static const Field names_UCalendarMonths[] =
{
  FIELD_NAME_STR( LEN_UCAL, UCAL_JANUARY ),
  FIELD_NAME_STR( LEN_UCAL, UCAL_FEBRUARY ),
  FIELD_NAME_STR( LEN_UCAL, UCAL_MARCH ),
  FIELD_NAME_STR( LEN_UCAL, UCAL_APRIL ),
  FIELD_NAME_STR( LEN_UCAL, UCAL_MAY ),
  FIELD_NAME_STR( LEN_UCAL, UCAL_JUNE ),
  FIELD_NAME_STR( LEN_UCAL, UCAL_JULY ),
  FIELD_NAME_STR( LEN_UCAL, UCAL_AUGUST ),
  FIELD_NAME_STR( LEN_UCAL, UCAL_SEPTEMBER ),
  FIELD_NAME_STR( LEN_UCAL, UCAL_OCTOBER ),
  FIELD_NAME_STR( LEN_UCAL, UCAL_NOVEMBER ),
  FIELD_NAME_STR( LEN_UCAL, UCAL_DECEMBER ),
  FIELD_NAME_STR( LEN_UCAL, UCAL_UNDECIMBER)
};

#include "unicode/udat.h"

#define LEN_UDAT 5 /* "UDAT_" */
static const int32_t count_UDateFormatStyle = UDAT_SHORT+1;
static const Field names_UDateFormatStyle[] =
{
        FIELD_NAME_STR( LEN_UDAT, UDAT_FULL ),
        FIELD_NAME_STR( LEN_UDAT, UDAT_LONG ),
        FIELD_NAME_STR( LEN_UDAT, UDAT_MEDIUM ),
        FIELD_NAME_STR( LEN_UDAT, UDAT_SHORT ),
        /* end regular */
    /*
     *  negative enums.. leave out for now.
        FIELD_NAME_STR( LEN_UDAT, UDAT_NONE ),
        FIELD_NAME_STR( LEN_UDAT, UDAT_PATTERN ),
     */
};

#endif

#include "unicode/uloc.h"

#define LEN_UAR 12 /* "ULOC_ACCEPT_" */
static const int32_t count_UAcceptResult = 3;
static const Field names_UAcceptResult[] =
{
        FIELD_NAME_STR( LEN_UAR, ULOC_ACCEPT_FAILED ),
        FIELD_NAME_STR( LEN_UAR, ULOC_ACCEPT_VALID ),
        FIELD_NAME_STR( LEN_UAR, ULOC_ACCEPT_FALLBACK ),
};

#if !UCONFIG_NO_COLLATION
#include "unicode/ucol.h"
#define LEN_UCOL 5 /* UCOL_ */
static const int32_t count_UColAttributeValue = UCOL_ATTRIBUTE_VALUE_COUNT;
static const Field names_UColAttributeValue[]  = {
   FIELD_NAME_STR( LEN_UCOL, UCOL_PRIMARY ),
   FIELD_NAME_STR( LEN_UCOL, UCOL_SECONDARY ),
   FIELD_NAME_STR( LEN_UCOL, UCOL_TERTIARY ),
//   FIELD_NAME_STR( LEN_UCOL, UCOL_CE_STRENGTH_LIMIT ),
   FIELD_NAME_STR( LEN_UCOL, UCOL_QUATERNARY ),
   // gap
   FIELD_NAME_STR( LEN_UCOL, UCOL_IDENTICAL ),
//   FIELD_NAME_STR( LEN_UCOL, UCOL_STRENGTH_LIMIT ),
   FIELD_NAME_STR( LEN_UCOL, UCOL_OFF ),
   FIELD_NAME_STR( LEN_UCOL, UCOL_ON ),
   // gap
   FIELD_NAME_STR( LEN_UCOL, UCOL_SHIFTED ),
   FIELD_NAME_STR( LEN_UCOL, UCOL_NON_IGNORABLE ),
   // gap
   FIELD_NAME_STR( LEN_UCOL, UCOL_LOWER_FIRST ),
   FIELD_NAME_STR( LEN_UCOL, UCOL_UPPER_FIRST ),
};

#endif


#if UCONFIG_ENABLE_PLUGINS
#include "unicode/icuplug.h"

#define LEN_UPLUG_REASON 13 /* UPLUG_REASON_ */
static const int32_t count_UPlugReason = UPLUG_REASON_COUNT;
static const Field names_UPlugReason[]  = {
   FIELD_NAME_STR( LEN_UPLUG_REASON, UPLUG_REASON_QUERY ),
   FIELD_NAME_STR( LEN_UPLUG_REASON, UPLUG_REASON_LOAD ),
   FIELD_NAME_STR( LEN_UPLUG_REASON, UPLUG_REASON_UNLOAD ),
};

#define LEN_UPLUG_LEVEL 12 /* UPLUG_LEVEL_ */
static const int32_t count_UPlugLevel = UPLUG_LEVEL_COUNT;
static const Field names_UPlugLevel[]  = {
   FIELD_NAME_STR( LEN_UPLUG_LEVEL, UPLUG_LEVEL_INVALID ),
   FIELD_NAME_STR( LEN_UPLUG_LEVEL, UPLUG_LEVEL_UNKNOWN ),
   FIELD_NAME_STR( LEN_UPLUG_LEVEL, UPLUG_LEVEL_LOW ),
   FIELD_NAME_STR( LEN_UPLUG_LEVEL, UPLUG_LEVEL_HIGH ),
};
#endif

#define LEN_UDBG 5 /* "UDBG_" */
static const int32_t count_UDebugEnumType = UDBG_ENUM_COUNT;
static const Field names_UDebugEnumType[] =
{
    FIELD_NAME_STR( LEN_UDBG, UDBG_UDebugEnumType ),
#if !UCONFIG_NO_FORMATTING
    FIELD_NAME_STR( LEN_UDBG, UDBG_UCalendarDateFields ),
    FIELD_NAME_STR( LEN_UDBG, UDBG_UCalendarMonths ),
    FIELD_NAME_STR( LEN_UDBG, UDBG_UDateFormatStyle ),
#endif
#if UCONFIG_ENABLE_PLUGINS
    FIELD_NAME_STR( LEN_UDBG, UDBG_UPlugReason ),
    FIELD_NAME_STR( LEN_UDBG, UDBG_UPlugLevel ),
#endif
    FIELD_NAME_STR( LEN_UDBG, UDBG_UAcceptResult ),
#if !UCONFIG_NO_COLLATION
    FIELD_NAME_STR( LEN_UDBG, UDBG_UColAttributeValue ),
#endif
};


// --- Add new enum types above this line ---

#define COUNT_CASE(x)  case UDBG_##x: return (actual?count_##x:UPRV_LENGTHOF(names_##x));
#define COUNT_FAIL_CASE(x) case UDBG_##x: return -1;

#define FIELD_CASE(x)  case UDBG_##x: return names_##x;
#define FIELD_FAIL_CASE(x) case UDBG_##x: return nullptr;

// low level

/**
 * @param type type of item
 * @param actual true: for the actual enum's type (UCAL_FIELD_COUNT, etc), or false for the string count
 */
static int32_t _udbg_enumCount(UDebugEnumType type, UBool actual) {
	switch(type) {
		COUNT_CASE(UDebugEnumType)
#if !UCONFIG_NO_FORMATTING
		COUNT_CASE(UCalendarDateFields)
		COUNT_CASE(UCalendarMonths)
		COUNT_CASE(UDateFormatStyle)
#endif
#if UCONFIG_ENABLE_PLUGINS
        COUNT_CASE(UPlugReason)
        COUNT_CASE(UPlugLevel)
#endif
        COUNT_CASE(UAcceptResult)
#if !UCONFIG_NO_COLLATION
        COUNT_CASE(UColAttributeValue)
#endif
		// COUNT_FAIL_CASE(UNonExistentEnum)
	default:
		return -1;
	}
}

static const Field* _udbg_enumFields(UDebugEnumType type) {
	switch(type) {
		FIELD_CASE(UDebugEnumType)
#if !UCONFIG_NO_FORMATTING
		FIELD_CASE(UCalendarDateFields)
		FIELD_CASE(UCalendarMonths)
		FIELD_CASE(UDateFormatStyle)
#endif
#if UCONFIG_ENABLE_PLUGINS
        FIELD_CASE(UPlugReason)
        FIELD_CASE(UPlugLevel)
#endif
        FIELD_CASE(UAcceptResult)
       // FIELD_FAIL_CASE(UNonExistentEnum)
#if !UCONFIG_NO_COLLATION
        FIELD_CASE(UColAttributeValue)
#endif
	default:
		return nullptr;
	}
}

// implementation

int32_t  udbg_enumCount(UDebugEnumType type) {
	return _udbg_enumCount(type, false);
}

int32_t  udbg_enumExpectedCount(UDebugEnumType type) {
	return _udbg_enumCount(type, true);
}

const char *  udbg_enumName(UDebugEnumType type, int32_t field) {
	if(field<0 ||
				field>=_udbg_enumCount(type,false)) { // also will catch unsupported items
		return nullptr;
	} else {
		const Field *fields = _udbg_enumFields(type);
		if(fields == nullptr) {
			return nullptr;
		} else {
			return fields[field].str + fields[field].prefix;
		}
	}
}

int32_t  udbg_enumArrayValue(UDebugEnumType type, int32_t field) {
	if(field<0 ||
				field>=_udbg_enumCount(type,false)) { // also will catch unsupported items
		return -1;
	} else {
		const Field *fields = _udbg_enumFields(type);
		if(fields == nullptr) {
			return -1;
		} else {
			return fields[field].num;
		}
	}
}

int32_t udbg_enumByName(UDebugEnumType type, const char *value) {
    if(type<0||type>=_udbg_enumCount(UDBG_UDebugEnumType, true)) {
        return -1; // type out of range
    }
	const Field *fields = _udbg_enumFields(type);
    if (fields != nullptr) {
        for(int32_t field = 0;field<_udbg_enumCount(type, false);field++) {
            if(!strcmp(value, fields[field].str + fields[field].prefix)) {
                return fields[field].num;
            }
        }
        // try with the prefix
        for(int32_t field = 0;field<_udbg_enumCount(type, false);field++) {
            if(!strcmp(value, fields[field].str)) {
                return fields[field].num;
            }
        }
    }
    // fail
    return -1;
}

/* platform info */
/**
 * Print the current platform
 */
U_CAPI const char *udbg_getPlatform()
{
#if U_PLATFORM_USES_ONLY_WIN32_API
    return "Windows";
#elif U_PLATFORM == U_PF_CYGWIN
    return "Cygwin";
#elif U_PLATFORM == U_PF_UNKNOWN
    return "unknown";
#elif U_PLATFORM == U_PF_DARWIN
    return "Darwin";
#elif U_PLATFORM == U_PF_BSD
    return "BSD";
#elif U_PLATFORM == U_PF_QNX
    return "QNX";
#elif U_PLATFORM == U_PF_LINUX
    return "Linux";
#elif U_PLATFORM == U_PF_ANDROID
    return "Android";
#elif U_PLATFORM == U_PF_CLASSIC_MACOS
    return "MacOS (Classic)";
#elif U_PLATFORM == U_PF_OS390
    return "IBM z";
#elif U_PLATFORM == U_PF_OS400
    return "IBM i";
#else
    return "Other (POSIX-like)";
#endif
}

struct USystemParams;

typedef int32_t U_CALLCONV USystemParameterCallback(const USystemParams *param, char *target, int32_t targetCapacity, UErrorCode *status);

struct USystemParams {
  const char *paramName;
  USystemParameterCallback *paramFunction;
  const char *paramStr;
  int32_t paramInt;
};

/* parameter types */
U_CAPI  int32_t
paramEmpty(const USystemParams * /* param */, char *target, int32_t targetCapacity, UErrorCode *status) {
  if(U_FAILURE(*status))return 0;
  return u_terminateChars(target, targetCapacity, 0, status);
}

U_CAPI  int32_t
paramStatic(const USystemParams *param, char *target, int32_t targetCapacity, UErrorCode *status) {
  if(param->paramStr==nullptr) return paramEmpty(param,target,targetCapacity,status);
  if(U_FAILURE(*status))return 0;
  int32_t len = static_cast<int32_t>(uprv_strlen(param->paramStr));
  if(target!=nullptr) {
    uprv_strncpy(target,param->paramStr,uprv_min(len,targetCapacity));
  }
  return u_terminateChars(target, targetCapacity, len, status);
}

static const char *nullString = "(null)";

static int32_t stringToStringBuffer(char *target, int32_t targetCapacity, const char *str, UErrorCode *status) {
  if(str==nullptr) str=nullString;

  int32_t len = static_cast<int32_t>(uprv_strlen(str));
  if (U_SUCCESS(*status)) {
    if(target!=nullptr) {
      uprv_strncpy(target,str,uprv_min(len,targetCapacity));
    }
  } else {
    const char *s = u_errorName(*status);
    len = static_cast<int32_t>(uprv_strlen(s));
    if(target!=nullptr) {
      uprv_strncpy(target,s,uprv_min(len,targetCapacity));
    }
  }
  return u_terminateChars(target, targetCapacity, len, status);
}

static int32_t integerToStringBuffer(char *target, int32_t targetCapacity, int32_t n, int32_t radix, UErrorCode *status) {
  if(U_FAILURE(*status)) return 0;
  char str[300];
  T_CString_integerToString(str,n,radix);
  return stringToStringBuffer(target,targetCapacity,str,status);
}

U_CAPI  int32_t
paramInteger(const USystemParams *param, char *target, int32_t targetCapacity, UErrorCode *status) {
  if(U_FAILURE(*status))return 0;
  if(param->paramStr==nullptr || param->paramStr[0]=='d') {
    return integerToStringBuffer(target,targetCapacity,param->paramInt, 10,status);
  } else if(param->paramStr[0]=='x') {
    return integerToStringBuffer(target,targetCapacity,param->paramInt, 16,status);
  } else if(param->paramStr[0]=='o') {
    return integerToStringBuffer(target,targetCapacity,param->paramInt, 8,status);
  } else if(param->paramStr[0]=='b') {
    return integerToStringBuffer(target,targetCapacity,param->paramInt, 2,status);
  } else {
    *status = U_INTERNAL_PROGRAM_ERROR;
    return 0;
  }
}


U_CAPI  int32_t
paramCldrVersion(const USystemParams * /* param */, char *target, int32_t targetCapacity, UErrorCode *status) {
  if(U_FAILURE(*status))return 0;
  char str[200]="";
  UVersionInfo icu;

  ulocdata_getCLDRVersion(icu, status);
  if(U_SUCCESS(*status)) {
    u_versionToString(icu, str);
    return stringToStringBuffer(target,targetCapacity,str,status);
  } else {
    return 0;
  }
}


#if !UCONFIG_NO_FORMATTING
U_CAPI  int32_t
paramTimezoneDefault(const USystemParams * /* param */, char *target, int32_t targetCapacity, UErrorCode *status) {
  if(U_FAILURE(*status))return 0;
  char16_t buf[100];
  char buf2[100];
  int32_t len;

  len = ucal_getDefaultTimeZone(buf, 100, status);
  if(U_SUCCESS(*status)&&len>0) {
    u_UCharsToChars(buf, buf2, len+1);
    return stringToStringBuffer(target,targetCapacity, buf2,status);
  } else {
    return 0;
  }
}
#endif

U_CAPI  int32_t
paramLocaleDefaultBcp47(const USystemParams * /* param */, char *target, int32_t targetCapacity, UErrorCode *status) {
  if(U_FAILURE(*status))return 0;
  const char *def = uloc_getDefault();
  return uloc_toLanguageTag(def,target,targetCapacity,false,status);
}


/* simple 1-liner param functions */
#define STRING_PARAM(func, str) U_CAPI  int32_t \
  func(const USystemParams *, char *target, int32_t targetCapacity, UErrorCode *status) \
  {  return stringToStringBuffer(target,targetCapacity,(str),status); }

STRING_PARAM(paramIcudataPath, u_getDataDirectory())
STRING_PARAM(paramPlatform, udbg_getPlatform())
STRING_PARAM(paramLocaleDefault, uloc_getDefault())
#if !UCONFIG_NO_CONVERSION
STRING_PARAM(paramConverterDefault, ucnv_getDefaultName())
#endif

#if !UCONFIG_NO_FORMATTING
STRING_PARAM(paramTimezoneVersion, ucal_getTZDataVersion(status))
#endif

static const USystemParams systemParams[] = {
  { "copyright",    paramStatic, U_COPYRIGHT_STRING,0 },
  { "product",      paramStatic, "icu4c",0 },
  { "product.full", paramStatic, "International Components for Unicode for C/C++",0 },
  { "version",      paramStatic, U_ICU_VERSION,0 },
  { "version.unicode", paramStatic, U_UNICODE_VERSION,0 },
  { "platform.number", paramInteger, "d",U_PLATFORM},
  { "platform.type", paramPlatform, nullptr ,0},
  { "locale.default", paramLocaleDefault, nullptr, 0},
  { "locale.default.bcp47", paramLocaleDefaultBcp47, nullptr, 0},
#if !UCONFIG_NO_CONVERSION
  { "converter.default", paramConverterDefault, nullptr, 0},
#endif
  { "icudata.name", paramStatic, U_ICUDATA_NAME, 0},
  { "icudata.path", paramIcudataPath, nullptr, 0},

  { "cldr.version", paramCldrVersion, nullptr, 0},

#if !UCONFIG_NO_FORMATTING
  { "tz.version", paramTimezoneVersion, nullptr, 0},
  { "tz.default", paramTimezoneDefault, nullptr, 0},
#endif

  { "cpu.bits",       paramInteger, "d", (sizeof(void*))*8},
  { "cpu.big_endian", paramInteger, "b", U_IS_BIG_ENDIAN},
  { "os.wchar_width", paramInteger, "d", U_SIZEOF_WCHAR_T},
  { "os.charset_family", paramInteger, "d", U_CHARSET_FAMILY},
#if defined (U_HOST)
  { "os.host", paramStatic, U_HOST, 0},
#endif
#if defined (U_BUILD)
  { "build.build", paramStatic, U_BUILD, 0},
#endif
#if defined (U_CC)
  { "build.cc", paramStatic, U_CC, 0},
#endif
#if defined (U_CXX)
  { "build.cxx", paramStatic, U_CXX, 0},
#endif
#if defined (CYGWINMSVC)
  { "build.cygwinmsvc", paramInteger, "b", 1},
#endif
  { "uconfig.internal_digitlist", paramInteger, "b", 1}, /* always 1 */
  { "uconfig.have_parseallinput", paramInteger, "b", UCONFIG_HAVE_PARSEALLINPUT},


};

#define U_SYSPARAM_COUNT UPRV_LENGTHOF(systemParams)

U_CAPI const char *udbg_getSystemParameterNameByIndex(int32_t i) {
  if (i >= 0 && i < U_SYSPARAM_COUNT) {
    return systemParams[i].paramName;
  } else {
    return nullptr;
  }
}


U_CAPI int32_t udbg_getSystemParameterValueByIndex(int32_t i, char *buffer, int32_t bufferCapacity, UErrorCode *status) {
  if (i >= 0 && i < U_SYSPARAM_COUNT) {
    return systemParams[i].paramFunction(&(systemParams[i]),buffer,bufferCapacity,status);
  } else {
    return 0;
  }
}

U_CAPI void udbg_writeIcuInfo(FILE *out) {
  char str[2000];
  /* todo: API for writing DTD? */
  fprintf(out, " <icuSystemParams type=\"icu4c\">\n");
  const char *paramName;
  for(int32_t i=0;(paramName=udbg_getSystemParameterNameByIndex(i))!=nullptr;i++) {
    UErrorCode status2 = U_ZERO_ERROR;
    udbg_getSystemParameterValueByIndex(i, str,2000,&status2);
    if(U_SUCCESS(status2)) {
      fprintf(out,"    <param name=\"%s\">%s</param>\n", paramName,str);
    } else {
      fprintf(out,"  <!-- n=\"%s\" ERROR: %s -->\n", paramName, u_errorName(status2));
    }
  }
  fprintf(out, " </icuSystemParams>\n");
}

#define UNICODE_BUG_URL "https://unicode-org.atlassian.net/browse/"
#define OLD_CLDR_PREFIX "cldrbug:"
#define CLDR_BUG_PREFIX "CLDR-"
#define ICU_BUG_PREFIX "ICU-"



#include <set>
#include <map>
#include <string>
#include <ostream>
#include <iostream>

class KnownIssues {
public:
  KnownIssues();
  ~KnownIssues();
  void add(const char *ticket, const char *where, const char16_t *msg, UBool *firstForTicket, UBool *firstForWhere);
  void add(const char *ticket, const char *where, const char *msg, UBool *firstForTicket, UBool *firstForWhere);
  UBool print();
private:
  std::map< std::string,
            std::map < std::string, std::set < std::string > > > fTable;
};

KnownIssues::KnownIssues()
  : fTable()
{
}

KnownIssues::~KnownIssues()
{
}

/**
 * Map cldr:1234 to CLDR-1234
 * Map 1234 to ICU-1234
 */
static std::string mapTicketId(const char *ticketStr) {
  std::string ticket(ticketStr);
  // TODO: Can remove this function once all logKnownIssue calls are switched over
  // to the ICU-1234 and CLDR-1234 format.
  if(ticket.rfind(OLD_CLDR_PREFIX) == 0) {
    // map cldrbug:1234 to CLDR-1234
    ticket.replace(0, uprv_strlen(OLD_CLDR_PREFIX), CLDR_BUG_PREFIX);
  } else if(::isdigit(ticket[0])) {
    // map 1234 to ICU-1234
    ticket.insert(0, ICU_BUG_PREFIX);
  }
  return ticket;
}

void KnownIssues::add(const char *ticketStr, const char *where, const char16_t *msg, UBool *firstForTicket, UBool *firstForWhere)
{
  const std::string ticket = mapTicketId(ticketStr);
  if(fTable.find(ticket) == fTable.end()) {
    if(firstForTicket!=nullptr) *firstForTicket = true;
    fTable[ticket] = std::map < std::string, std::set < std::string > >();
  } else {
    if(firstForTicket!=nullptr) *firstForTicket = false;
  }
  if(where==nullptr) return;

  if(fTable[ticket].find(where) == fTable[ticket].end()) {
    if(firstForWhere!=nullptr) *firstForWhere = true;
    fTable[ticket][where] = std::set < std::string >();
  } else {
    if(firstForWhere!=nullptr) *firstForWhere = false;
  }
  if(msg==nullptr || !*msg) return;

  const icu::UnicodeString ustr(msg);

  fTable[ticket][where].insert(std::string(icu::CStr(ustr)()));
}

void KnownIssues::add(const char *ticketStr, const char *where, const char *msg, UBool *firstForTicket, UBool *firstForWhere)
{
  const std::string ticket = mapTicketId(ticketStr);
  if(fTable.find(ticket) == fTable.end()) {
    if(firstForTicket!=nullptr) *firstForTicket = true;
    fTable[ticket] = std::map < std::string, std::set < std::string > >();
  } else {
    if(firstForTicket!=nullptr) *firstForTicket = false;
  }
  if(where==nullptr) return;

  if(fTable[ticket].find(where) == fTable[ticket].end()) {
    if(firstForWhere!=nullptr) *firstForWhere = true;
    fTable[ticket][where] = std::set < std::string >();
  } else {
    if(firstForWhere!=nullptr) *firstForWhere = false;
  }
  if(msg==nullptr || !*msg) return;

  std::string str(msg);
  fTable[ticket][where].insert(str);
}

UBool KnownIssues::print()
{
  if(fTable.empty()) {
    return false;
  }

  std::cout << "KNOWN ISSUES" << std::endl;
  for( std::map<  std::string,
          std::map <  std::string,  std::set <  std::string > > >::iterator i = fTable.begin();
       i != fTable.end();
       i++ ) {
    const std::string ticketid = (*i).first;
    std::cout << "[" << ticketid << "] ";
    if(ticketid.rfind(ICU_BUG_PREFIX) == 0 || ticketid.rfind(CLDR_BUG_PREFIX) == 0) {
      // If it's a unicode.org bug.
      std::cout << UNICODE_BUG_URL << ticketid;
    } // Else: some other kind of bug. Allow this, but without a URL.
    std::cout << std::endl;

    for( std::map< std::string, std::set < std::string > >::iterator ii = (*i).second.begin();
         ii != (*i).second.end();
         ii++ ) {
      std::cout << "  " << (*ii).first << std::endl;
      for ( std::set < std::string >::iterator iii = (*ii).second.begin();
            iii != (*ii).second.end();
            iii++ ) {
        std::cout << "     " << '"' << (*iii) << '"' << std::endl;
      }
    }
  }
  return true;
}

U_CAPI void *udbg_knownIssue_openU(void *ptr, const char *ticket, char *where, const char16_t *msg, UBool *firstForTicket,
                                   UBool *firstForWhere) {
  KnownIssues *t = static_cast<KnownIssues*>(ptr);
  if(t==nullptr) {
    t = new KnownIssues();
  }

  t->add(ticket, where, msg, firstForTicket, firstForWhere);

  return static_cast<void*>(t);
}

U_CAPI void *udbg_knownIssue_open(void *ptr, const char *ticket, char *where, const char *msg, UBool *firstForTicket,
                                   UBool *firstForWhere) {
  KnownIssues *t = static_cast<KnownIssues*>(ptr);
  if(t==nullptr) {
    t = new KnownIssues();
  }

  t->add(ticket, where, msg, firstForTicket, firstForWhere);

  return static_cast<void*>(t);
}

U_CAPI UBool udbg_knownIssue_print(void *ptr) {
  KnownIssues *t = static_cast<KnownIssues*>(ptr);
  if(t==nullptr) {
    return false;
  } else {
    t->print();
    return true;
  }
}

U_CAPI void udbg_knownIssue_close(void *ptr) {
  KnownIssues *t = static_cast<KnownIssues*>(ptr);
  delete t;
}
