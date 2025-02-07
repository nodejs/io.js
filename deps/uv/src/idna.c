/* Copyright libuv contributors. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Derived from https://github.com/bnoordhuis/punycode
 * but updated to support IDNA 2008.
 */

#include "uv.h"
#include "uv-common.h"
#include "idna.h"
#include <assert.h>
#include <string.h>
#include <limits.h> /* UINT_MAX */


static int32_t uv__wtf8_decode1(const char** input) {
  uint32_t code_point;
  uint8_t b1;
  uint8_t b2;
  uint8_t b3;
  uint8_t b4;

  b1 = **input;
  if (b1 <= 0x7F)
    return b1; /* ASCII code point */
  if (b1 < 0xC2)
    return -1; /* invalid: continuation byte */
  code_point = b1;

  b2 = *++*input;
  if ((b2 & 0xC0) != 0x80)
    return -1; /* invalid: not a continuation byte */
  code_point = (code_point << 6) | (b2 & 0x3F);
  if (b1 <= 0xDF)
    return 0x7FF & code_point; /* two-byte character */

  b3 = *++*input;
  if ((b3 & 0xC0) != 0x80)
    return -1; /* invalid: not a continuation byte */
  code_point = (code_point << 6) | (b3 & 0x3F);
  if (b1 <= 0xEF)
    return 0xFFFF & code_point; /* three-byte character */

  b4 = *++*input;
  if ((b4 & 0xC0) != 0x80)
    return -1; /* invalid: not a continuation byte */
  code_point = (code_point << 6) | (b4 & 0x3F);
  if (b1 <= 0xF4) {
    code_point &= 0x1FFFFF;
    if (code_point <= 0x10FFFF)
      return code_point; /* four-byte character */
  }

  /* code point too large */
  return -1;
}


static unsigned uv__utf8_decode1_slow(const char** p,
                                      const char* pe,
                                      unsigned a) {
  unsigned b;
  unsigned c;
  unsigned d;
  unsigned min;

  if (a > 0xF7)
    return -1;

  switch (pe - *p) {
  default:
    if (a > 0xEF) {
      min = 0x10000;
      a = a & 7;
      b = (unsigned char) *(*p)++;
      c = (unsigned char) *(*p)++;
      d = (unsigned char) *(*p)++;
      break;
    }
    /* Fall through. */
  case 2:
    if (a > 0xDF) {
      min = 0x800;
      b = 0x80 | (a & 15);
      c = (unsigned char) *(*p)++;
      d = (unsigned char) *(*p)++;
      a = 0;
      break;
    }
    /* Fall through. */
  case 1:
    if (a > 0xBF) {
      min = 0x80;
      b = 0x80;
      c = 0x80 | (a & 31);
      d = (unsigned char) *(*p)++;
      a = 0;
      break;
    }
    /* Fall through. */
  case 0:
    return -1;  /* Invalid continuation byte. */
  }

  if (0x80 != (0xC0 & (b ^ c ^ d)))
    return -1;  /* Invalid sequence. */

  b &= 63;
  c &= 63;
  d &= 63;
  a = (a << 18) | (b << 12) | (c << 6) | d;

  if (a < min)
    return -1;  /* Overlong sequence. */

  if (a > 0x10FFFF)
    return -1;  /* Four-byte sequence > U+10FFFF. */

  if (a >= 0xD800 && a <= 0xDFFF)
    return -1;  /* Surrogate pair. */

  return a;
}


unsigned uv__utf8_decode1(const char** p, const char* pe) {
  unsigned a;

  assert(*p < pe);

  a = (unsigned char) *(*p)++;

  if (a < 128)
    return a;  /* ASCII, common case. */

  return uv__utf8_decode1_slow(p, pe, a);
}


