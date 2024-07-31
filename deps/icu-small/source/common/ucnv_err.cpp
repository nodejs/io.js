// © 2016 and later: Unicode, Inc. and others.
// License & terms of use: http://www.unicode.org/copyright.html
/*
 *****************************************************************************
 *
 *   Copyright (C) 1998-2016, International Business Machines
 *   Corporation and others.  All Rights Reserved.
 *
 *****************************************************************************
 *
 *  ucnv_err.c
 *  Implements error behaviour functions called by T_UConverter_{from,to}Unicode
 *
 *
*   Change history:
*
*   06/29/2000  helena      Major rewrite of the callback APIs.
*/

#include "unicode/utypes.h"

#if !UCONFIG_NO_CONVERSION

#include "unicode/ucnv_err.h"
#include "unicode/ucnv_cb.h"
#include "ucnv_cnv.h"
#include "cmemory.h"
#include "unicode/ucnv.h"
#include "ustrfmt.h"

#define VALUE_STRING_LENGTH 48
/*Magic # 32 = 4(number of char in value string) * 8(max number of bytes per char for any converter) */
#define UNICODE_PERCENT_SIGN_CODEPOINT  0x0025
#define UNICODE_U_CODEPOINT             0x0055
#define UNICODE_X_CODEPOINT             0x0058
#define UNICODE_RS_CODEPOINT            0x005C
#define UNICODE_U_LOW_CODEPOINT         0x0075
#define UNICODE_X_LOW_CODEPOINT         0x0078
#define UNICODE_AMP_CODEPOINT           0x0026
#define UNICODE_HASH_CODEPOINT          0x0023
#define UNICODE_SEMICOLON_CODEPOINT     0x003B
#define UNICODE_PLUS_CODEPOINT          0x002B
#define UNICODE_LEFT_CURLY_CODEPOINT    0x007B
#define UNICODE_RIGHT_CURLY_CODEPOINT   0x007D
#define UNICODE_SPACE_CODEPOINT         0x0020
#define UCNV_PRV_ESCAPE_ICU         0
#define UCNV_PRV_ESCAPE_C           'C'
#define UCNV_PRV_ESCAPE_XML_DEC     'D'
#define UCNV_PRV_ESCAPE_XML_HEX     'X'
#define UCNV_PRV_ESCAPE_JAVA        'J'
#define UCNV_PRV_ESCAPE_UNICODE     'U'
#define UCNV_PRV_ESCAPE_CSS2        'S'
#define UCNV_PRV_STOP_ON_ILLEGAL    'i'

/*
 * IS_DEFAULT_IGNORABLE_CODE_POINT
 * This is to check if a code point has the default ignorable unicode property.
 * As such, this list needs to be updated if the ignorable code point list ever
 * changes.
 * To avoid dependency on other code, this list is hard coded here.
 * When an ignorable code point is found and is unmappable, the default callbacks
 * will ignore them.
 * For a list of the default ignorable code points, use this link:
 * https://util.unicode.org/UnicodeJsps/list-unicodeset.jsp?a=%5B%3ADI%3A%5D&abb=on&g=&i=
 *
 * This list should be sync with the one in CharsetCallback.java
 */
#define IS_DEFAULT_IGNORABLE_CODE_POINT(c) ( \
    (c == 0x00AD) || \
    (c == 0x034F) || \
    (c == 0x061C) || \
    (c == 0x115F) || \
    (c == 0x1160) || \
    (0x17B4 <= c && c <= 0x17B5) || \
    (0x180B <= c && c <= 0x180F) || \
    (0x200B <= c && c <= 0x200F) || \
    (0x202A <= c && c <= 0x202E) || \
    (0x2060 <= c && c <= 0x206F) || \
    (c == 0x3164) || \
    (0xFE00 <= c && c <= 0xFE0F) || \
    (c == 0xFEFF) || \
    (c == 0xFFA0) || \
    (0xFFF0 <= c && c <= 0xFFF8) || \
    (0x1BCA0 <= c && c <= 0x1BCA3) || \
    (0x1D173 <= c && c <= 0x1D17A) || \
    (0xE0000 <= c && c <= 0xE0FFF))


