/*
 * sfparse
 *
 * Copyright (c) 2023 sfparse contributors
 * Copyright (c) 2019 nghttp3 contributors
 * Copyright (c) 2015 nghttp2 contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef SFPARSE_H
#define SFPARSE_H

/* Define WIN32 when build target is Win32 API (borrowed from
   libcurl) */
#if (defined(_WIN32) || defined(__WIN32__)) && !defined(WIN32)
#  define WIN32
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_MSC_VER) && (_MSC_VER < 1800)
/* MSVC < 2013 does not have inttypes.h because it is not C99
   compliant.  See compiler macros and version number in
   https://sourceforge.net/p/predef/wiki/Compilers/ */
#  include <stdint.h>
#else /* !defined(_MSC_VER) || (_MSC_VER >= 1800) */
#  include <inttypes.h>
#endif /* !defined(_MSC_VER) || (_MSC_VER >= 1800) */
#include <sys/types.h>
#include <stddef.h>

/**
 * @enum
 *
 * :type:`sf_type` defines value type.
 */
typedef enum sf_type {
  /**
   * :enum:`SF_TYPE_BOOLEAN` indicates boolean type.
   */
  SF_TYPE_BOOLEAN,
  /**
   * :enum:`SF_TYPE_INTEGER` indicates integer type.
   */
  SF_TYPE_INTEGER,
  /**
   * :enum:`SF_TYPE_DECIMAL` indicates decimal type.
   */
  SF_TYPE_DECIMAL,
  /**
   * :enum:`SF_TYPE_STRING` indicates string type.
   */
  SF_TYPE_STRING,
  /**
   * :enum:`SF_TYPE_TOKEN` indicates token type.
   */
  SF_TYPE_TOKEN,
  /**
   * :enum:`SF_TYPE_BYTESEQ` indicates byte sequence type.
   */
  SF_TYPE_BYTESEQ,
  /**
   * :enum:`SF_TYPE_INNER_LIST` indicates inner list type.
   */
  SF_TYPE_INNER_LIST,
  /**
   * :enum:`SF_TYPE_DATE` indicates date type.
   */
  SF_TYPE_DATE
} sf_type;

/**
 * @macro
 *
 * :macro:`SF_ERR_PARSE_ERROR` indicates fatal parse error has
 * occurred, and it is not possible to continue the processing.
 */
#define SF_ERR_PARSE_ERROR -1

/**
 * @macro
 *
 * :macro:`SF_ERR_EOF` indicates that there is nothing left to read.
 * The context of this error varies depending on the function that
 * returns this error code.
 */
#define SF_ERR_EOF -2

/**
 * @struct
 *
 * :type:`sf_vec` stores sequence of bytes.
 */
typedef struct sf_vec {
  /**
   * :member:`base` points to the beginning of the sequence of bytes.
   */
  uint8_t *base;
  /**
   * :member:`len` is the number of bytes contained in this sequence.
   */
  size_t len;
} sf_vec;

/**
 * @macro
 *
 * :macro:`SF_VALUE_FLAG_NONE` indicates no flag set.
 */
#define SF_VALUE_FLAG_NONE 0x0u

/**
 * @macro
 *
 * :macro:`SF_VALUE_FLAG_ESCAPED_STRING` indicates that a string
 * contains escaped character(s).
 */
#define SF_VALUE_FLAG_ESCAPED_STRING 0x1u

/**
 * @struct
 *
 * :type:`sf_decimal` contains decimal value.
 */
typedef struct sf_decimal {
  /**
   * :member:`numer` contains numerator of the decimal value.
   */
  int64_t numer;
  /**
   * :member:`denom` contains denominator of the decimal value.
   */
  int64_t denom;
} sf_decimal;

/**
 * @struct
 *
 * :type:`sf_value` stores a Structured Field item.  For Inner List,
 * only type is set to :enum:`sf_type.SF_TYPE_INNER_LIST`.  In order
 * to read the items contained in an inner list, call
 * `sf_parser_inner_list`.
 */
