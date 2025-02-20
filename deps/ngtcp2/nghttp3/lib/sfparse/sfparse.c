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
#include "sfparse.h"

#include <string.h>
#include <assert.h>
#include <stdlib.h>

#ifdef __AVX2__
#  include <immintrin.h>
#endif /* __AVX2__ */

#define SFPARSE_STATE_DICT 0x08u
#define SFPARSE_STATE_LIST 0x10u
#define SFPARSE_STATE_ITEM 0x18u

#define SFPARSE_STATE_INNER_LIST 0x04u

#define SFPARSE_STATE_BEFORE 0x00u
#define SFPARSE_STATE_BEFORE_PARAMS 0x01u
#define SFPARSE_STATE_PARAMS 0x02u
#define SFPARSE_STATE_AFTER 0x03u

#define SFPARSE_STATE_OP_MASK 0x03u

#define SFPARSE_SET_STATE_AFTER(NAME)                                          \
  (SFPARSE_STATE_##NAME | SFPARSE_STATE_AFTER)
#define SFPARSE_SET_STATE_BEFORE_PARAMS(NAME)                                  \
  (SFPARSE_STATE_##NAME | SFPARSE_STATE_BEFORE_PARAMS)
#define SFPARSE_SET_STATE_INNER_LIST_BEFORE(NAME)                              \
  (SFPARSE_STATE_##NAME | SFPARSE_STATE_INNER_LIST | SFPARSE_STATE_BEFORE)

#define SFPARSE_STATE_DICT_AFTER SFPARSE_SET_STATE_AFTER(DICT)
#define SFPARSE_STATE_DICT_BEFORE_PARAMS SFPARSE_SET_STATE_BEFORE_PARAMS(DICT)
#define SFPARSE_STATE_DICT_INNER_LIST_BEFORE                                   \
  SFPARSE_SET_STATE_INNER_LIST_BEFORE(DICT)

#define SFPARSE_STATE_LIST_AFTER SFPARSE_SET_STATE_AFTER(LIST)
#define SFPARSE_STATE_LIST_BEFORE_PARAMS SFPARSE_SET_STATE_BEFORE_PARAMS(LIST)
#define SFPARSE_STATE_LIST_INNER_LIST_BEFORE                                   \
  SFPARSE_SET_STATE_INNER_LIST_BEFORE(LIST)

#define SFPARSE_STATE_ITEM_AFTER SFPARSE_SET_STATE_AFTER(ITEM)
#define SFPARSE_STATE_ITEM_BEFORE_PARAMS SFPARSE_SET_STATE_BEFORE_PARAMS(ITEM)
#define SFPARSE_STATE_ITEM_INNER_LIST_BEFORE                                   \
  SFPARSE_SET_STATE_INNER_LIST_BEFORE(ITEM)

#define SFPARSE_STATE_INITIAL 0x00u

#define DIGIT_CASES                                                            \
  case '0':                                                                    \
  case '1':                                                                    \
  case '2':                                                                    \
  case '3':                                                                    \
  case '4':                                                                    \
  case '5':                                                                    \
  case '6':                                                                    \
  case '7':                                                                    \
  case '8':                                                                    \
  case '9'

#define LCALPHA_CASES                                                          \
  case 'a':                                                                    \
  case 'b':                                                                    \
  case 'c':                                                                    \
  case 'd':                                                                    \
  case 'e':                                                                    \
  case 'f':                                                                    \
  case 'g':                                                                    \
  case 'h':                                                                    \
  case 'i':                                                                    \
  case 'j':                                                                    \
  case 'k':                                                                    \
  case 'l':                                                                    \
  case 'm':                                                                    \
  case 'n':                                                                    \
  case 'o':                                                                    \
  case 'p':                                                                    \
  case 'q':                                                                    \
  case 'r':                                                                    \
  case 's':                                                                    \
  case 't':                                                                    \
  case 'u':                                                                    \
  case 'v':                                                                    \
  case 'w':                                                                    \
  case 'x':                                                                    \
  case 'y':                                                                    \
  case 'z'

#define UCALPHA_CASES                                                          \
  case 'A':                                                                    \
  case 'B':                                                                    \
  case 'C':                                                                    \
  case 'D':                                                                    \
  case 'E':                                                                    \
  case 'F':                                                                    \
  case 'G':                                                                    \
  case 'H':                                                                    \
  case 'I':                                                                    \
  case 'J':                                                                    \
  case 'K':                                                                    \
  case 'L':                                                                    \
  case 'M':                                                                    \
  case 'N':                                                                    \
  case 'O':                                                                    \
  case 'P':                                                                    \
  case 'Q':                                                                    \
  case 'R':                                                                    \
  case 'S':                                                                    \
  case 'T':                                                                    \
  case 'U':                                                                    \
  case 'V':                                                                    \
  case 'W':                                                                    \
  case 'X':                                                                    \
  case 'Y':                                                                    \
  case 'Z'

#define ALPHA_CASES                                                            \
  UCALPHA_CASES:                                                               \
  LCALPHA_CASES

#define TOKEN_CASES                                                            \
  case '!':                                                                    \
  case '#':                                                                    \
  case '$':                                                                    \
  case '%':                                                                    \
  case '&':                                                                    \
  case '\'':                                                                   \
  case '*':                                                                    \
  case '+':                                                                    \
  case '-':                                                                    \
  case '.':                                                                    \
  case '/':                                                                    \
  DIGIT_CASES:                                                                 \
  case ':':                                                                    \
  UCALPHA_CASES:                                                               \
  case '^':                                                                    \
  case '_':                                                                    \
  case '`':                                                                    \
  LCALPHA_CASES:                                                               \
  case '|':                                                                    \
  case '~'

#define LCHEXALPHA_CASES                                                       \
  case 'a':                                                                    \
  case 'b':                                                                    \
  case 'c':                                                                    \
  case 'd':                                                                    \
  case 'e':                                                                    \
  case 'f'

#define X00_1F_CASES                                                           \
  case 0x00:                                                                   \
  case 0x01:                                                                   \
  case 0x02:                                                                   \
  case 0x03:                                                                   \
  case 0x04:                                                                   \
  case 0x05:                                                                   \
  case 0x06:                                                                   \
  case 0x07:                                                                   \
  case 0x08:                                                                   \
  case 0x09:                                                                   \
  case 0x0a:                                                                   \
  case 0x0b:                                                                   \
  case 0x0c:                                                                   \
  case 0x0d:                                                                   \
  case 0x0e:                                                                   \
  case 0x0f:                                                                   \
  case 0x10:                                                                   \
  case 0x11:                                                                   \
  case 0x12:                                                                   \
  case 0x13:                                                                   \
  case 0x14:                                                                   \
  case 0x15:                                                                   \
  case 0x16:                                                                   \
  case 0x17:                                                                   \
  case 0x18:                                                                   \
  case 0x19:                                                                   \
  case 0x1a:                                                                   \
  case 0x1b:                                                                   \
  case 0x1c:                                                                   \
  case 0x1d:                                                                   \
  case 0x1e:                                                                   \
  case 0x1f

#define X20_21_CASES                                                           \
  case ' ':                                                                    \
  case '!'

#define X23_5B_CASES                                                           \
  case '#':                                                                    \
  case '$':                                                                    \
  case '%':                                                                    \
  case '&':                                                                    \
  case '\'':                                                                   \
  case '(':                                                                    \
  case ')':                                                                    \
  case '*':                                                                    \
  case '+':                                                                    \
  case ',':                                                                    \
  case '-':                                                                    \
  case '.':                                                                    \
  case '/':                                                                    \
  DIGIT_CASES:                                                                 \
  case ':':                                                                    \
  case ';':                                                                    \
  case '<':                                                                    \
  case '=':                                                                    \
  case '>':                                                                    \
  case '?':                                                                    \
  case '@':                                                                    \
  UCALPHA_CASES:                                                               \
  case '['

#define X5D_7E_CASES                                                           \
  case ']':                                                                    \
  case '^':                                                                    \
  case '_':                                                                    \
  case '`':                                                                    \
  LCALPHA_CASES:                                                               \
  case '{':                                                                    \
  case '|':                                                                    \
  case '}':                                                                    \
  case '~'

#define X7F_FF_CASES                                                           \
  case 0x7f:                                                                   \
  case 0x80:                                                                   \
  case 0x81:                                                                   \
  case 0x82:                                                                   \
  case 0x83:                                                                   \
  case 0x84:                                                                   \
  case 0x85:                                                                   \
  case 0x86:                                                                   \
  case 0x87:                                                                   \
  case 0x88:                                                                   \
  case 0x89:                                                                   \
  case 0x8a:                                                                   \
  case 0x8b:                                                                   \
  case 0x8c:                                                                   \
  case 0x8d:                                                                   \
  case 0x8e:                                                                   \
  case 0x8f:                                                                   \
  case 0x90:                                                                   \
  case 0x91:                                                                   \
  case 0x92:                                                                   \
  case 0x93:                                                                   \
  case 0x94:                                                                   \
  case 0x95:                                                                   \
  case 0x96:                                                                   \
  case 0x97:                                                                   \
  case 0x98:                                                                   \
  case 0x99:                                                                   \
  case 0x9a:                                                                   \
  case 0x9b:                                                                   \
  case 0x9c:                                                                   \
  case 0x9d:                                                                   \
  case 0x9e:                                                                   \
  case 0x9f:                                                                   \
  case 0xa0:                                                                   \
  case 0xa1:                                                                   \
  case 0xa2:                                                                   \
  case 0xa3:                                                                   \
  case 0xa4:                                                                   \
  case 0xa5:                                                                   \
  case 0xa6:                                                                   \
  case 0xa7:                                                                   \
  case 0xa8:                                                                   \
  case 0xa9:                                                                   \
  case 0xaa:                                                                   \
  case 0xab:                                                                   \
  case 0xac:                                                                   \
  case 0xad:                                                                   \
  case 0xae:                                                                   \
  case 0xaf:                                                                   \
  case 0xb0:                                                                   \
  case 0xb1:                                                                   \
  case 0xb2:                                                                   \
  case 0xb3:                                                                   \
  case 0xb4:                                                                   \
  case 0xb5:                                                                   \
  case 0xb6:                                                                   \
  case 0xb7:                                                                   \
  case 0xb8:                                                                   \
  case 0xb9:                                                                   \
  case 0xba:                                                                   \
  case 0xbb:                                                                   \
  case 0xbc:                                                                   \
  case 0xbd:                                                                   \
  case 0xbe:                                                                   \
  case 0xbf:                                                                   \
  case 0xc0:                                                                   \
  case 0xc1:                                                                   \
  case 0xc2:                                                                   \
  case 0xc3:                                                                   \
  case 0xc4:                                                                   \
  case 0xc5:                                                                   \
  case 0xc6:                                                                   \
  case 0xc7:                                                                   \
  case 0xc8:                                                                   \
  case 0xc9:                                                                   \
  case 0xca:                                                                   \
  case 0xcb:                                                                   \
  case 0xcc:                                                                   \
  case 0xcd:                                                                   \
  case 0xce:                                                                   \
  case 0xcf:                                                                   \
  case 0xd0:                                                                   \
  case 0xd1:                                                                   \
  case 0xd2:                                                                   \
  case 0xd3:                                                                   \
  case 0xd4:                                                                   \
  case 0xd5:                                                                   \
  case 0xd6:                                                                   \
  case 0xd7:                                                                   \
  case 0xd8:                                                                   \
  case 0xd9:                                                                   \
  case 0xda:                                                                   \
  case 0xdb:                                                                   \
  case 0xdc:                                                                   \
  case 0xdd:                                                                   \
  case 0xde:                                                                   \
  case 0xdf:                                                                   \
  case 0xe0:                                                                   \
  case 0xe1:                                                                   \
  case 0xe2:                                                                   \
  case 0xe3:                                                                   \
  case 0xe4:                                                                   \
  case 0xe5:                                                                   \
  case 0xe6:                                                                   \
  case 0xe7:                                                                   \
  case 0xe8:                                                                   \
  case 0xe9:                                                                   \
  case 0xea:                                                                   \
  case 0xeb:                                                                   \
  case 0xec:                                                                   \
  case 0xed:                                                                   \
  case 0xee:                                                                   \
  case 0xef:                                                                   \
  case 0xf0:                                                                   \
  case 0xf1:                                                                   \
  case 0xf2:                                                                   \
  case 0xf3:                                                                   \
  case 0xf4:                                                                   \
  case 0xf5:                                                                   \
  case 0xf6:                                                                   \
  case 0xf7:                                                                   \
  case 0xf8:                                                                   \
  case 0xf9:                                                                   \
  case 0xfa:                                                                   \
  case 0xfb:                                                                   \
  case 0xfc:                                                                   \
  case 0xfd:                                                                   \
  case 0xfe:                                                                   \
  case 0xff

static int is_ws(uint8_t c) {
  switch (c) {
  case ' ':
  case '\t':
    return 1;
  default:
    return 0;
  }
}

#ifdef __AVX2__
#  ifdef _MSC_VER
#    include <intrin.h>

static int ctz(unsigned int v) {
  unsigned long n;

  /* Assume that v is not 0. */
  _BitScanForward(&n, v);

  return (int)n;
}
#  else /* !_MSC_VER */
#    define ctz __builtin_ctz
#  endif /* !_MSC_VER */
#endif   /* __AVX2__ */

static int parser_eof(sfparse_parser *sfp) { return sfp->pos == sfp->end; }

static void parser_discard_ows(sfparse_parser *sfp) {
  for (; !parser_eof(sfp) && is_ws(*sfp->pos); ++sfp->pos)
    ;
}

static void parser_discard_sp(sfparse_parser *sfp) {
  for (; !parser_eof(sfp) && *sfp->pos == ' '; ++sfp->pos)
    ;
}

static void parser_set_op_state(sfparse_parser *sfp, uint32_t op) {
  sfp->state &= ~SFPARSE_STATE_OP_MASK;
  sfp->state |= op;
}

static void parser_unset_inner_list_state(sfparse_parser *sfp) {
  sfp->state &= ~SFPARSE_STATE_INNER_LIST;
}

#ifdef __AVX2__
static const uint8_t *find_char_key(const uint8_t *first, const uint8_t *last) {
  const __m256i us = _mm256_set1_epi8('_');
  const __m256i ds = _mm256_set1_epi8('-');
  const __m256i dot = _mm256_set1_epi8('.');
  const __m256i ast = _mm256_set1_epi8('*');
  const __m256i r0l = _mm256_set1_epi8('0' - 1);
  const __m256i r0r = _mm256_set1_epi8('9' + 1);
  const __m256i r1l = _mm256_set1_epi8('a' - 1);
  const __m256i r1r = _mm256_set1_epi8('z' + 1);
  __m256i s, x;
  uint32_t m;

  for (; first != last; first += 32) {
    s = _mm256_loadu_si256((void *)first);

    x = _mm256_cmpeq_epi8(s, us);
    x = _mm256_or_si256(_mm256_cmpeq_epi8(s, ds), x);
    x = _mm256_or_si256(_mm256_cmpeq_epi8(s, dot), x);
    x = _mm256_or_si256(_mm256_cmpeq_epi8(s, ast), x);
    x = _mm256_or_si256(
      _mm256_and_si256(_mm256_cmpgt_epi8(s, r0l), _mm256_cmpgt_epi8(r0r, s)),
      x);
    x = _mm256_or_si256(
      _mm256_and_si256(_mm256_cmpgt_epi8(s, r1l), _mm256_cmpgt_epi8(r1r, s)),
      x);

    m = ~(uint32_t)_mm256_movemask_epi8(x);
    if (m) {
      return first + ctz(m);
    }
  }

  return last;
}
#endif /* __AVX2__ */

static int parser_key(sfparse_parser *sfp, sfparse_vec *dest) {
  const uint8_t *base;
#ifdef __AVX2__
  const uint8_t *last;
#endif /* __AVX2__ */

  switch (*sfp->pos) {
  case '*':
  LCALPHA_CASES:
    break;
  default:
    return SFPARSE_ERR_PARSE;
  }

  base = sfp->pos++;

#ifdef __AVX2__
  if (sfp->end - sfp->pos >= 32) {
    last = sfp->pos + ((sfp->end - sfp->pos) & ~0x1fu);

    sfp->pos = find_char_key(sfp->pos, last);
    if (sfp->pos != last) {
      goto fin;
    }
  }
#endif /* __AVX2__ */

  for (; !parser_eof(sfp); ++sfp->pos) {
    switch (*sfp->pos) {
    case '_':
    case '-':
    case '.':
    case '*':
    DIGIT_CASES:
    LCALPHA_CASES:
      continue;
    }

    break;
  }

#ifdef __AVX2__
fin:
#endif /* __AVX2__ */
  if (dest) {
    dest->base = (uint8_t *)base;
    dest->len = (size_t)(sfp->pos - dest->base);
  }

  return 0;
}

static int parser_number(sfparse_parser *sfp, sfparse_value *dest) {
  int sign = 1;
  int64_t value = 0;
  size_t len = 0;
  size_t fpos = 0;

  if (*sfp->pos == '-') {
    ++sfp->pos;
    if (parser_eof(sfp)) {
      return SFPARSE_ERR_PARSE;
    }

    sign = -1;
  }

  assert(!parser_eof(sfp));

  for (; !parser_eof(sfp); ++sfp->pos) {
    switch (*sfp->pos) {
    DIGIT_CASES:
      if (++len > 15) {
        return SFPARSE_ERR_PARSE;
      }

      value *= 10;
      value += *sfp->pos - '0';

      continue;
    }

    break;
  }

  if (len == 0) {
    return SFPARSE_ERR_PARSE;
  }

  if (parser_eof(sfp) || *sfp->pos != '.') {
    if (dest) {
      dest->type = SFPARSE_TYPE_INTEGER;
      dest->flags = SFPARSE_VALUE_FLAG_NONE;
      dest->integer = value * sign;
    }

    return 0;
  }

  /* decimal */

  if (len > 12) {
    return SFPARSE_ERR_PARSE;
  }

  fpos = len;

  ++sfp->pos;

  for (; !parser_eof(sfp); ++sfp->pos) {
    switch (*sfp->pos) {
    DIGIT_CASES:
      if (++len > 15) {
        return SFPARSE_ERR_PARSE;
      }

      value *= 10;
      value += *sfp->pos - '0';

      continue;
    }

    break;
  }

  if (fpos == len || len - fpos > 3) {
    return SFPARSE_ERR_PARSE;
  }

  if (dest) {
    dest->type = SFPARSE_TYPE_DECIMAL;
    dest->flags = SFPARSE_VALUE_FLAG_NONE;
    dest->decimal.numer = value * sign;

    switch (len - fpos) {
    case 1:
      dest->decimal.denom = 10;

      break;
    case 2:
      dest->decimal.denom = 100;

      break;
    case 3:
      dest->decimal.denom = 1000;

      break;
    }
  }

  return 0;
}

static int parser_date(sfparse_parser *sfp, sfparse_value *dest) {
  int rv;
  sfparse_value val;

  /* The first byte has already been validated by the caller. */
  assert('@' == *sfp->pos);

  ++sfp->pos;

  if (parser_eof(sfp)) {
    return SFPARSE_ERR_PARSE;
  }

  rv = parser_number(sfp, &val);
  if (rv != 0) {
    return rv;
  }

  if (val.type != SFPARSE_TYPE_INTEGER) {
    return SFPARSE_ERR_PARSE;
  }

  if (dest) {
    *dest = val;
    dest->type = SFPARSE_TYPE_DATE;
  }

  return 0;
}

#ifdef __AVX2__
static const uint8_t *find_char_string(const uint8_t *first,
                                       const uint8_t *last) {
  const __m256i bs = _mm256_set1_epi8('\\');
  const __m256i dq = _mm256_set1_epi8('"');
  const __m256i del = _mm256_set1_epi8(0x7f);
  const __m256i sp = _mm256_set1_epi8(' ');
  __m256i s, x;
  uint32_t m;

  for (; first != last; first += 32) {
    s = _mm256_loadu_si256((void *)first);

    x = _mm256_cmpgt_epi8(sp, s);
    x = _mm256_or_si256(_mm256_cmpeq_epi8(s, bs), x);
    x = _mm256_or_si256(_mm256_cmpeq_epi8(s, dq), x);
    x = _mm256_or_si256(_mm256_cmpeq_epi8(s, del), x);

    m = (uint32_t)_mm256_movemask_epi8(x);
    if (m) {
      return first + ctz(m);
    }
  }

  return last;
}
#endif /* __AVX2__ */

static int parser_string(sfparse_parser *sfp, sfparse_value *dest) {
  const uint8_t *base;
#ifdef __AVX2__
  const uint8_t *last;
#endif /* __AVX2__ */
  uint32_t flags = SFPARSE_VALUE_FLAG_NONE;

  /* The first byte has already been validated by the caller. */
  assert('"' == *sfp->pos);

  base = ++sfp->pos;

#ifdef __AVX2__
  for (; sfp->end - sfp->pos >= 32; ++sfp->pos) {
    last = sfp->pos + ((sfp->end - sfp->pos) & ~0x1fu);

    sfp->pos = find_char_string(sfp->pos, last);
    if (sfp->pos == last) {
      break;
    }

    switch (*sfp->pos) {
    case '\\':
      ++sfp->pos;
      if (parser_eof(sfp)) {
        return SFPARSE_ERR_PARSE;
      }

      switch (*sfp->pos) {
      case '"':
      case '\\':
        flags = SFPARSE_VALUE_FLAG_ESCAPED_STRING;

        break;
      default:
        return SFPARSE_ERR_PARSE;
      }

      break;
    case '"':
      goto fin;
    default:
      return SFPARSE_ERR_PARSE;
    }
  }
#endif /* __AVX2__ */

  for (; !parser_eof(sfp); ++sfp->pos) {
    switch (*sfp->pos) {
    X20_21_CASES:
    X23_5B_CASES:
    X5D_7E_CASES:
      break;
    case '\\':
      ++sfp->pos;
      if (parser_eof(sfp)) {
        return SFPARSE_ERR_PARSE;
      }

      switch (*sfp->pos) {
      case '"':
      case '\\':
        flags = SFPARSE_VALUE_FLAG_ESCAPED_STRING;

        break;
      default:
        return SFPARSE_ERR_PARSE;
      }

      break;
    case '"':
      goto fin;
    default:
      return SFPARSE_ERR_PARSE;
    }
  }

  return SFPARSE_ERR_PARSE;

fin:
  if (dest) {
    dest->type = SFPARSE_TYPE_STRING;
    dest->flags = flags;
    dest->vec.len = (size_t)(sfp->pos - base);
    dest->vec.base = dest->vec.len == 0 ? NULL : (uint8_t *)base;
  }

  ++sfp->pos;

  return 0;
}

#ifdef __AVX2__
static const uint8_t *find_char_token(const uint8_t *first,
                                      const uint8_t *last) {
  /* r0: !..:, excluding "(),
     r1: A..Z
     r2: ^..~, excluding {} */
  const __m256i r0l = _mm256_set1_epi8('!' - 1);
  const __m256i r0r = _mm256_set1_epi8(':' + 1);
  const __m256i dq = _mm256_set1_epi8('"');
  const __m256i prl = _mm256_set1_epi8('(');
  const __m256i prr = _mm256_set1_epi8(')');
  const __m256i comma = _mm256_set1_epi8(',');
  const __m256i r1l = _mm256_set1_epi8('A' - 1);
  const __m256i r1r = _mm256_set1_epi8('Z' + 1);
  const __m256i r2l = _mm256_set1_epi8('^' - 1);
  const __m256i r2r = _mm256_set1_epi8('~' + 1);
  const __m256i cbl = _mm256_set1_epi8('{');
  const __m256i cbr = _mm256_set1_epi8('}');
  __m256i s, x;
  uint32_t m;

  for (; first != last; first += 32) {
    s = _mm256_loadu_si256((void *)first);

    x = _mm256_andnot_si256(
      _mm256_cmpeq_epi8(s, comma),
      _mm256_andnot_si256(
        _mm256_cmpeq_epi8(s, prr),
        _mm256_andnot_si256(
          _mm256_cmpeq_epi8(s, prl),
          _mm256_andnot_si256(_mm256_cmpeq_epi8(s, dq),
                              _mm256_and_si256(_mm256_cmpgt_epi8(s, r0l),
                                               _mm256_cmpgt_epi8(r0r, s))))));
    x = _mm256_or_si256(
      _mm256_and_si256(_mm256_cmpgt_epi8(s, r1l), _mm256_cmpgt_epi8(r1r, s)),
      x);
    x = _mm256_or_si256(
      _mm256_andnot_si256(
        _mm256_cmpeq_epi8(s, cbr),
        _mm256_andnot_si256(_mm256_cmpeq_epi8(s, cbl),
                            _mm256_and_si256(_mm256_cmpgt_epi8(s, r2l),
                                             _mm256_cmpgt_epi8(r2r, s)))),
      x);

    m = ~(uint32_t)_mm256_movemask_epi8(x);
    if (m) {
      return first + ctz(m);
    }
  }

  return last;
}
#endif /* __AVX2__ */

static int parser_token(sfparse_parser *sfp, sfparse_value *dest) {
  const uint8_t *base;
#ifdef __AVX2__
  const uint8_t *last;
#endif /* __AVX2__ */

  /* The first byte has already been validated by the caller. */
  base = sfp->pos++;

#ifdef __AVX2__
  if (sfp->end - sfp->pos >= 32) {
    last = sfp->pos + ((sfp->end - sfp->pos) & ~0x1fu);

    sfp->pos = find_char_token(sfp->pos, last);
    if (sfp->pos != last) {
      goto fin;
    }
  }
#endif /* __AVX2__ */

  for (; !parser_eof(sfp); ++sfp->pos) {
    switch (*sfp->pos) {
    TOKEN_CASES:
      continue;
    }

    break;
  }

#ifdef __AVX2__
fin:
#endif /* __AVX2__ */
  if (dest) {
    dest->type = SFPARSE_TYPE_TOKEN;
    dest->flags = SFPARSE_VALUE_FLAG_NONE;
    dest->vec.base = (uint8_t *)base;
    dest->vec.len = (size_t)(sfp->pos - base);
  }

  return 0;
}

#ifdef __AVX2__
static const uint8_t *find_char_byteseq(const uint8_t *first,
                                        const uint8_t *last) {
  const __m256i pls = _mm256_set1_epi8('+');
  const __m256i fs = _mm256_set1_epi8('/');
  const __m256i r0l = _mm256_set1_epi8('0' - 1);
  const __m256i r0r = _mm256_set1_epi8('9' + 1);
  const __m256i r1l = _mm256_set1_epi8('A' - 1);
  const __m256i r1r = _mm256_set1_epi8('Z' + 1);
  const __m256i r2l = _mm256_set1_epi8('a' - 1);
  const __m256i r2r = _mm256_set1_epi8('z' + 1);
  __m256i s, x;
  uint32_t m;

  for (; first != last; first += 32) {
    s = _mm256_loadu_si256((void *)first);

    x = _mm256_cmpeq_epi8(s, pls);
    x = _mm256_or_si256(_mm256_cmpeq_epi8(s, fs), x);
    x = _mm256_or_si256(
      _mm256_and_si256(_mm256_cmpgt_epi8(s, r0l), _mm256_cmpgt_epi8(r0r, s)),
      x);
    x = _mm256_or_si256(
      _mm256_and_si256(_mm256_cmpgt_epi8(s, r1l), _mm256_cmpgt_epi8(r1r, s)),
      x);
    x = _mm256_or_si256(
      _mm256_and_si256(_mm256_cmpgt_epi8(s, r2l), _mm256_cmpgt_epi8(r2r, s)),
      x);

    m = ~(uint32_t)_mm256_movemask_epi8(x);
    if (m) {
      return first + ctz(m);
    }
  }

  return last;
}
#endif /* __AVX2__ */

static int parser_byteseq(sfparse_parser *sfp, sfparse_value *dest) {
  const uint8_t *base;
#ifdef __AVX2__
  const uint8_t *last;
#endif /* __AVX2__ */

  /* The first byte has already been validated by the caller. */
  assert(':' == *sfp->pos);

  base = ++sfp->pos;

#ifdef __AVX2__
  if (sfp->end - sfp->pos >= 32) {
    last = sfp->pos + ((sfp->end - sfp->pos) & ~0x1fu);
    sfp->pos = find_char_byteseq(sfp->pos, last);
  }
#endif /* __AVX2__ */

  for (; !parser_eof(sfp); ++sfp->pos) {
    switch (*sfp->pos) {
    case '+':
    case '/':
    DIGIT_CASES:
    ALPHA_CASES:
      continue;
    case '=':
      switch ((sfp->pos - base) & 0x3) {
      case 0:
      case 1:
        return SFPARSE_ERR_PARSE;
      case 2:
        ++sfp->pos;

        if (parser_eof(sfp)) {
          return SFPARSE_ERR_PARSE;
        }

        if (*sfp->pos == '=') {
          ++sfp->pos;
        }

        break;
      case 3:
        ++sfp->pos;

        break;
      }

      if (parser_eof(sfp) || *sfp->pos != ':') {
        return SFPARSE_ERR_PARSE;
      }

      goto fin;
    case ':':
      if (((sfp->pos - base) & 0x3) == 1) {
        return SFPARSE_ERR_PARSE;
      }

      goto fin;
    default:
      return SFPARSE_ERR_PARSE;
    }
  }

  return SFPARSE_ERR_PARSE;

fin:
  if (dest) {
    dest->type = SFPARSE_TYPE_BYTESEQ;
    dest->flags = SFPARSE_VALUE_FLAG_NONE;
    dest->vec.len = (size_t)(sfp->pos - base);
    dest->vec.base = dest->vec.len == 0 ? NULL : (uint8_t *)base;
  }

  ++sfp->pos;

  return 0;
}

static int parser_boolean(sfparse_parser *sfp, sfparse_value *dest) {
  int b;

  /* The first byte has already been validated by the caller. */
  assert('?' == *sfp->pos);

  ++sfp->pos;

  if (parser_eof(sfp)) {
    return SFPARSE_ERR_PARSE;
  }

  switch (*sfp->pos) {
  case '0':
    b = 0;

    break;
  case '1':
    b = 1;

    break;
  default:
    return SFPARSE_ERR_PARSE;
  }

  ++sfp->pos;

  if (dest) {
    dest->type = SFPARSE_TYPE_BOOLEAN;
    dest->flags = SFPARSE_VALUE_FLAG_NONE;
    dest->boolean = b;
  }

  return 0;
}

static int pctdecode(uint8_t *pc, const uint8_t **ppos) {
  uint8_t c, b = **ppos;

  switch (b) {
  DIGIT_CASES:
    c = (uint8_t)((b - '0') << 4);

    break;
  LCHEXALPHA_CASES:
    c = (uint8_t)((b - 'a' + 10) << 4);

    break;
  default:
    return -1;
  }

  b = *++*ppos;

  switch (b) {
  DIGIT_CASES:
    c |= (uint8_t)(b - '0');

    break;
  LCHEXALPHA_CASES:
    c |= (uint8_t)(b - 'a' + 10);

    break;
  default:
    return -1;
  }

  *pc = c;
  ++*ppos;

  return 0;
}

/* Start of utf8 dfa */
/* Copyright (c) 2008-2010 Bjoern Hoehrmann <bjoern@hoehrmann.de>
 * See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
 *
 * Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#define UTF8_ACCEPT 0
#define UTF8_REJECT 12

/* clang-format off */
static const uint8_t utf8d[] = {
  /*
   * The first part of the table maps bytes to character classes that
   * to reduce the size of the transition table and create bitmasks.
   */
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
   1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
   7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
   8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
  10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

   /*
    * The second part is a transition table that maps a combination
    * of a state of the automaton and a character class to a state.
    */
   0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
  12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
  12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
  12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
  12,36,12,12,12,12,12,12,12,12,12,12,
};
/* clang-format on */

static void utf8_decode(uint32_t *state, uint8_t byte) {
  *state = utf8d[256 + *state + utf8d[byte]];
}

/* End of utf8 dfa */

static int parser_dispstring(sfparse_parser *sfp, sfparse_value *dest) {
  const uint8_t *base;
  uint8_t c;
  uint32_t utf8state = UTF8_ACCEPT;

  assert('%' == *sfp->pos);

  ++sfp->pos;

  if (parser_eof(sfp) || *sfp->pos != '"') {
    return SFPARSE_ERR_PARSE;
  }

  base = ++sfp->pos;

  for (; !parser_eof(sfp);) {
    switch (*sfp->pos) {
    X00_1F_CASES:
    X7F_FF_CASES:
      return SFPARSE_ERR_PARSE;
    case '%':
      ++sfp->pos;

      if (sfp->pos + 2 > sfp->end) {
        return SFPARSE_ERR_PARSE;
      }

      if (pctdecode(&c, &sfp->pos) != 0) {
        return SFPARSE_ERR_PARSE;
      }

      utf8_decode(&utf8state, c);
      if (utf8state == UTF8_REJECT) {
        return SFPARSE_ERR_PARSE;
      }

      break;
    case '"':
      if (utf8state != UTF8_ACCEPT) {
        return SFPARSE_ERR_PARSE;
      }

      if (dest) {
        dest->type = SFPARSE_TYPE_DISPSTRING;
        dest->flags = SFPARSE_VALUE_FLAG_NONE;
        dest->vec.len = (size_t)(sfp->pos - base);
        dest->vec.base = dest->vec.len == 0 ? NULL : (uint8_t *)base;
      }

      ++sfp->pos;

      return 0;
    default:
      if (utf8state != UTF8_ACCEPT) {
        return SFPARSE_ERR_PARSE;
      }

      ++sfp->pos;
    }
  }

  return SFPARSE_ERR_PARSE;
}

static int parser_bare_item(sfparse_parser *sfp, sfparse_value *dest) {
  switch (*sfp->pos) {
  case '"':
    return parser_string(sfp, dest);
  case '-':
  DIGIT_CASES:
    return parser_number(sfp, dest);
  case '@':
    return parser_date(sfp, dest);
  case ':':
    return parser_byteseq(sfp, dest);
  case '?':
    return parser_boolean(sfp, dest);
  case '*':
  ALPHA_CASES:
    return parser_token(sfp, dest);
  case '%':
    return parser_dispstring(sfp, dest);
  default:
    return SFPARSE_ERR_PARSE;
  }
}

static int parser_skip_inner_list(sfparse_parser *sfp);

int sfparse_parser_param(sfparse_parser *sfp, sfparse_vec *dest_key,
                         sfparse_value *dest_value) {
  int rv;

  switch (sfp->state & SFPARSE_STATE_OP_MASK) {
  case SFPARSE_STATE_BEFORE:
    rv = parser_skip_inner_list(sfp);
    if (rv != 0) {
      return rv;
    }

    /* fall through */
  case SFPARSE_STATE_BEFORE_PARAMS:
    parser_set_op_state(sfp, SFPARSE_STATE_PARAMS);

    break;
  case SFPARSE_STATE_PARAMS:
    break;
  default:
    assert(0);
    abort();
  }

  if (parser_eof(sfp) || *sfp->pos != ';') {
    parser_set_op_state(sfp, SFPARSE_STATE_AFTER);

    return SFPARSE_ERR_EOF;
  }

  ++sfp->pos;

  parser_discard_sp(sfp);
  if (parser_eof(sfp)) {
    return SFPARSE_ERR_PARSE;
  }

  rv = parser_key(sfp, dest_key);
  if (rv != 0) {
    return rv;
  }

  if (parser_eof(sfp) || *sfp->pos != '=') {
    if (dest_value) {
      dest_value->type = SFPARSE_TYPE_BOOLEAN;
      dest_value->flags = SFPARSE_VALUE_FLAG_NONE;
      dest_value->boolean = 1;
    }

    return 0;
  }

  ++sfp->pos;

  if (parser_eof(sfp)) {
    return SFPARSE_ERR_PARSE;
  }

  return parser_bare_item(sfp, dest_value);
}

static int parser_skip_params(sfparse_parser *sfp) {
  int rv;

  for (;;) {
    rv = sfparse_parser_param(sfp, NULL, NULL);
    switch (rv) {
    case 0:
      break;
    case SFPARSE_ERR_EOF:
      return 0;
    case SFPARSE_ERR_PARSE:
      return rv;
    default:
      assert(0);
      abort();
    }
  }
}

int sfparse_parser_inner_list(sfparse_parser *sfp, sfparse_value *dest) {
  int rv;

  switch (sfp->state & SFPARSE_STATE_OP_MASK) {
  case SFPARSE_STATE_BEFORE:
    parser_discard_sp(sfp);
    if (parser_eof(sfp)) {
      return SFPARSE_ERR_PARSE;
    }

    break;
  case SFPARSE_STATE_BEFORE_PARAMS:
    rv = parser_skip_params(sfp);
    if (rv != 0) {
      return rv;
    }

    /* Technically, we are entering SFPARSE_STATE_AFTER, but we will set
       another state without reading the state. */
    /* parser_set_op_state(sfp, SFPARSE_STATE_AFTER); */

    /* fall through */
  case SFPARSE_STATE_AFTER:
    if (parser_eof(sfp)) {
      return SFPARSE_ERR_PARSE;
    }

    switch (*sfp->pos) {
    case ' ':
      parser_discard_sp(sfp);
      if (parser_eof(sfp)) {
        return SFPARSE_ERR_PARSE;
      }

      break;
    case ')':
      break;
    default:
      return SFPARSE_ERR_PARSE;
    }

    break;
  default:
    assert(0);
    abort();
  }

  if (*sfp->pos == ')') {
    ++sfp->pos;

    parser_unset_inner_list_state(sfp);
    parser_set_op_state(sfp, SFPARSE_STATE_BEFORE_PARAMS);

    return SFPARSE_ERR_EOF;
  }

  rv = parser_bare_item(sfp, dest);
  if (rv != 0) {
    return rv;
  }

  parser_set_op_state(sfp, SFPARSE_STATE_BEFORE_PARAMS);

  return 0;
}

static int parser_skip_inner_list(sfparse_parser *sfp) {
  int rv;

  for (;;) {
    rv = sfparse_parser_inner_list(sfp, NULL);
    switch (rv) {
    case 0:
      break;
    case SFPARSE_ERR_EOF:
      return 0;
    case SFPARSE_ERR_PARSE:
      return rv;
    default:
      assert(0);
      abort();
    }
  }
}

static int parser_next_key_or_item(sfparse_parser *sfp) {
  parser_discard_ows(sfp);

  if (parser_eof(sfp)) {
    return SFPARSE_ERR_EOF;
  }

  if (*sfp->pos != ',') {
    return SFPARSE_ERR_PARSE;
  }

  ++sfp->pos;

  parser_discard_ows(sfp);
  if (parser_eof(sfp)) {
    return SFPARSE_ERR_PARSE;
  }

  return 0;
}

static int parser_dict_value(sfparse_parser *sfp, sfparse_value *dest) {
  int rv;

  if (parser_eof(sfp) || *(sfp->pos) != '=') {
    /* Boolean true */
    if (dest) {
      dest->type = SFPARSE_TYPE_BOOLEAN;
      dest->flags = SFPARSE_VALUE_FLAG_NONE;
      dest->boolean = 1;
    }

    sfp->state = SFPARSE_STATE_DICT_BEFORE_PARAMS;

    return 0;
  }

  ++sfp->pos;

  if (parser_eof(sfp)) {
    return SFPARSE_ERR_PARSE;
  }

  if (*sfp->pos == '(') {
    if (dest) {
      dest->type = SFPARSE_TYPE_INNER_LIST;
      dest->flags = SFPARSE_VALUE_FLAG_NONE;
    }

    ++sfp->pos;

    sfp->state = SFPARSE_STATE_DICT_INNER_LIST_BEFORE;

    return 0;
  }

  rv = parser_bare_item(sfp, dest);
  if (rv != 0) {
    return rv;
  }

  sfp->state = SFPARSE_STATE_DICT_BEFORE_PARAMS;

  return 0;
}

int sfparse_parser_dict(sfparse_parser *sfp, sfparse_vec *dest_key,
                        sfparse_value *dest_value) {
  int rv;

  switch (sfp->state) {
  case SFPARSE_STATE_DICT_INNER_LIST_BEFORE:
    rv = parser_skip_inner_list(sfp);
    if (rv != 0) {
      return rv;
    }

    /* fall through */
  case SFPARSE_STATE_DICT_BEFORE_PARAMS:
    rv = parser_skip_params(sfp);
    if (rv != 0) {
      return rv;
    }

    /* fall through */
  case SFPARSE_STATE_DICT_AFTER:
    rv = parser_next_key_or_item(sfp);
    if (rv != 0) {
      return rv;
    }

    break;
  case SFPARSE_STATE_INITIAL:
    parser_discard_sp(sfp);

    if (parser_eof(sfp)) {
      return SFPARSE_ERR_EOF;
    }

    break;
  default:
    assert(0);
    abort();
  }

  rv = parser_key(sfp, dest_key);
  if (rv != 0) {
    return rv;
  }

  return parser_dict_value(sfp, dest_value);
}

int sfparse_parser_list(sfparse_parser *sfp, sfparse_value *dest) {
  int rv;

  switch (sfp->state) {
  case SFPARSE_STATE_LIST_INNER_LIST_BEFORE:
    rv = parser_skip_inner_list(sfp);
    if (rv != 0) {
      return rv;
    }

    /* fall through */
  case SFPARSE_STATE_LIST_BEFORE_PARAMS:
    rv = parser_skip_params(sfp);
    if (rv != 0) {
      return rv;
    }

    /* fall through */
  case SFPARSE_STATE_LIST_AFTER:
    rv = parser_next_key_or_item(sfp);
    if (rv != 0) {
      return rv;
    }

    break;
  case SFPARSE_STATE_INITIAL:
    parser_discard_sp(sfp);

    if (parser_eof(sfp)) {
      return SFPARSE_ERR_EOF;
    }

    break;
  default:
    assert(0);
    abort();
  }

  if (*sfp->pos == '(') {
    if (dest) {
      dest->type = SFPARSE_TYPE_INNER_LIST;
      dest->flags = SFPARSE_VALUE_FLAG_NONE;
    }

    ++sfp->pos;

    sfp->state = SFPARSE_STATE_LIST_INNER_LIST_BEFORE;

    return 0;
  }

  rv = parser_bare_item(sfp, dest);
  if (rv != 0) {
    return rv;
  }

  sfp->state = SFPARSE_STATE_LIST_BEFORE_PARAMS;

  return 0;
}

int sfparse_parser_item(sfparse_parser *sfp, sfparse_value *dest) {
  int rv;

  switch (sfp->state) {
  case SFPARSE_STATE_INITIAL:
    parser_discard_sp(sfp);

    if (parser_eof(sfp)) {
      return SFPARSE_ERR_PARSE;
    }

    break;
  case SFPARSE_STATE_ITEM_INNER_LIST_BEFORE:
    rv = parser_skip_inner_list(sfp);
    if (rv != 0) {
      return rv;
    }

    /* fall through */
  case SFPARSE_STATE_ITEM_BEFORE_PARAMS:
    rv = parser_skip_params(sfp);
    if (rv != 0) {
      return rv;
    }

    /* fall through */
  case SFPARSE_STATE_ITEM_AFTER:
    parser_discard_sp(sfp);

    if (!parser_eof(sfp)) {
      return SFPARSE_ERR_PARSE;
    }

    return SFPARSE_ERR_EOF;
  default:
    assert(0);
    abort();
  }

  if (*sfp->pos == '(') {
    if (dest) {
      dest->type = SFPARSE_TYPE_INNER_LIST;
      dest->flags = SFPARSE_VALUE_FLAG_NONE;
    }

    ++sfp->pos;

    sfp->state = SFPARSE_STATE_ITEM_INNER_LIST_BEFORE;

    return 0;
  }

  rv = parser_bare_item(sfp, dest);
  if (rv != 0) {
    return rv;
  }

  sfp->state = SFPARSE_STATE_ITEM_BEFORE_PARAMS;

  return 0;
}

void sfparse_parser_init(sfparse_parser *sfp, const uint8_t *data,
                         size_t datalen) {
  if (datalen == 0) {
    sfp->pos = sfp->end = NULL;
  } else {
    sfp->pos = data;
    sfp->end = data + datalen;
  }

  sfp->state = SFPARSE_STATE_INITIAL;
}

void sfparse_unescape(sfparse_vec *dest, const sfparse_vec *src) {
  const uint8_t *p, *q;
  uint8_t *o;
  size_t len, slen;

  if (src->len == 0) {
    dest->len = 0;

    return;
  }

  o = dest->base;
  p = src->base;
  len = src->len;

  for (;;) {
    q = memchr(p, '\\', len);
    if (q == NULL) {
      memcpy(o, p, len);
      o += len;

      dest->len = (size_t)(o - dest->base);

      return;
    }

    slen = (size_t)(q - p);
    memcpy(o, p, slen);
    o += slen;

    p = q + 1;
    *o++ = *p++;
    len -= slen + 2;
  }
}

void sfparse_base64decode(sfparse_vec *dest, const sfparse_vec *src) {
  static const int index_tbl[] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60,
    61, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1,
    -1, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
    43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1};
  uint8_t *o;
  const uint8_t *p, *end;
  uint32_t n;
  size_t i, left;
  int idx;

  if (src->len == 0) {
    dest->len = 0;

    return;
  }

  o = dest->base;
  p = src->base;
  left = src->len & 0x3;
  if (left == 0 && src->base[src->len - 1] == '=') {
    left = 4;
  }
  end = src->base + src->len - left;

  for (; p != end;) {
    n = 0;

    for (i = 1; i <= 4; ++i, ++p) {
      idx = index_tbl[*p];

      assert(idx != -1);

      n += (uint32_t)(idx << (24 - i * 6));
    }

    *o++ = (uint8_t)(n >> 16);
    *o++ = (n >> 8) & 0xffu;
    *o++ = n & 0xffu;
  }

  switch (left) {
  case 0:
    goto fin;
  case 1:
    assert(0);
    abort();
  case 3:
    if (src->base[src->len - 1] == '=') {
      left = 2;
    }

    break;
  case 4:
    assert('=' == src->base[src->len - 1]);

    if (src->base[src->len - 2] == '=') {
      left = 2;
    } else {
      left = 3;
    }

    break;
  }

  switch (left) {
  case 2:
    *o = (uint8_t)(index_tbl[*p++] << 2);
    *o++ |= (uint8_t)(index_tbl[*p++] >> 4);

    break;
  case 3:
    n = (uint32_t)(index_tbl[*p++] << 10);
    n += (uint32_t)(index_tbl[*p++] << 4);
    n += (uint32_t)(index_tbl[*p++] >> 2);
    *o++ = (n >> 8) & 0xffu;
    *o++ = n & 0xffu;

    break;
  }

fin:
  dest->len = (size_t)(o - dest->base);
}

void sfparse_pctdecode(sfparse_vec *dest, const sfparse_vec *src) {
  const uint8_t *p, *q;
  uint8_t *o;
  size_t len, slen;

  if (src->len == 0) {
    dest->len = 0;

    return;
  }

  o = dest->base;
  p = src->base;
  len = src->len;

  for (;;) {
    q = memchr(p, '%', len);
    if (q == NULL) {
      memcpy(o, p, len);
      o += len;

      dest->len = (size_t)(o - dest->base);

      return;
    }

    slen = (size_t)(q - p);
    memcpy(o, p, slen);
    o += slen;

    p = q + 1;

    pctdecode(o++, &p);

    len -= slen + 3;
  }
}