/*Function Pointer STOPS at the ILLEGAL_SEQUENCE */
U_CAPI void    U_EXPORT2
UCNV_FROM_U_CALLBACK_STOP (
                  const void *context,
                  UConverterFromUnicodeArgs *fromUArgs,
                  const char16_t* codeUnits,
                  int32_t length,
                  UChar32 codePoint,
                  UConverterCallbackReason reason,
                  UErrorCode * err)
{
    (void)context;
    (void)fromUArgs;
    (void)codeUnits;
    (void)length;
    if (reason == UCNV_UNASSIGNED && IS_DEFAULT_IGNORABLE_CODE_POINT(codePoint))
    {
        /*
         * Skip if the codepoint has unicode property of default ignorable.
         */
        *err = U_ZERO_ERROR;
    }
    /* the caller must have set the error code accordingly */
}


/*Function Pointer STOPS at the ILLEGAL_SEQUENCE */
U_CAPI void    U_EXPORT2
UCNV_TO_U_CALLBACK_STOP (
                   const void *context,
                   UConverterToUnicodeArgs *toUArgs,
                   const char* codePoints,
                   int32_t length,
                   UConverterCallbackReason reason,
                   UErrorCode * err)
{
    /* the caller must have set the error code accordingly */
    (void)context; (void)toUArgs; (void)codePoints; (void)length; (void)reason; (void)err;
}

U_CAPI void    U_EXPORT2
UCNV_FROM_U_CALLBACK_SKIP (                  
                  const void *context,
                  UConverterFromUnicodeArgs *fromUArgs,
                  const char16_t* codeUnits,
                  int32_t length,
                  UChar32 codePoint,
                  UConverterCallbackReason reason,
                  UErrorCode * err)
{
    (void)fromUArgs;
    (void)codeUnits;
    (void)length;
    if (reason <= UCNV_IRREGULAR)
    {
        if (reason == UCNV_UNASSIGNED && IS_DEFAULT_IGNORABLE_CODE_POINT(codePoint))
        {
            /*
             * Skip if the codepoint has unicode property of default ignorable.
             */
            *err = U_ZERO_ERROR;
        }
        else if (context == nullptr || (*((char*)context) == UCNV_PRV_STOP_ON_ILLEGAL && reason == UCNV_UNASSIGNED))
        {
            *err = U_ZERO_ERROR;
        }
        /* else the caller must have set the error code accordingly. */
    }
    /* else ignore the reset, close and clone calls. */
}

U_CAPI void    U_EXPORT2
UCNV_FROM_U_CALLBACK_SUBSTITUTE (
                  const void *context,
                  UConverterFromUnicodeArgs *fromArgs,
                  const char16_t* codeUnits,
                  int32_t length,
                  UChar32 codePoint,
                  UConverterCallbackReason reason,
                  UErrorCode * err)
{
    (void)codeUnits;
    (void)length;
    if (reason <= UCNV_IRREGULAR)
    {
        if (reason == UCNV_UNASSIGNED && IS_DEFAULT_IGNORABLE_CODE_POINT(codePoint))
        {
            /*
             * Skip if the codepoint has unicode property of default ignorable.
             */
            *err = U_ZERO_ERROR;
        }
        else if (context == nullptr || (*((char*)context) == UCNV_PRV_STOP_ON_ILLEGAL && reason == UCNV_UNASSIGNED))
        {
            *err = U_ZERO_ERROR;
            ucnv_cbFromUWriteSub(fromArgs, 0, err);
        }
        /* else the caller must have set the error code accordingly. */
    }
    /* else ignore the reset, close and clone calls. */
}

/*uses uprv_itou to get a unicode escape sequence of the offensive sequence,
 *uses a clean copy (resetted) of the converter, to convert that unicode
 *escape sequence to the target codepage (if conversion failure happens then
 *we revert to substituting with subchar)
 */