typedef struct sf_value {
  /**
   * :member:`type` is the type of the value contained in this
   * particular object.
   */
  sf_type type;
  /**
   * :member:`flags` is bitwise OR of one or more of
   * :macro:`SF_VALUE_FLAG_* <SF_VALUE_FLAG_NONE>`.
   */
  uint32_t flags;
  /**
   * @anonunion_start
   *
   * @sf_value_value
   */
  union {
    /**
     * :member:`boolean` contains boolean value if :member:`type` ==
     * :enum:`sf_type.SF_TYPE_BOOLEAN`.  1 indicates true, and 0
     * indicates false.
     */
    int boolean;
    /**
     * :member:`integer` contains integer value if :member:`type` is
     * either :enum:`sf_type.SF_TYPE_INTEGER` or
     * :enum:`sf_type.SF_TYPE_DATE`.
     */
    int64_t integer;
    /**
     * :member:`decimal` contains decimal value if :member:`type` ==
     * :enum:`sf_type.SF_TYPE_DECIMAL`.
     */
    sf_decimal decimal;
    /**
     * :member:`vec` contains sequence of bytes if :member:`type` is
     * either :enum:`sf_type.SF_TYPE_STRING`,
     * :enum:`sf_type.SF_TYPE_TOKEN`, or
     * :enum:`sf_type.SF_TYPE_BYTESEQ`.
     *
     * For :enum:`sf_type.SF_TYPE_STRING`, this field contains one or
     * more escaped characters if :member:`flags` has
     * :macro:`SF_VALUE_FLAG_ESCAPED_STRING` set.  To unescape the
     * string, use `sf_unescape`.
     *
     * For :enum:`sf_type.SF_TYPE_BYTESEQ`, this field contains base64
     * encoded string.  To decode this byte string, use
     * `sf_base64decode`.
     *
     * If :member:`vec.len <sf_vec.len>` == 0, :member:`vec.base
     * <sf_vec.base>` is guaranteed to be NULL.
     */
    sf_vec vec;
    /**
     * @anonunion_end
     */
  };
} sf_value;

/**
 * @struct
 *
 * :type:`sf_parser` is the Structured Field Values parser.  Use
 * `sf_parser_init` to initialize it.
 */
typedef struct sf_parser {
  /* all fields are private */
  const uint8_t *pos;
  const uint8_t *end;
  uint32_t state;
} sf_parser;

/**
 * @function
 *
 * `sf_parser_init` initializes |sfp| with the given buffer pointed by
 * |data| of length |datalen|.
 */
void sf_parser_init(sf_parser *sfp, const uint8_t *data, size_t datalen);

/**
 * @function
 *
 * `sf_parser_param` reads a parameter.  If this function returns 0,
 * it stores parameter key and value in |dest_key| and |dest_value|
 * respectively, if they are not NULL.
 *
 * This function does no effort to find duplicated keys.  Same key may
 * be reported more than once.
 *
 * Caller should keep calling this function until it returns negative
 * error code.  If it returns :macro:`SF_ERR_EOF`, all parameters have
 * read, and caller can continue to read rest of the values.  If it
 * returns :macro:`SF_ERR_PARSE_ERROR`, it encountered fatal error
 * while parsing field value.
 */
int sf_parser_param(sf_parser *sfp, sf_vec *dest_key, sf_value *dest_value);

/**
 * @function
 *
 * `sf_parser_dict` reads the next dictionary key and value pair.  If
 * this function returns 0, it stores the key and value in |dest_key|
 * and |dest_value| respectively, if they are not NULL.
 *
 * Caller can optionally read parameters attached to the pair by
 * calling `sf_parser_param`.
 *
 * This function does no effort to find duplicated keys.  Same key may
 * be reported more than once.
 *
 * Caller should keep calling this function until it returns negative
 * error code.  If it returns :macro:`SF_ERR_EOF`, all key and value
 * pairs have been read, and there is nothing left to read.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * :macro:`SF_ERR_EOF`
 *     All values in the dictionary have read.
 * :macro:`SF_ERR_PARSE_ERROR`
 *     It encountered fatal error while parsing field value.
 */
int sf_parser_dict(sf_parser *sfp, sf_vec *dest_key, sf_value *dest_value);

