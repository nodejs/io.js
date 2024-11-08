// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
*******************************************************************************
* Copyright (C) 2008-2013, International Business Machines Corporation and
* others. All Rights Reserved.
*******************************************************************************
*
*
* File GENDER.CPP
*
* Modification History:*
*   Date        Name        Description
*
********************************************************************************
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_FORMATTING

#include <utility>

#include "unicode/gender.h"
#include "unicode/ugender.h"
#include "unicode/ures.h"

#include "charstr.h"
#include "cmemory.h"
#include "cstring.h"
#include "mutex.h"
#include "uassert.h"
#include "ucln_in.h"
#include "ulocimp.h"
#include "umutex.h"
#include "uhash.h"

static UHashtable* gGenderInfoCache = nullptr;

static const char* gNeutralStr = "neutral";
static const char* gMailTaintsStr = "maleTaints";
static const char* gMixedNeutralStr = "mixedNeutral";
static icu::GenderInfo* gObjs = nullptr;
static icu::UInitOnce gGenderInitOnce {};

enum GenderStyle {
  NEUTRAL,
  MIXED_NEUTRAL,
  MALE_TAINTS,
  GENDER_STYLE_LENGTH
};

U_CDECL_BEGIN

static UBool U_CALLCONV gender_cleanup() {
  if (gGenderInfoCache != nullptr) {
    uhash_close(gGenderInfoCache);
    gGenderInfoCache = nullptr;
    delete [] gObjs;
  }
  gGenderInitOnce.reset();
  return true;
}

U_CDECL_END

U_NAMESPACE_BEGIN

void U_CALLCONV GenderInfo_initCache(UErrorCode &status) {
  ucln_i18n_registerCleanup(UCLN_I18N_GENDERINFO, gender_cleanup);
  U_ASSERT(gGenderInfoCache == nullptr);
  if (U_FAILURE(status)) {
      return;
  }
  gObjs = new GenderInfo[GENDER_STYLE_LENGTH];
  if (gObjs == nullptr) {
    status = U_MEMORY_ALLOCATION_ERROR;
    return;
  }
  for (int i = 0; i < GENDER_STYLE_LENGTH; i++) {
    gObjs[i]._style = i;
  }
  gGenderInfoCache = uhash_open(uhash_hashChars, uhash_compareChars, nullptr, &status);
  if (U_FAILURE(status)) {
    delete [] gObjs;
    return;
  }
  uhash_setKeyDeleter(gGenderInfoCache, uprv_free);
}


GenderInfo::GenderInfo() {
}

GenderInfo::~GenderInfo() {
}

const GenderInfo* GenderInfo::getInstance(const Locale& locale, UErrorCode& status) {
  // Make sure our cache exists.
  umtx_initOnce(gGenderInitOnce, &GenderInfo_initCache, status);
  if (U_FAILURE(status)) {
    return nullptr;
  }

  static UMutex gGenderMetaLock;
  const GenderInfo* result = nullptr;
  const char* key = locale.getName();
  {
    Mutex lock(&gGenderMetaLock);
    result = static_cast<const GenderInfo*>(uhash_get(gGenderInfoCache, key));
  }
  if (result) {
    return result;
  }

  // On cache miss, try to create GenderInfo from CLDR data
  result = loadInstance(locale, status);
  if (U_FAILURE(status)) {
    return nullptr;
  }

  // Try to put our GenderInfo object in cache. If there is a race condition,
  // favor the GenderInfo object that is already in the cache.
  {
    Mutex lock(&gGenderMetaLock);
    GenderInfo* temp = static_cast<GenderInfo*>(uhash_get(gGenderInfoCache, key));
    if (temp) {
      result = temp;
    } else {
      uhash_put(gGenderInfoCache, uprv_strdup(key), (void*) result, &status);
      if (U_FAILURE(status)) {
        return nullptr;
      }
    }
  }
  return result;
}