U_CAPI void    U_EXPORT2
UCNV_FROM_U_CALLBACK_ESCAPE (
                         const void *context,
                         UConverterFromUnicodeArgs *fromArgs,
                         const char16_t *codeUnits,
                         int32_t length,
                         UChar32 codePoint,
                         UConverterCallbackReason reason,
                         UErrorCode * err)
{

  char16_t valueString[VALUE_STRING_LENGTH];
  int32_t valueStringLength = 0;
  int32_t i = 0;

  const char16_t *myValueSource = nullptr;
  UErrorCode err2 = U_ZERO_ERROR;
  UConverterFromUCallback original = nullptr;
  const void *originalContext;

  UConverterFromUCallback ignoredCallback = nullptr;
  const void *ignoredContext;
  
  if (reason > UCNV_IRREGULAR)
  {
      return;
  }
  else if (reason == UCNV_UNASSIGNED && IS_DEFAULT_IGNORABLE_CODE_POINT(codePoint))
  {
      /*
       * Skip if the codepoint has unicode property of default ignorable.
       */
      *err = U_ZERO_ERROR;
      return;
  }

  ucnv_setFromUCallBack (fromArgs->converter,
                     (UConverterFromUCallback) UCNV_FROM_U_CALLBACK_SUBSTITUTE,
                     nullptr,
                     &original,
                     &originalContext,
                     &err2);
  
  if (U_FAILURE (err2))
  {
    *err = err2;
    return;
  } 
  if(context==nullptr)
  { 
      while (i < length)
      {
        valueString[valueStringLength++] = (char16_t) UNICODE_PERCENT_SIGN_CODEPOINT;  /* adding % */
        valueString[valueStringLength++] = (char16_t) UNICODE_U_CODEPOINT; /* adding U */
        valueStringLength += uprv_itou (valueString + valueStringLength, VALUE_STRING_LENGTH - valueStringLength, (uint16_t)codeUnits[i++], 16, 4);
      }
  }
  else
  {
      switch(*((char*)context))
      {
      case UCNV_PRV_ESCAPE_JAVA:
          while (i < length)
          {
              valueString[valueStringLength++] = (char16_t) UNICODE_RS_CODEPOINT;    /* adding \ */
              valueString[valueStringLength++] = (char16_t) UNICODE_U_LOW_CODEPOINT; /* adding u */
              valueStringLength += uprv_itou (valueString + valueStringLength, VALUE_STRING_LENGTH - valueStringLength, (uint16_t)codeUnits[i++], 16, 4);
          }
          break;

      case UCNV_PRV_ESCAPE_C:
          valueString[valueStringLength++] = (char16_t) UNICODE_RS_CODEPOINT;    /* adding \ */

          if(length==2){
              valueString[valueStringLength++] = (char16_t) UNICODE_U_CODEPOINT; /* adding U */
              valueStringLength += uprv_itou (valueString + valueStringLength, VALUE_STRING_LENGTH - valueStringLength, codePoint, 16, 8);

          }
          else{
              valueString[valueStringLength++] = (char16_t) UNICODE_U_LOW_CODEPOINT; /* adding u */
              valueStringLength += uprv_itou (valueString + valueStringLength, VALUE_STRING_LENGTH - valueStringLength, (uint16_t)codeUnits[0], 16, 4);
          }
          break;

      case UCNV_PRV_ESCAPE_XML_DEC:

          valueString[valueStringLength++] = (char16_t) UNICODE_AMP_CODEPOINT;   /* adding & */
          valueString[valueStringLength++] = (char16_t) UNICODE_HASH_CODEPOINT;  /* adding # */
          if(length==2){
              valueStringLength += uprv_itou (valueString + valueStringLength, VALUE_STRING_LENGTH - valueStringLength, codePoint, 10, 0);
          }
          else{
              valueStringLength += uprv_itou (valueString + valueStringLength, VALUE_STRING_LENGTH - valueStringLength, (uint16_t)codeUnits[0], 10, 0);
          }
          valueString[valueStringLength++] = (char16_t) UNICODE_SEMICOLON_CODEPOINT; /* adding ; */
          break;

      case UCNV_PRV_ESCAPE_XML_HEX:

          valueString[valueStringLength++] = (char16_t) UNICODE_AMP_CODEPOINT;   /* adding & */
          valueString[valueStringLength++] = (char16_t) UNICODE_HASH_CODEPOINT;  /* adding # */
          valueString[valueStringLength++] = (char16_t) UNICODE_X_LOW_CODEPOINT; /* adding x */
          if(length==2){
              valueStringLength += uprv_itou (valueString + valueStringLength, VALUE_STRING_LENGTH - valueStringLength, codePoint, 16, 0);
          }
          else{
              valueStringLength += uprv_itou (valueString + valueStringLength, VALUE_STRING_LENGTH - valueStringLength, (uint16_t)codeUnits[0], 16, 0);
          }
          valueString[valueStringLength++] = (char16_t) UNICODE_SEMICOLON_CODEPOINT; /* adding ; */
          break;

      case UCNV_PRV_ESCAPE_UNICODE:
          valueString[valueStringLength++] = (char16_t) UNICODE_LEFT_CURLY_CODEPOINT;    /* adding { */
          valueString[valueStringLength++] = (char16_t) UNICODE_U_CODEPOINT;    /* adding U */
          valueString[valueStringLength++] = (char16_t) UNICODE_PLUS_CODEPOINT; /* adding + */
          if (length == 2) {
              valueStringLength += uprv_itou (valueString + valueStringLength, VALUE_STRING_LENGTH - valueStringLength, codePoint, 16, 4);
          } else {
              valueStringLength += uprv_itou (valueString + valueStringLength, VALUE_STRING_LENGTH - valueStringLength, (uint16_t)codeUnits[0], 16, 4);
          }
          valueString[valueStringLength++] = (char16_t) UNICODE_RIGHT_CURLY_CODEPOINT;    /* adding } */
          break;

      case UCNV_PRV_ESCAPE_CSS2:
          valueString[valueStringLength++] = (char16_t) UNICODE_RS_CODEPOINT;    /* adding \ */
          valueStringLength += uprv_itou (valueString + valueStringLength, VALUE_STRING_LENGTH - valueStringLength, codePoint, 16, 0);
          /* Always add space character, because the next character might be whitespace,
             which would erroneously be considered the termination of the escape sequence. */
          valueString[valueStringLength++] = (char16_t) UNICODE_SPACE_CODEPOINT;
          break;

      default:
          while (i < length)
          {
              valueString[valueStringLength++] = (char16_t) UNICODE_PERCENT_SIGN_CODEPOINT;  /* adding % */
              valueString[valueStringLength++] = (char16_t) UNICODE_U_CODEPOINT;             /* adding U */
              valueStringLength += uprv_itou (valueString + valueStringLength, VALUE_STRING_LENGTH - valueStringLength, (uint16_t)codeUnits[i++], 16, 4);
          }
      }
  }  
  myValueSource = valueString;

  /* reset the error */
  *err = U_ZERO_ERROR;

  ucnv_cbFromUWriteUChars(fromArgs, &myValueSource, myValueSource+valueStringLength, 0, err);

  ucnv_setFromUCallBack (fromArgs->converter,
                         original,
                         originalContext,
                         &ignoredCallback,
                         &ignoredContext,
                         &err2);
  if (U_FAILURE (err2))
  {
      *err = err2;
      return;
  }
}