/**
 * @function
 *
 * `sf_parser_list` reads the next list item.  If this function
 * returns 0, it stores the item in |dest| if it is not NULL.
 *
 * Caller can optionally read parameters attached to the item by
 * calling `sf_parser_param`.
 *
 * Caller should keep calling this function until it returns negative
 * error code.  If it returns :macro:`SF_ERR_EOF`, all values in the
 * list have been read, and there is nothing left to read.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * :macro:`SF_ERR_EOF`
 *     All values in the list have read.
 * :macro:`SF_ERR_PARSE_ERROR`
 *     It encountered fatal error while parsing field value.
 */
int sf_parser_list(sf_parser *sfp, sf_value *dest);

/**
 * @function
 *
 * `sf_parser_item` reads a single item.  If this function returns 0,
 * it stores the item in |dest| if it is not NULL.
 *
 * This function is only used for the field value that consists of a
 * single item.
 *
 * Caller can optionally read parameters attached to the item by
 * calling `sf_parser_param`.
 *
 * Caller should call this function again to make sure that there is
 * nothing left to read.  If this 2nd function call returns
 * :macro:`SF_ERR_EOF`, all data have been processed successfully.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * :macro:`SF_ERR_EOF`
 *     There is nothing left to read.
 * :macro:`SF_ERR_PARSE_ERROR`
 *     It encountered fatal error while parsing field value.
 */
int sf_parser_item(sf_parser *sfp, sf_value *dest);

/**
 * @function
 *
 * `sf_parser_inner_list` reads the next inner list item.  If this
 * function returns 0, it stores the item in |dest| if it is not NULL.
 *
 * Caller can optionally read parameters attached to the item by
 * calling `sf_parser_param`.
 *
 * Caller should keep calling this function until it returns negative
 * error code.  If it returns :macro:`SF_ERR_EOF`, all values in this
 * inner list have been read, and caller can optionally read
 * parameters attached to this inner list by calling
 * `sf_parser_param`.  Then caller can continue to read rest of the
 * values.
 *
 * This function returns 0 if it succeeds, or one of the following
 * negative error codes:
 *
 * :macro:`SF_ERR_EOF`
 *     All values in the inner list have read.
 * :macro:`SF_ERR_PARSE_ERROR`
 *     It encountered fatal error while parsing field value.
 */
int sf_parser_inner_list(sf_parser *sfp, sf_value *dest);

/**
 * @function
 *
 * `sf_unescape` copies |src| to |dest| by removing escapes (``\``).
 * |src| should be the pointer to :member:`sf_value.vec` of type
 * :enum:`sf_type.SF_TYPE_STRING` produced by either `sf_parser_dict`,
 * `sf_parser_list`, `sf_parser_inner_list`, `sf_parser_item`, or
 * `sf_parser_param`, otherwise the behavior is undefined.
 *
 * :member:`dest->base <sf_vec.base>` must point to the buffer that
 * has sufficient space to store the unescaped string.
 *
 * If there is no escape character in |src|, |*src| is assigned to
 * |*dest|.  This includes the case that :member:`src->len
 * <sf_vec.len>` == 0.
 *
 * This function sets the length of unescaped string to
 * :member:`dest->len <sf_vec.len>`.
 */
void sf_unescape(sf_vec *dest, const sf_vec *src);

/**
 * @function
 *
 * `sf_base64decode` decodes Base64 encoded string |src| and writes
 * the result into |dest|.  |src| should be the pointer to
 * :member:`sf_value.vec` of type :enum:`sf_type.SF_TYPE_BYTESEQ`
 * produced by either `sf_parser_dict`, `sf_parser_list`,
 * `sf_parser_inner_list`, `sf_parser_item`, or `sf_parser_param`,
 * otherwise the behavior is undefined.
 *
 * :member:`dest->base <sf_vec.base>` must point to the buffer that
 * has sufficient space to store the decoded byte string.
 *
 * If :member:`src->len <sf_vec.len>` == 0, |*src| is assigned to
 * |*dest|.
 *
 * This function sets the length of decoded byte string to
 * :member:`dest->len <sf_vec.len>`.
 */
void sf_base64decode(sf_vec *dest, const sf_vec *src);

#ifdef __cplusplus
}
#endif

#endif /* SFPARSE_H */