const GenderInfo* GenderInfo::loadInstance(const Locale& locale, UErrorCode& status) {
  LocalUResourceBundlePointer rb(
      ures_openDirect(nullptr, "genderList", &status));
  if (U_FAILURE(status)) {
    return nullptr;
  }
  LocalUResourceBundlePointer locRes(ures_getByKey(rb.getAlias(), "genderList", nullptr, &status));
  if (U_FAILURE(status)) {
    return nullptr;
  }
  int32_t resLen = 0;
  const char* curLocaleName = locale.getName();
  UErrorCode key_status = U_ZERO_ERROR;
  const char16_t* s = ures_getStringByKey(locRes.getAlias(), curLocaleName, &resLen, &key_status);
  if (s == nullptr) {
    key_status = U_ZERO_ERROR;
    CharString parentLocaleName(curLocaleName, key_status);
    while (s == nullptr) {
      {
          CharString tmp = ulocimp_getParent(parentLocaleName.data(), status);
          if (tmp.isEmpty()) break;
          parentLocaleName = std::move(tmp);
      }
      key_status = U_ZERO_ERROR;
      resLen = 0;
      s = ures_getStringByKey(locRes.getAlias(), parentLocaleName.data(), &resLen, &key_status);
      key_status = U_ZERO_ERROR;
    }
  }
  if (s == nullptr) {
    return &gObjs[NEUTRAL];
  }
  char type_str[256] = "";
  u_UCharsToChars(s, type_str, resLen + 1);
  if (uprv_strcmp(type_str, gNeutralStr) == 0) {
    return &gObjs[NEUTRAL];
  }
  if (uprv_strcmp(type_str, gMixedNeutralStr) == 0) {
    return &gObjs[MIXED_NEUTRAL]; 
  }
  if (uprv_strcmp(type_str, gMailTaintsStr) == 0) {
    return &gObjs[MALE_TAINTS];
  }
  return &gObjs[NEUTRAL];
}

UGender GenderInfo::getListGender(const UGender* genders, int32_t length, UErrorCode& status) const {
  if (U_FAILURE(status)) {
    return UGENDER_OTHER;
  }
  if (length == 0) {
    return UGENDER_OTHER;
  }
  if (length == 1) {
    return genders[0];
  }
  UBool has_female = false;
  UBool has_male = false;
  switch (_style) {
    case NEUTRAL:
      return UGENDER_OTHER;
    case MIXED_NEUTRAL:
      for (int32_t i = 0; i < length; ++i) {
        switch (genders[i]) {
          case UGENDER_OTHER:
            return UGENDER_OTHER;
            break;
          case UGENDER_FEMALE:
            if (has_male) {
              return UGENDER_OTHER;
            }
            has_female = true;
            break;
          case UGENDER_MALE:
            if (has_female) {
              return UGENDER_OTHER;
            }
            has_male = true;
            break;
          default:
            break;
        }
      }
      return has_male ? UGENDER_MALE : UGENDER_FEMALE;
      break;
    case MALE_TAINTS:
      for (int32_t i = 0; i < length; ++i) {
        if (genders[i] != UGENDER_FEMALE) {
          return UGENDER_MALE;
        }
      }
      return UGENDER_FEMALE;
      break;
    default:
      return UGENDER_OTHER;
      break;
  }
}

const GenderInfo* GenderInfo::getNeutralInstance() {
  return &gObjs[NEUTRAL];
}

const GenderInfo* GenderInfo::getMixedNeutralInstance() {
  return &gObjs[MIXED_NEUTRAL];
}

const GenderInfo* GenderInfo::getMaleTaintsInstance() {
  return &gObjs[MALE_TAINTS];
}

U_NAMESPACE_END

U_CAPI const UGenderInfo* U_EXPORT2
ugender_getInstance(const char* locale, UErrorCode* status) {
  return (const UGenderInfo*) icu::GenderInfo::getInstance(locale, *status);
}

U_CAPI UGender U_EXPORT2
ugender_getListGender(const UGenderInfo* genderInfo, const UGender* genders, int32_t size, UErrorCode* status) {
  return ((const icu::GenderInfo *)genderInfo)->getListGender(genders, size, *status);
}

#endif /* #if !UCONFIG_NO_FORMATTING */