U_CAPI void  U_EXPORT2
UCNV_TO_U_CALLBACK_SKIP (
                 const void *context,
                 UConverterToUnicodeArgs *toArgs,
                 const char* codeUnits,
                 int32_t length,
                 UConverterCallbackReason reason,
                 UErrorCode * err)
{
    (void)toArgs;
    (void)codeUnits;
    (void)length;
    if (reason <= UCNV_IRREGULAR)
    {
        if (context == nullptr || (*((char*)context) == UCNV_PRV_STOP_ON_ILLEGAL && reason == UCNV_UNASSIGNED))
        {
            *err = U_ZERO_ERROR;
        }
        /* else the caller must have set the error code accordingly. */
    }
    /* else ignore the reset, close and clone calls. */
}

U_CAPI void    U_EXPORT2
UCNV_TO_U_CALLBACK_SUBSTITUTE (
                 const void *context,
                 UConverterToUnicodeArgs *toArgs,
                 const char* codeUnits,
                 int32_t length,
                 UConverterCallbackReason reason,
                 UErrorCode * err)
{
    (void)codeUnits;
    (void)length;
    if (reason <= UCNV_IRREGULAR)
    {
        if (context == nullptr || (*((char*)context) == UCNV_PRV_STOP_ON_ILLEGAL && reason == UCNV_UNASSIGNED))
        {
            *err = U_ZERO_ERROR;
            ucnv_cbToUWriteSub(toArgs,0,err);
        }
        /* else the caller must have set the error code accordingly. */
    }
    /* else ignore the reset, close and clone calls. */
}