static int uv__idna_toascii_label(const char* s, const char* se,
                                  char** d, char* de) {
  static const char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  const char* ss;
  unsigned c;
  unsigned h;
  unsigned k;
  unsigned n;
  unsigned m;
  unsigned q;
  unsigned t;
  unsigned x;
  unsigned y;
  unsigned bias;
  unsigned delta;
  unsigned todo;
  int first;

  h = 0;
  ss = s;
  todo = 0;

  /* Note: after this loop we've visited all UTF-8 characters and know
   * they're legal so we no longer need to check for decode errors.
   */
  while (s < se) {
    c = uv__utf8_decode1(&s, se);

    if (c == UINT_MAX)
      return UV_EINVAL;

    if (c < 128)
      h++;
    else
      todo++;
  }

  /* Only write "xn--" when there are non-ASCII characters. */
  if (todo > 0) {
    if (*d < de) *(*d)++ = 'x';
    if (*d < de) *(*d)++ = 'n';
    if (*d < de) *(*d)++ = '-';
    if (*d < de) *(*d)++ = '-';
  }

  /* Write ASCII characters. */
  x = 0;
  s = ss;
  while (s < se) {
    c = uv__utf8_decode1(&s, se);
    assert(c != UINT_MAX);

    if (c > 127)
      continue;

    if (*d < de)
      *(*d)++ = c;

    if (++x == h)
      break;  /* Visited all ASCII characters. */
  }

  if (todo == 0)
    return h;

  /* Only write separator when we've written ASCII characters first. */
  if (h > 0)
    if (*d < de)
      *(*d)++ = '-';

  n = 128;
  bias = 72;
  delta = 0;
  first = 1;

  while (todo > 0) {
    m = -1;
    s = ss;

    while (s < se) {
      c = uv__utf8_decode1(&s, se);
      assert(c != UINT_MAX);

      if (c >= n)
        if (c < m)
          m = c;
    }

    x = m - n;
    y = h + 1;

    if (x > ~delta / y)
      return UV_E2BIG;  /* Overflow. */

    delta += x * y;
    n = m;

    s = ss;
    while (s < se) {
      c = uv__utf8_decode1(&s, se);
      assert(c != UINT_MAX);

      if (c < n)
        if (++delta == 0)
          return UV_E2BIG;  /* Overflow. */

      if (c != n)
        continue;

      for (k = 36, q = delta; /* empty */; k += 36) {
        t = 1;

        if (k > bias)
          t = k - bias;

        if (t > 26)
          t = 26;

        if (q < t)
          break;

        /* TODO(bnoordhuis) Since 1 <= t <= 26 and therefore
         * 10 <= y <= 35, we can optimize the long division
         * into a table-based reciprocal multiplication.
         */
        x = q - t;
        y = 36 - t;  /* 10 <= y <= 35 since 1 <= t <= 26. */
        q = x / y;
        t = t + x % y;  /* 1 <= t <= 35 because of y. */

        if (*d < de)
          *(*d)++ = alphabet[t];
      }

      if (*d < de)
        *(*d)++ = alphabet[q];

      delta /= 2;

      if (first) {
        delta /= 350;
        first = 0;
      }

      /* No overflow check is needed because |delta| was just
       * divided by 2 and |delta+delta >= delta + delta/h|.
       */
      h++;
      delta += delta / h;

      for (bias = 0; delta > 35 * 26 / 2; bias += 36)
        delta /= 35;

      bias += 36 * delta / (delta + 38);
      delta = 0;
      todo--;
    }

    delta++;
    n++;
  }

  return 0;
}


ssize_t uv__idna_toascii(const char* s, const char* se, char* d, char* de) {
  const char* si;
  const char* st;
  unsigned c;
  char* ds;
  int rc;

  if (s == se)
    return UV_EINVAL;

  ds = d;

  si = s;
  while (si < se) {
    st = si;
    c = uv__utf8_decode1(&si, se);

    if (c == UINT_MAX)
      return UV_EINVAL;

    if (c != '.')
      if (c != 0x3002)  /* 。 */
        if (c != 0xFF0E)  /* ． */
          if (c != 0xFF61)  /* ｡ */
            continue;

    rc = uv__idna_toascii_label(s, st, &d, de);

    if (rc < 0)
      return rc;

    if (d < de)
      *d++ = '.';

    s = si;
  }

  if (s < se) {
    rc = uv__idna_toascii_label(s, se, &d, de);

    if (rc < 0)
      return rc;
  }

  if (d >= de)
    return UV_EINVAL;

  *d++ = '\0';
  return d - ds;  /* Number of bytes written. */
}


ssize_t uv_wtf8_length_as_utf16(const char* source_ptr) {
  size_t w_target_len = 0;
  int32_t code_point;

  do {
    code_point = uv__wtf8_decode1(&source_ptr);
    if (code_point < 0)
      return -1;
    if (code_point > 0xFFFF)
      w_target_len++;
    w_target_len++;
  } while (*source_ptr++);

  return w_target_len;
}


void uv_wtf8_to_utf16(const char* source_ptr,
                      uint16_t* w_target,
                      size_t w_target_len) {
  int32_t code_point;

  do {
    code_point = uv__wtf8_decode1(&source_ptr);
    /* uv_wtf8_length_as_utf16 should have been called and checked first. */
    assert(code_point >= 0);
    if (code_point > 0xFFFF) {
      assert(code_point < 0x10FFFF);
      *w_target++ = (((code_point - 0x10000) >> 10) + 0xD800);
      *w_target++ = ((code_point - 0x10000) & 0x3FF) + 0xDC00;
      w_target_len -= 2;
    } else {
      *w_target++ = code_point;
      w_target_len -= 1;
    }
  } while (*source_ptr++);

  (void)w_target_len;
  assert(w_target_len == 0);
}


