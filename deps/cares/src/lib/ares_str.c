/* MIT License
 *
 * Copyright (c) 1998 Massachusetts Institute of Technology
 * Copyright (c) The c-ares project and its contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ares_private.h"
#include "ares_str.h"

#ifdef HAVE_STDINT_H
#  include <stdint.h>
#endif

size_t ares_strlen(const char *str)
{
  if (str == NULL) {
    return 0;
  }

  return strlen(str);
}

char *ares_strdup(const char *s1)
{
  size_t len;
  char  *out;

  if (s1 == NULL) {
    return NULL;
  }

  len = ares_strlen(s1);

  /* Don't see how this is possible */
  if (len == SIZE_MAX) {
    return NULL; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  out = ares_malloc(len + 1);
  if (out == NULL) {
    return NULL;
  }

  if (len) {
    memcpy(out, s1, len);
  }

  out[len] = 0;
  return out;
}

size_t ares_strcpy(char *dest, const char *src, size_t dest_size)
{
  size_t len = 0;

  if (dest == NULL || dest_size == 0) {
    return 0; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  len = ares_strlen(src);

  if (len >= dest_size) {
    len = dest_size - 1;
  }

  if (len) {
    memcpy(dest, src, len);
  }

  dest[len] = 0;
  return len;
}

ares_bool_t ares_str_isnum(const char *str)
{
  size_t i;

  if (str == NULL || *str == 0) {
    return ARES_FALSE;
  }

  for (i = 0; str[i] != 0; i++) {
    if (str[i] < '0' || str[i] > '9') {
      return ARES_FALSE;
    }
  }
  return ARES_TRUE;
}

void ares__str_rtrim(char *str)
{
  size_t len;
  size_t i;

  if (str == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  len = ares_strlen(str);
  for (i = len; i > 0; i--) {
    if (!ares__isspace(str[i - 1])) {
      break;
    }
  }
  str[i] = 0;
}

void ares__str_ltrim(char *str)
{
  size_t i;
  size_t len;

  if (str == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  for (i = 0; str[i] != 0 && ares__isspace(str[i]); i++) {
    /* Do nothing */
  }

  if (i == 0) {
    return;
  }

  len = ares_strlen(str);
  if (i != len) {
    memmove(str, str + i, len - i);
  }
  str[len - i] = 0;
}

void ares__str_trim(char *str)
{
  ares__str_ltrim(str);
  ares__str_rtrim(str);
}

/* tolower() is locale-specific.  Use a lookup table fast conversion that only
 * operates on ASCII */
static const unsigned char ares__tolower_lookup[] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C,
  0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
  0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26,
  0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33,
  0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40,
  0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D,
  0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A,
  0x5B, 0x5C, 0x5D, 0x5E, 0x5F, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
  0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0x73, 0x74,
  0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F, 0x80, 0x81,
  0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E,
  0x8F, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B,
  0x9C, 0x9D, 0x9E, 0x9F, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8,
  0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF, 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5,
  0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF, 0xC0, 0xC1, 0xC2,
  0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
  0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC,
  0xDD, 0xDE, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9,
  0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6,
  0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF
};

unsigned char ares__tolower(unsigned char c)
{
  return ares__tolower_lookup[c];
}

ares_bool_t ares__memeq_ci(const unsigned char *ptr, const unsigned char *val,
                           size_t len)
{
  size_t i;
  for (i = 0; i < len; i++) {
    if (ares__tolower_lookup[ptr[i]] != ares__tolower_lookup[val[i]]) {
      return ARES_FALSE;
    }
  }
  return ARES_TRUE;
}

ares_bool_t ares__isspace(int ch)
{
  switch (ch) {
    case '\r':
    case '\t':
    case ' ':
    case '\v':
    case '\f':
    case '\n':
      return ARES_TRUE;
    default:
      break;
  }
  return ARES_FALSE;
}

ares_bool_t ares__isprint(int ch)
{
  if (ch >= 0x20 && ch <= 0x7E) {
    return ARES_TRUE;
  }
  return ARES_FALSE;
}

/* Character set allowed by hostnames.  This is to include the normal
 * domain name character set plus:
 *  - underscores which are used in SRV records.
 *  - Forward slashes such as are used for classless in-addr.arpa
 *    delegation (CNAMEs)
 *  - Asterisks may be used for wildcard domains in CNAMEs as seen in the
 *    real world.
 * While RFC 2181 section 11 does state not to do validation,
 * that applies to servers, not clients.  Vulnerabilities have been
 * reported when this validation is not performed.  Security is more
 * important than edge-case compatibility (which is probably invalid
 * anyhow). */
ares_bool_t ares__is_hostnamech(int ch)
{
  /* [A-Za-z0-9-*._/]
   * Don't use isalnum() as it is locale-specific
   */
  if (ch >= 'A' && ch <= 'Z') {
    return ARES_TRUE;
  }
  if (ch >= 'a' && ch <= 'z') {
    return ARES_TRUE;
  }
  if (ch >= '0' && ch <= '9') {
    return ARES_TRUE;
  }
  if (ch == '-' || ch == '.' || ch == '_' || ch == '/' || ch == '*') {
    return ARES_TRUE;
  }

  return ARES_FALSE;
}

ares_bool_t ares__is_hostname(const char *str)
{
  size_t i;

  if (str == NULL) {
    return ARES_FALSE; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  for (i = 0; str[i] != 0; i++) {
    if (!ares__is_hostnamech(str[i])) {
      return ARES_FALSE;
    }
  }
  return ARES_TRUE;
}

ares_bool_t ares__str_isprint(const char *str, size_t len)
{
  size_t i;

  if (str == NULL && len != 0) {
    return ARES_FALSE;
  }

  for (i = 0; i < len; i++) {
    if (!ares__isprint(str[i])) {
      return ARES_FALSE;
    }
  }
  return ARES_TRUE;
}
