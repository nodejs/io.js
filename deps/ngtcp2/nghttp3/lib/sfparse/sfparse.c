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

#define SF_STATE_DICT 0x08u
#define SF_STATE_LIST 0x10u
#define SF_STATE_ITEM 0x18u

#define SF_STATE_INNER_LIST 0x04u

#define SF_STATE_BEFORE 0x00u
#define SF_STATE_BEFORE_PARAMS 0x01u
#define SF_STATE_PARAMS 0x02u
#define SF_STATE_AFTER 0x03u

#define SF_STATE_OP_MASK 0x03u

#define SF_SET_STATE_AFTER(NAME) (SF_STATE_##NAME | SF_STATE_AFTER)
#define SF_SET_STATE_BEFORE_PARAMS(NAME)                                       \
  (SF_STATE_##NAME | SF_STATE_BEFORE_PARAMS)
#define SF_SET_STATE_INNER_LIST_BEFORE(NAME)                                   \
  (SF_STATE_##NAME | SF_STATE_INNER_LIST | SF_STATE_BEFORE)

#define SF_STATE_DICT_AFTER SF_SET_STATE_AFTER(DICT)
#define SF_STATE_DICT_BEFORE_PARAMS SF_SET_STATE_BEFORE_PARAMS(DICT)
#define SF_STATE_DICT_INNER_LIST_BEFORE SF_SET_STATE_INNER_LIST_BEFORE(DICT)

#define SF_STATE_LIST_AFTER SF_SET_STATE_AFTER(LIST)
#define SF_STATE_LIST_BEFORE_PARAMS SF_SET_STATE_BEFORE_PARAMS(LIST)
#define SF_STATE_LIST_INNER_LIST_BEFORE SF_SET_STATE_INNER_LIST_BEFORE(LIST)

#define SF_STATE_ITEM_AFTER SF_SET_STATE_AFTER(ITEM)
#define SF_STATE_ITEM_BEFORE_PARAMS SF_SET_STATE_BEFORE_PARAMS(ITEM)
#define SF_STATE_ITEM_INNER_LIST_BEFORE SF_SET_STATE_INNER_LIST_BEFORE(ITEM)

#define SF_STATE_INITIAL 0x00u

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

static int parser_eof(sf_parser *sfp) { return sfp->pos == sfp->end; }

static void parser_discard_ows(sf_parser *sfp) {
  for (; !parser_eof(sfp) && is_ws(*sfp->pos); ++sfp->pos)
    ;
}

static void parser_discard_sp(sf_parser *sfp) {
  for (; !parser_eof(sfp) && *sfp->pos == ' '; ++sfp->pos)
    ;
}

static void parser_set_op_state(sf_parser *sfp, uint32_t op) {
  sfp->state &= ~SF_STATE_OP_MASK;
  sfp->state |= op;
}

static void parser_unset_inner_list_state(sf_parser *sfp) {
  sfp->state &= ~SF_STATE_INNER_LIST;
}

static int parser_key(sf_parser *sfp, sf_vec *dest) {
  const uint8_t *base;

  switch (*sfp->pos) {
  case '*':
  LCALPHA_CASES:
    break;
  default:
    return SF_ERR_PARSE_ERROR;
  }

  base = sfp->pos++;

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

  if (dest) {
    dest->base = (uint8_t *)base;
    dest->len = (size_t)(sfp->pos - dest->base);
  }

  return 0;
}

static int parser_number(sf_parser *sfp, sf_value *dest) {
  int sign = 1;
  int64_t value = 0;
  size_t len = 0;
  size_t fpos = 0;

  if (*sfp->pos == '-') {
    ++sfp->pos;
    if (parser_eof(sfp)) {
      return SF_ERR_PARSE_ERROR;
    }

    sign = -1;
  }

  assert(!parser_eof(sfp));

  for (; !parser_eof(sfp); ++sfp->pos) {
    switch (*sfp->pos) {
    DIGIT_CASES:
      if (++len > 15) {
        return SF_ERR_PARSE_ERROR;
      }

      value *= 10;
      value += *sfp->pos - '0';

      continue;
    }

    break;
  }

  if (len == 0) {
    return SF_ERR_PARSE_ERROR;
  }

  if (parser_eof(sfp) || *sfp->pos != '.') {
    if (dest) {
      dest->type = SF_TYPE_INTEGER;
      dest->flags = SF_VALUE_FLAG_NONE;
      dest->integer = value * sign;
    }

    return 0;
  }

  /* decimal */

  if (len > 12) {
    return SF_ERR_PARSE_ERROR;
  }

  fpos = len;

  ++sfp->pos;

  for (; !parser_eof(sfp); ++sfp->pos) {
    switch (*sfp->pos) {
    DIGIT_CASES:
      if (++len > 15) {
        return SF_ERR_PARSE_ERROR;
      }

      value *= 10;
      value += *sfp->pos - '0';

      continue;
    }

    break;
  }

  if (fpos == len || len - fpos > 3) {
    return SF_ERR_PARSE_ERROR;
  }

  if (dest) {
    dest->type = SF_TYPE_DECIMAL;
    dest->flags = SF_VALUE_FLAG_NONE;
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

static int parser_date(sf_parser *sfp, sf_value *dest) {
  int rv;
  sf_value val;

  /* The first byte has already been validated by the caller. */
  assert('@' == *sfp->pos);

  ++sfp->pos;

  if (parser_eof(sfp)) {
    return SF_ERR_PARSE_ERROR;
  }

  rv = parser_number(sfp, &val);
  if (rv != 0) {
    return rv;
  }

  if (val.type != SF_TYPE_INTEGER) {
    return SF_ERR_PARSE_ERROR;
  }

  if (dest) {
    *dest = val;
    dest->type = SF_TYPE_DATE;
  }

  return 0;
}

static int parser_string(sf_parser *sfp, sf_value *dest) {
  const uint8_t *base;
  uint32_t flags = SF_VALUE_FLAG_NONE;

  /* The first byte has already been validated by the caller. */
  assert('"' == *sfp->pos);

  base = ++sfp->pos;

  for (; !parser_eof(sfp); ++sfp->pos) {
    switch (*sfp->pos) {
    X20_21_CASES:
    X23_5B_CASES:
    X5D_7E_CASES:
      break;
    case '\\':
      ++sfp->pos;
      if (parser_eof(sfp)) {
        return SF_ERR_PARSE_ERROR;
      }

      switch (*sfp->pos) {
      case '"':
      case '\\':
        flags = SF_VALUE_FLAG_ESCAPED_STRING;

        break;
      default:
        return SF_ERR_PARSE_ERROR;
      }

      break;
    case '"':
      if (dest) {
        dest->type = SF_TYPE_STRING;
        dest->flags = flags;
        dest->vec.len = (size_t)(sfp->pos - base);
        dest->vec.base = dest->vec.len == 0 ? NULL : (uint8_t *)base;
      }

      ++sfp->pos;

      return 0;
    default:
      return SF_ERR_PARSE_ERROR;
    }
  }

  return SF_ERR_PARSE_ERROR;
}

static int parser_token(sf_parser *sfp, sf_value *dest) {
  const uint8_t *base;

  /* The first byte has already been validated by the caller. */
  base = sfp->pos++;

  for (; !parser_eof(sfp); ++sfp->pos) {
    switch (*sfp->pos) {
    TOKEN_CASES:
      continue;
    }

    break;
  }

  if (dest) {
    dest->type = SF_TYPE_TOKEN;
    dest->flags = SF_VALUE_FLAG_NONE;
    dest->vec.base = (uint8_t *)base;
    dest->vec.len = (size_t)(sfp->pos - base);
  }

  return 0;
}

static int parser_byteseq(sf_parser *sfp, sf_value *dest) {
  const uint8_t *base;

  /* The first byte has already been validated by the caller. */
  assert(':' == *sfp->pos);

  base = ++sfp->pos;

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
        return SF_ERR_PARSE_ERROR;
      case 2:
        ++sfp->pos;

        if (parser_eof(sfp)) {
          return SF_ERR_PARSE_ERROR;
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
        return SF_ERR_PARSE_ERROR;
      }

      goto fin;
    case ':':
      if (((sfp->pos - base) & 0x3) == 1) {
        return SF_ERR_PARSE_ERROR;
      }

      goto fin;
    default:
      return SF_ERR_PARSE_ERROR;
    }
  }

  return SF_ERR_PARSE_ERROR;

fin:
  if (dest) {
    dest->type = SF_TYPE_BYTESEQ;
    dest->flags = SF_VALUE_FLAG_NONE;
    dest->vec.len = (size_t)(sfp->pos - base);
    dest->vec.base = dest->vec.len == 0 ? NULL : (uint8_t *)base;
  }

  ++sfp->pos;

  return 0;
}

static int parser_boolean(sf_parser *sfp, sf_value *dest) {
  int b;

  /* The first byte has already been validated by the caller. */
  assert('?' == *sfp->pos);

  ++sfp->pos;

  if (parser_eof(sfp)) {
    return SF_ERR_PARSE_ERROR;
  }

  switch (*sfp->pos) {
  case '0':
    b = 0;

    break;
  case '1':
    b = 1;

    break;
  default:
    return SF_ERR_PARSE_ERROR;
  }

  ++sfp->pos;

  if (dest) {
    dest->type = SF_TYPE_BOOLEAN;
    dest->flags = SF_VALUE_FLAG_NONE;
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

static int parser_dispstring(sf_parser *sfp, sf_value *dest) {
  const uint8_t *base;
  uint8_t c;
  uint32_t utf8state = UTF8_ACCEPT;

  assert('%' == *sfp->pos);

  ++sfp->pos;

  if (parser_eof(sfp) || *sfp->pos != '"') {
    return SF_ERR_PARSE_ERROR;
  }

  base = ++sfp->pos;

  for (; !parser_eof(sfp);) {
    switch (*sfp->pos) {
    X00_1F_CASES:
    X7F_FF_CASES:
      return SF_ERR_PARSE_ERROR;
    case '%':
      ++sfp->pos;

      if (sfp->pos + 2 > sfp->end) {
        return SF_ERR_PARSE_ERROR;
      }

      if (pctdecode(&c, &sfp->pos) != 0) {
        return SF_ERR_PARSE_ERROR;
      }

      utf8_decode(&utf8state, c);
      if (utf8state == UTF8_REJECT) {
        return SF_ERR_PARSE_ERROR;
      }

      break;
    case '"':
      if (utf8state != UTF8_ACCEPT) {
        return SF_ERR_PARSE_ERROR;
      }

      if (dest) {
        dest->type = SF_TYPE_DISPSTRING;
        dest->flags = SF_VALUE_FLAG_NONE;
        dest->vec.len = (size_t)(sfp->pos - base);
        dest->vec.base = dest->vec.len == 0 ? NULL : (uint8_t *)base;
      }

      ++sfp->pos;

      return 0;
    default:
      if (utf8state != UTF8_ACCEPT) {
        return SF_ERR_PARSE_ERROR;
      }

      ++sfp->pos;
    }
  }

  return SF_ERR_PARSE_ERROR;
}

static int parser_bare_item(sf_parser *sfp, sf_value *dest) {
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
    return SF_ERR_PARSE_ERROR;
  }
}

static int parser_skip_inner_list(sf_parser *sfp);

int sf_parser_param(sf_parser *sfp, sf_vec *dest_key, sf_value *dest_value) {
  int rv;

  switch (sfp->state & SF_STATE_OP_MASK) {
  case SF_STATE_BEFORE:
    rv = parser_skip_inner_list(sfp);
    if (rv != 0) {
      return rv;
    }

    /* fall through */
  case SF_STATE_BEFORE_PARAMS:
    parser_set_op_state(sfp, SF_STATE_PARAMS);

    break;
  case SF_STATE_PARAMS:
    break;
  default:
    assert(0);
    abort();
  }

  if (parser_eof(sfp) || *sfp->pos != ';') {
    parser_set_op_state(sfp, SF_STATE_AFTER);

    return SF_ERR_EOF;
  }

  ++sfp->pos;

  parser_discard_sp(sfp);
  if (parser_eof(sfp)) {
    return SF_ERR_PARSE_ERROR;
  }

  rv = parser_key(sfp, dest_key);
  if (rv != 0) {
    return rv;
  }

  if (parser_eof(sfp) || *sfp->pos != '=') {
    if (dest_value) {
      dest_value->type = SF_TYPE_BOOLEAN;
      dest_value->flags = SF_VALUE_FLAG_NONE;
      dest_value->boolean = 1;
    }

    return 0;
  }

  ++sfp->pos;

  if (parser_eof(sfp)) {
    return SF_ERR_PARSE_ERROR;
  }

  return parser_bare_item(sfp, dest_value);
}

static int parser_skip_params(sf_parser *sfp) {
  int rv;

  for (;;) {
    rv = sf_parser_param(sfp, NULL, NULL);
    switch (rv) {
    case 0:
      break;
    case SF_ERR_EOF:
      return 0;
    case SF_ERR_PARSE_ERROR:
      return rv;
    default:
      assert(0);
      abort();
    }
  }
}

int sf_parser_inner_list(sf_parser *sfp, sf_value *dest) {
  int rv;

  switch (sfp->state & SF_STATE_OP_MASK) {
  case SF_STATE_BEFORE:
    parser_discard_sp(sfp);
    if (parser_eof(sfp)) {
      return SF_ERR_PARSE_ERROR;
    }

    break;
  case SF_STATE_BEFORE_PARAMS:
    rv = parser_skip_params(sfp);
    if (rv != 0) {
      return rv;
    }

    /* Technically, we are entering SF_STATE_AFTER, but we will set
       another state without reading the state. */
    /* parser_set_op_state(sfp, SF_STATE_AFTER); */

    /* fall through */
  case SF_STATE_AFTER:
    if (parser_eof(sfp)) {
      return SF_ERR_PARSE_ERROR;
    }

    switch (*sfp->pos) {
    case ' ':
      parser_discard_sp(sfp);
      if (parser_eof(sfp)) {
        return SF_ERR_PARSE_ERROR;
      }

      break;
    case ')':
      break;
    default:
      return SF_ERR_PARSE_ERROR;
    }

    break;
  default:
    assert(0);
    abort();
  }

  if (*sfp->pos == ')') {
    ++sfp->pos;

    parser_unset_inner_list_state(sfp);
    parser_set_op_state(sfp, SF_STATE_BEFORE_PARAMS);

    return SF_ERR_EOF;
  }

  rv = parser_bare_item(sfp, dest);
  if (rv != 0) {
    return rv;
  }

  parser_set_op_state(sfp, SF_STATE_BEFORE_PARAMS);

  return 0;
}

static int parser_skip_inner_list(sf_parser *sfp) {
  int rv;

  for (;;) {
    rv = sf_parser_inner_list(sfp, NULL);
    switch (rv) {
    case 0:
      break;
    case SF_ERR_EOF:
      return 0;
    case SF_ERR_PARSE_ERROR:
      return rv;
    default:
      assert(0);
      abort();
    }
  }
}

static int parser_next_key_or_item(sf_parser *sfp) {
  parser_discard_ows(sfp);

  if (parser_eof(sfp)) {
    return SF_ERR_EOF;
  }

  if (*sfp->pos != ',') {
    return SF_ERR_PARSE_ERROR;
  }

  ++sfp->pos;

  parser_discard_ows(sfp);
  if (parser_eof(sfp)) {
    return SF_ERR_PARSE_ERROR;
  }

  return 0;
}

static int parser_dict_value(sf_parser *sfp, sf_value *dest) {
  int rv;

  if (parser_eof(sfp) || *(sfp->pos) != '=') {
    /* Boolean true */
    if (dest) {
      dest->type = SF_TYPE_BOOLEAN;
      dest->flags = SF_VALUE_FLAG_NONE;
      dest->boolean = 1;
    }

    sfp->state = SF_STATE_DICT_BEFORE_PARAMS;

    return 0;
  }

  ++sfp->pos;

  if (parser_eof(sfp)) {
    return SF_ERR_PARSE_ERROR;
  }

  if (*sfp->pos == '(') {
    if (dest) {
      dest->type = SF_TYPE_INNER_LIST;
      dest->flags = SF_VALUE_FLAG_NONE;
    }

    ++sfp->pos;

    sfp->state = SF_STATE_DICT_INNER_LIST_BEFORE;

    return 0;
  }

  rv = parser_bare_item(sfp, dest);
  if (rv != 0) {
    return rv;
  }

  sfp->state = SF_STATE_DICT_BEFORE_PARAMS;

  return 0;
}

int sf_parser_dict(sf_parser *sfp, sf_vec *dest_key, sf_value *dest_value) {
  int rv;

  switch (sfp->state) {
  case SF_STATE_DICT_INNER_LIST_BEFORE:
    rv = parser_skip_inner_list(sfp);
    if (rv != 0) {
      return rv;
    }

    /* fall through */
  case SF_STATE_DICT_BEFORE_PARAMS:
    rv = parser_skip_params(sfp);
    if (rv != 0) {
      return rv;
    }

    /* fall through */
  case SF_STATE_DICT_AFTER:
    rv = parser_next_key_or_item(sfp);
    if (rv != 0) {
      return rv;
    }

    break;
  case SF_STATE_INITIAL:
    parser_discard_sp(sfp);

    if (parser_eof(sfp)) {
      return SF_ERR_EOF;
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

int sf_parser_list(sf_parser *sfp, sf_value *dest) {
  int rv;

  switch (sfp->state) {
  case SF_STATE_LIST_INNER_LIST_BEFORE:
    rv = parser_skip_inner_list(sfp);
    if (rv != 0) {
      return rv;
    }

    /* fall through */
  case SF_STATE_LIST_BEFORE_PARAMS:
    rv = parser_skip_params(sfp);
    if (rv != 0) {
      return rv;
    }

    /* fall through */
  case SF_STATE_LIST_AFTER:
    rv = parser_next_key_or_item(sfp);
    if (rv != 0) {
      return rv;
    }

    break;
  case SF_STATE_INITIAL:
    parser_discard_sp(sfp);

    if (parser_eof(sfp)) {
      return SF_ERR_EOF;
    }

    break;
  default:
    assert(0);
    abort();
  }

  if (*sfp->pos == '(') {
    if (dest) {
      dest->type = SF_TYPE_INNER_LIST;
      dest->flags = SF_VALUE_FLAG_NONE;
    }

    ++sfp->pos;

    sfp->state = SF_STATE_LIST_INNER_LIST_BEFORE;

    return 0;
  }

  rv = parser_bare_item(sfp, dest);
  if (rv != 0) {
    return rv;
  }

  sfp->state = SF_STATE_LIST_BEFORE_PARAMS;

  return 0;
}

int sf_parser_item(sf_parser *sfp, sf_value *dest) {
  int rv;

  switch (sfp->state) {
  case SF_STATE_INITIAL:
    parser_discard_sp(sfp);

    if (parser_eof(sfp)) {
      return SF_ERR_PARSE_ERROR;
    }

    break;
  case SF_STATE_ITEM_INNER_LIST_BEFORE:
    rv = parser_skip_inner_list(sfp);
    if (rv != 0) {
      return rv;
    }

    /* fall through */
  case SF_STATE_ITEM_BEFORE_PARAMS:
    rv = parser_skip_params(sfp);
    if (rv != 0) {
      return rv;
    }

    /* fall through */
  case SF_STATE_ITEM_AFTER:
    parser_discard_sp(sfp);

    if (!parser_eof(sfp)) {
      return SF_ERR_PARSE_ERROR;
    }

    return SF_ERR_EOF;
  default:
    assert(0);
    abort();
  }

  if (*sfp->pos == '(') {
    if (dest) {
      dest->type = SF_TYPE_INNER_LIST;
      dest->flags = SF_VALUE_FLAG_NONE;
    }

    ++sfp->pos;

    sfp->state = SF_STATE_ITEM_INNER_LIST_BEFORE;

    return 0;
  }

  rv = parser_bare_item(sfp, dest);
  if (rv != 0) {
    return rv;
  }

  sfp->state = SF_STATE_ITEM_BEFORE_PARAMS;

  return 0;
}

void sf_parser_init(sf_parser *sfp, const uint8_t *data, size_t datalen) {
  if (datalen == 0) {
    sfp->pos = sfp->end = NULL;
  } else {
    sfp->pos = data;
    sfp->end = data + datalen;
  }

  sfp->state = SF_STATE_INITIAL;
}

void sf_unescape(sf_vec *dest, const sf_vec *src) {
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

void sf_base64decode(sf_vec *dest, const sf_vec *src) {
  static const int index_tbl[] = {
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, 52, 53, 54, 55, 56, 57,
      58, 59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,
      7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
      25, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
      37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1};
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

void sf_pctdecode(sf_vec *dest, const sf_vec *src) {
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