/*uses uprv_itou to get a unicode escape sequence of the offensive sequence,
 *and uses that as the substitution sequence
 */
U_CAPI void   U_EXPORT2
UCNV_TO_U_CALLBACK_ESCAPE (
                 const void *context,
                 UConverterToUnicodeArgs *toArgs,
                 const char* codeUnits,
                 int32_t length,
                 UConverterCallbackReason reason,
                 UErrorCode * err)
{
    char16_t uniValueString[VALUE_STRING_LENGTH];
    int32_t valueStringLength = 0;
    int32_t i = 0;

    if (reason > UCNV_IRREGULAR)
    {
        return;
    }

    if(context==nullptr)
    {    
        while (i < length)
        {
            uniValueString[valueStringLength++] = (char16_t) UNICODE_PERCENT_SIGN_CODEPOINT; /* adding % */
            uniValueString[valueStringLength++] = (char16_t) UNICODE_X_CODEPOINT;    /* adding X */
            valueStringLength += uprv_itou (uniValueString + valueStringLength, VALUE_STRING_LENGTH - valueStringLength, (uint8_t) codeUnits[i++], 16, 2);
        }
    }
    else
    {
        switch(*((char*)context))
        {
        case UCNV_PRV_ESCAPE_XML_DEC:
            while (i < length)
            {
                uniValueString[valueStringLength++] = (char16_t) UNICODE_AMP_CODEPOINT;   /* adding & */
                uniValueString[valueStringLength++] = (char16_t) UNICODE_HASH_CODEPOINT;  /* adding # */
                valueStringLength += uprv_itou (uniValueString + valueStringLength, VALUE_STRING_LENGTH - valueStringLength, (uint8_t)codeUnits[i++], 10, 0);
                uniValueString[valueStringLength++] = (char16_t) UNICODE_SEMICOLON_CODEPOINT; /* adding ; */
            }
            break;

        case UCNV_PRV_ESCAPE_XML_HEX:
            while (i < length)
            {
                uniValueString[valueStringLength++] = (char16_t) UNICODE_AMP_CODEPOINT;   /* adding & */
                uniValueString[valueStringLength++] = (char16_t) UNICODE_HASH_CODEPOINT;  /* adding # */
                uniValueString[valueStringLength++] = (char16_t) UNICODE_X_LOW_CODEPOINT; /* adding x */
                valueStringLength += uprv_itou (uniValueString + valueStringLength, VALUE_STRING_LENGTH - valueStringLength, (uint8_t)codeUnits[i++], 16, 0);
                uniValueString[valueStringLength++] = (char16_t) UNICODE_SEMICOLON_CODEPOINT; /* adding ; */
            }
            break;
        case UCNV_PRV_ESCAPE_C:
            while (i < length)
            {
                uniValueString[valueStringLength++] = (char16_t) UNICODE_RS_CODEPOINT;    /* adding \ */
                uniValueString[valueStringLength++] = (char16_t) UNICODE_X_LOW_CODEPOINT; /* adding x */
                valueStringLength += uprv_itou (uniValueString + valueStringLength, VALUE_STRING_LENGTH - valueStringLength, (uint8_t)codeUnits[i++], 16, 2);
            }
            break;
        default:
            while (i < length)
            {
                uniValueString[valueStringLength++] = (char16_t) UNICODE_PERCENT_SIGN_CODEPOINT; /* adding % */
                uniValueString[valueStringLength++] = (char16_t) UNICODE_X_CODEPOINT;    /* adding X */
                uprv_itou (uniValueString + valueStringLength, VALUE_STRING_LENGTH - valueStringLength, (uint8_t) codeUnits[i++], 16, 2);
                valueStringLength += 2;
            }
        }
    }
    /* reset the error */
    *err = U_ZERO_ERROR;

    ucnv_cbToUWriteUChars(toArgs, uniValueString, valueStringLength, 0, err);
}

#endif