static int32_t uv__get_surrogate_value(const uint16_t* w_source_ptr,
                                       ssize_t w_source_len) {
  uint16_t u;
  uint16_t next;

  u = w_source_ptr[0];
  if (u >= 0xD800 && u <= 0xDBFF && w_source_len != 1) {
    next = w_source_ptr[1];
    if (next >= 0xDC00 && next <= 0xDFFF)
      return 0x10000 + ((u - 0xD800) << 10) + (next - 0xDC00);
  }
  return u;
}


size_t uv_utf16_length_as_wtf8(const uint16_t* w_source_ptr,
                               ssize_t w_source_len) {
  size_t target_len;
  int32_t code_point;

  target_len = 0;
  while (w_source_len) {
    code_point = uv__get_surrogate_value(w_source_ptr, w_source_len);
    /* Can be invalid UTF-8 but must be valid WTF-8. */
    assert(code_point >= 0);
    if (w_source_len < 0 && code_point == 0)
      break;
    if (code_point < 0x80)
      target_len += 1;
    else if (code_point < 0x800)
      target_len += 2;
    else if (code_point < 0x10000)
      target_len += 3;
    else {
      target_len += 4;
      w_source_ptr++;
      if (w_source_len > 0)
        w_source_len--;
    }
    w_source_ptr++;
    if (w_source_len > 0)
      w_source_len--;
  }

  return target_len;
}


int uv_utf16_to_wtf8(const uint16_t* w_source_ptr,
                     ssize_t w_source_len,
                     char** target_ptr,
                     size_t* target_len_ptr) {
  size_t target_len;
  char* target;
  char* target_end;
  int32_t code_point;

  /* If *target_ptr is provided, then *target_len_ptr must be its length
   * (excluding space for NUL), otherwise we will compute the target_len_ptr
   * length and may return a new allocation in *target_ptr if target_ptr is
   * provided. */
  if (target_ptr == NULL || *target_ptr == NULL) {
    target_len = uv_utf16_length_as_wtf8(w_source_ptr, w_source_len);
    if (target_len_ptr != NULL)
      *target_len_ptr = target_len;
  } else {
    target_len = *target_len_ptr;
  }

  if (target_ptr == NULL)
    return 0;

  if (*target_ptr == NULL) {
    target = uv__malloc(target_len + 1);
    if (target == NULL) {
      return UV_ENOMEM;
    }
    *target_ptr = target;
  } else {
    target = *target_ptr;
  }

  target_end = target + target_len;

  while (target != target_end && w_source_len) {
    code_point = uv__get_surrogate_value(w_source_ptr, w_source_len);
    /* Can be invalid UTF-8 but must be valid WTF-8. */
    assert(code_point >= 0);
    if (w_source_len < 0 && code_point == 0) {
      w_source_len = 0;
      break;
    }
    if (code_point < 0x80) {
      *target++ = code_point;
    } else if (code_point < 0x800) {
      *target++ = 0xC0 | (code_point >> 6);
      if (target == target_end)
        break;
      *target++ = 0x80 | (code_point & 0x3F);
    } else if (code_point < 0x10000) {
      *target++ = 0xE0 | (code_point >> 12);
      if (target == target_end)
        break;
      *target++ = 0x80 | ((code_point >> 6) & 0x3F);
      if (target == target_end)
        break;
      *target++ = 0x80 | (code_point & 0x3F);
    } else {
      *target++ = 0xF0 | (code_point >> 18);
      if (target == target_end)
        break;
      *target++ = 0x80 | ((code_point >> 12) & 0x3F);
      if (target == target_end)
        break;
      *target++ = 0x80 | ((code_point >> 6) & 0x3F);
      if (target == target_end)
        break;
      *target++ = 0x80 | (code_point & 0x3F);
      /* uv__get_surrogate_value consumed 2 input characters */
      w_source_ptr++;
      if (w_source_len > 0)
        w_source_len--;
    }
    target_len = target - *target_ptr;
    w_source_ptr++;
    if (w_source_len > 0)
      w_source_len--;
  }

  if (target != target_end && target_len_ptr != NULL)
    /* Did not fill all of the provided buffer, so update the target_len_ptr
     * output with the space used. */
    *target_len_ptr = target - *target_ptr;

  /* Check if input fit into target exactly. */
  if (w_source_len < 0 && target == target_end && w_source_ptr[0] == 0)
    w_source_len = 0;

  *target++ = '\0';

  /* Characters remained after filling the buffer, compute the remaining length now. */
  if (w_source_len) {
    if (target_len_ptr != NULL)
      *target_len_ptr = target_len + uv_utf16_length_as_wtf8(w_source_ptr, w_source_len);
    return UV_ENOBUFS;
  }

  return 0;
}
