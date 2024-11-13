/* MIT License
 *
 * Copyright (c) 2023 Brad House
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

typedef struct {
  char  *name;
  size_t name_len;
  size_t idx;
} ares_nameoffset_t;

static void ares_nameoffset_free(void *arg)
{
  ares_nameoffset_t *off = arg;
  if (off == NULL) {
    return; /* LCOV_EXCL_LINE: DefensiveCoding */
  }
  ares_free(off->name);
  ares_free(off);
}

static ares_status_t ares_nameoffset_create(ares_llist_t **list,
                                            const char *name, size_t idx)
{
  ares_status_t      status;
  ares_nameoffset_t *off = NULL;

  if (list == NULL || name == NULL || ares_strlen(name) == 0 ||
      ares_strlen(name) > 255) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  if (*list == NULL) {
    *list = ares_llist_create(ares_nameoffset_free);
  }
  if (*list == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto fail;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  off = ares_malloc_zero(sizeof(*off));
  if (off == NULL) {
    return ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
  }

  off->name     = ares_strdup(name);
  off->name_len = ares_strlen(off->name);
  off->idx      = idx;

  if (ares_llist_insert_last(*list, off) == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto fail;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  return ARES_SUCCESS;

/* LCOV_EXCL_START: OutOfMemory */
fail:
  ares_nameoffset_free(off);
  return status;
  /* LCOV_EXCL_STOP */
}

static const ares_nameoffset_t *ares_nameoffset_find(ares_llist_t *list,
                                                     const char   *name)
{
  size_t                   name_len = ares_strlen(name);
  ares_llist_node_t       *node;
  const ares_nameoffset_t *longest_match = NULL;

  if (list == NULL || name == NULL || name_len == 0) {
    return NULL;
  }

  for (node = ares_llist_node_first(list); node != NULL;
       node = ares_llist_node_next(node)) {
    const ares_nameoffset_t *val = ares_llist_node_val(node);
    size_t                   prefix_len;

    /* Can't be a match if the stored name is longer */
    if (val->name_len > name_len) {
      continue;
    }

    /* Can't be the longest match if our existing longest match is longer */
    if (longest_match != NULL && longest_match->name_len > val->name_len) {
      continue;
    }

    prefix_len = name_len - val->name_len;

    /* Due to DNS 0x20, lets not inadvertently mangle things, use case-sensitive
     * matching instead of case-insensitive.  This may result in slightly
     * larger DNS queries overall. */
    if (!ares_streq(val->name, name + prefix_len)) {
      continue;
    }

    /* We need to make sure if `val->name` is "example.com" that name is
     * is separated by a label, e.g. "myexample.com" is not ok, however
     * "my.example.com" is, so we look for the preceding "." */
    if (prefix_len != 0 && name[prefix_len - 1] != '.') {
      continue;
    }

    longest_match = val;
  }

  return longest_match;
}

static void ares_dns_labels_free_cb(void *arg)
{
  ares_buf_t **buf = arg;
  if (buf == NULL) {
    return;
  }

  ares_buf_destroy(*buf);
}

static ares_buf_t *ares_dns_labels_add(ares_array_t *labels)
{
  ares_buf_t **buf;

  if (labels == NULL) {
    return NULL; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  if (ares_array_insert_last((void **)&buf, labels) != ARES_SUCCESS) {
    return NULL;
  }

  *buf = ares_buf_create();
  if (*buf == NULL) {
    ares_array_remove_last(labels);
    return NULL;
  }

  return *buf;
}

static ares_buf_t *ares_dns_labels_get_last(ares_array_t *labels)
{
  ares_buf_t **buf = ares_array_last(labels);

  if (buf == NULL) {
    return NULL;
  }

  return *buf;
}

static ares_buf_t *ares_dns_labels_get_at(ares_array_t *labels, size_t idx)
{
  ares_buf_t **buf = ares_array_at(labels, idx);

  if (buf == NULL) {
    return NULL;
  }

  return *buf;
}

static void ares_dns_name_labels_del_last(ares_array_t *labels)
{
  ares_array_remove_last(labels);
}

static ares_status_t ares_parse_dns_name_escape(ares_buf_t *namebuf,
                                                ares_buf_t *label,
                                                ares_bool_t validate_hostname)
{
  ares_status_t status;
  unsigned char c;

  status = ares_buf_fetch_bytes(namebuf, &c, 1);
  if (status != ARES_SUCCESS) {
    return ARES_EBADNAME;
  }

  /* If next character is a digit, read 2 more digits */
  if (ares_isdigit(c)) {
    size_t       i;
    unsigned int val = 0;

    val = c - '0';

    for (i = 0; i < 2; i++) {
      status = ares_buf_fetch_bytes(namebuf, &c, 1);
      if (status != ARES_SUCCESS) {
        return ARES_EBADNAME;
      }

      if (!ares_isdigit(c)) {
        return ARES_EBADNAME;
      }
      val *= 10;
      val += c - '0';
    }

    /* Out of range */
    if (val > 255) {
      return ARES_EBADNAME;
    }

    if (validate_hostname && !ares_is_hostnamech((unsigned char)val)) {
      return ARES_EBADNAME;
    }

    return ares_buf_append_byte(label, (unsigned char)val);
  }

  /* We can just output the character */
  if (validate_hostname && !ares_is_hostnamech(c)) {
    return ARES_EBADNAME;
  }

  return ares_buf_append_byte(label, c);
}

static ares_status_t ares_split_dns_name(ares_array_t *labels,
                                         ares_bool_t   validate_hostname,
                                         const char   *name)
{
  ares_status_t status;
  ares_buf_t   *label   = NULL;
  ares_buf_t   *namebuf = NULL;
  size_t        i;
  size_t        total_len = 0;
  unsigned char c;

  if (name == NULL || labels == NULL) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  /* Put name into a buffer for parsing */
  namebuf = ares_buf_create();
  if (namebuf == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  if (*name != '\0') {
    status =
      ares_buf_append(namebuf, (const unsigned char *)name, ares_strlen(name));
    if (status != ARES_SUCCESS) {
      goto done; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  /* Start with 1 label */
  label = ares_dns_labels_add(labels);
  if (label == NULL) {
    status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
    goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
  }

  while (ares_buf_fetch_bytes(namebuf, &c, 1) == ARES_SUCCESS) {
    /* New label */
    if (c == '.') {
      label = ares_dns_labels_add(labels);
      if (label == NULL) {
        status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
        goto done;            /* LCOV_EXCL_LINE: OutOfMemory */
      }
      continue;
    }

    /* Escape */
    if (c == '\\') {
      status = ares_parse_dns_name_escape(namebuf, label, validate_hostname);
      if (status != ARES_SUCCESS) {
        goto done;
      }
      continue;
    }

    /* Output direct character */
    if (validate_hostname && !ares_is_hostnamech(c)) {
      status = ARES_EBADNAME;
      goto done;
    }

    status = ares_buf_append_byte(label, c);
    if (status != ARES_SUCCESS) {
      goto done; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  /* Remove trailing blank label */
  if (ares_buf_len(ares_dns_labels_get_last(labels)) == 0) {
    ares_dns_name_labels_del_last(labels);
  }

  /* If someone passed in "." there could have been 2 blank labels, check for
   * that */
  if (ares_array_len(labels) == 1 &&
      ares_buf_len(ares_dns_labels_get_last(labels)) == 0) {
    ares_dns_name_labels_del_last(labels);
  }

  /* Scan to make sure label lengths are valid */
  for (i = 0; i < ares_array_len(labels); i++) {
    const ares_buf_t *buf = ares_dns_labels_get_at(labels, i);
    size_t            len = ares_buf_len(buf);
    /* No 0-length labels, and no labels over 63 bytes */
    if (len == 0 || len > 63) {
      status = ARES_EBADNAME;
      goto done;
    }
    total_len += len;
  }

  /* Can't exceed maximum (unescaped) length */
  if (ares_array_len(labels) && total_len + ares_array_len(labels) - 1 > 255) {
    status = ARES_EBADNAME;
    goto done;
  }

  status = ARES_SUCCESS;

done:
  ares_buf_destroy(namebuf);
  return status;
}

ares_status_t ares_dns_name_write(ares_buf_t *buf, ares_llist_t **list,
                                  ares_bool_t validate_hostname,
                                  const char *name)
{
  const ares_nameoffset_t *off = NULL;
  size_t                   name_len;
  size_t                   orig_name_len;
  size_t                   pos    = ares_buf_len(buf);
  ares_array_t            *labels = NULL;
  char                     name_copy[512];
  ares_status_t            status;

  if (buf == NULL || name == NULL) {
    return ARES_EFORMERR; /* LCOV_EXCL_LINE: DefensiveCoding */
  }

  labels = ares_array_create(sizeof(ares_buf_t *), ares_dns_labels_free_cb);
  if (labels == NULL) {
    return ARES_ENOMEM;
  }

  /* NOTE: due to possible escaping, name_copy buffer is > 256 to allow for
   *       this */
  name_len      = ares_strcpy(name_copy, name, sizeof(name_copy));
  orig_name_len = name_len;

  /* Find longest match */
  if (list != NULL) {
    off = ares_nameoffset_find(*list, name_copy);
    if (off != NULL && off->name_len != name_len) {
      /* truncate */
      name_len            -= (off->name_len + 1);
      name_copy[name_len]  = 0;
    }
  }

  /* Output labels */
  if (off == NULL || off->name_len != orig_name_len) {
    size_t i;

    status = ares_split_dns_name(labels, validate_hostname, name_copy);
    if (status != ARES_SUCCESS) {
      goto done;
    }

    for (i = 0; i < ares_array_len(labels); i++) {
      size_t               len  = 0;
      const ares_buf_t    *lbuf = ares_dns_labels_get_at(labels, i);
      const unsigned char *ptr  = ares_buf_peek(lbuf, &len);

      status = ares_buf_append_byte(buf, (unsigned char)(len & 0xFF));
      if (status != ARES_SUCCESS) {
        goto done; /* LCOV_EXCL_LINE: OutOfMemory */
      }

      status = ares_buf_append(buf, ptr, len);
      if (status != ARES_SUCCESS) {
        goto done; /* LCOV_EXCL_LINE: OutOfMemory */
      }
    }

    /* If we are NOT jumping to another label, output terminator */
    if (off == NULL) {
      status = ares_buf_append_byte(buf, 0);
      if (status != ARES_SUCCESS) {
        goto done; /* LCOV_EXCL_LINE: OutOfMemory */
      }
    }
  }

  /* Output name compression offset jump */
  if (off != NULL) {
    unsigned short u16 =
      (unsigned short)0xC000 | (unsigned short)(off->idx & 0x3FFF);
    status = ares_buf_append_be16(buf, u16);
    if (status != ARES_SUCCESS) {
      goto done; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  /* Store pointer for future jumps as long as its not an exact match for
   * a prior entry */
  if (list != NULL && (off == NULL || off->name_len != orig_name_len) &&
      name_len > 0) {
    status = ares_nameoffset_create(list, name /* not truncated copy! */, pos);
    if (status != ARES_SUCCESS) {
      goto done; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  status = ARES_SUCCESS;

done:
  ares_array_destroy(labels);
  return status;
}

/* Reserved characters for names that need to be escaped */
static ares_bool_t is_reservedch(int ch)
{
  switch (ch) {
    case '"':
    case '.':
    case ';':
    case '\\':
    case '(':
    case ')':
    case '@':
    case '$':
      return ARES_TRUE;
    default:
      break;
  }

  return ARES_FALSE;
}

static ares_status_t ares_fetch_dnsname_into_buf(ares_buf_t *buf,
                                                 ares_buf_t *dest, size_t len,
                                                 ares_bool_t is_hostname)
{
  size_t               remaining_len;
  const unsigned char *ptr = ares_buf_peek(buf, &remaining_len);
  ares_status_t        status;
  size_t               i;

  if (buf == NULL || len == 0 || remaining_len < len) {
    return ARES_EBADRESP;
  }

  for (i = 0; i < len; i++) {
    unsigned char c = ptr[i];

    /* Hostnames have a very specific allowed character set.  Anything outside
     * of that (non-printable and reserved included) are disallowed */
    if (is_hostname && !ares_is_hostnamech(c)) {
      status = ARES_EBADRESP;
      goto fail;
    }

    /* NOTE: dest may be NULL if the user is trying to skip the name. validation
     *       still occurs above. */
    if (dest == NULL) {
      continue;
    }

    /* Non-printable characters need to be output as \DDD */
    if (!ares_isprint(c)) {
      unsigned char escape[4];

      escape[0] = '\\';
      escape[1] = '0' + (c / 100);
      escape[2] = '0' + ((c % 100) / 10);
      escape[3] = '0' + (c % 10);

      status = ares_buf_append(dest, escape, sizeof(escape));
      if (status != ARES_SUCCESS) {
        goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
      }

      continue;
    }

    /* Reserved characters need to be escaped, otherwise normal */
    if (is_reservedch(c)) {
      status = ares_buf_append_byte(dest, '\\');
      if (status != ARES_SUCCESS) {
        goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
      }
    }

    status = ares_buf_append_byte(dest, c);
    if (status != ARES_SUCCESS) {
      return status; /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  return ares_buf_consume(buf, len);

fail:
  return status;
}

ares_status_t ares_dns_name_parse(ares_buf_t *buf, char **name,
                                  ares_bool_t is_hostname)
{
  size_t        save_offset = 0;
  unsigned char c;
  ares_status_t status;
  ares_buf_t   *namebuf     = NULL;
  size_t        label_start = ares_buf_get_position(buf);

  if (buf == NULL) {
    return ARES_EFORMERR;
  }

  if (name != NULL) {
    namebuf = ares_buf_create();
    if (namebuf == NULL) {
      status = ARES_ENOMEM;
      goto fail;
    }
  }

  /* The compression scheme allows a domain name in a message to be
   * represented as either:
   *
   * - a sequence of labels ending in a zero octet
   * - a pointer
   * - a sequence of labels ending with a pointer
   */
  while (1) {
    /* Keep track of the minimum label starting position to prevent forward
     * jumping */
    if (label_start > ares_buf_get_position(buf)) {
      label_start = ares_buf_get_position(buf);
    }

    status = ares_buf_fetch_bytes(buf, &c, 1);
    if (status != ARES_SUCCESS) {
      goto fail;
    }

    /* Pointer/Redirect */
    if ((c & 0xc0) == 0xc0) {
      /* The pointer takes the form of a two octet sequence:
       *
       *   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
       *   | 1  1|                OFFSET                   |
       *   +--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+--+
       *
       * The first two bits are ones.  This allows a pointer to be distinguished
       * from a label, since the label must begin with two zero bits because
       * labels are restricted to 63 octets or less.  (The 10 and 01
       * combinations are reserved for future use.)  The OFFSET field specifies
       * an offset from the start of the message (i.e., the first octet of the
       * ID field in the domain header).  A zero offset specifies the first byte
       * of the ID field, etc.
       */
      size_t offset = (size_t)((c & 0x3F) << 8);

      /* Fetch second byte of the redirect length */
      status = ares_buf_fetch_bytes(buf, &c, 1);
      if (status != ARES_SUCCESS) {
        goto fail;
      }

      offset |= ((size_t)c);

      /* According to RFC 1035 4.1.4:
       *    In this scheme, an entire domain name or a list of labels at
       *    the end of a domain name is replaced with a pointer to a prior
       *    occurrence of the same name.
       * Note the word "prior", meaning it must go backwards.  This was
       * confirmed via the ISC BIND code that it also prevents forward
       * pointers.
       */
      if (offset >= label_start) {
        status = ARES_EBADNAME;
        goto fail;
      }

      /* First time we make a jump, save the current position */
      if (save_offset == 0) {
        save_offset = ares_buf_get_position(buf);
      }

      status = ares_buf_set_position(buf, offset);
      if (status != ARES_SUCCESS) {
        status = ARES_EBADNAME;
        goto fail;
      }

      continue;
    } else if ((c & 0xc0) != 0) {
      /* 10 and 01 are reserved */
      status = ARES_EBADNAME;
      goto fail;
    } else if (c == 0) {
      /* termination via zero octet*/
      break;
    }

    /* New label */

    /* Labels are separated by periods */
    if (ares_buf_len(namebuf) != 0 && name != NULL) {
      status = ares_buf_append_byte(namebuf, '.');
      if (status != ARES_SUCCESS) {
        goto fail; /* LCOV_EXCL_LINE: OutOfMemory */
      }
    }

    status = ares_fetch_dnsname_into_buf(buf, namebuf, c, is_hostname);
    if (status != ARES_SUCCESS) {
      goto fail;
    }
  }

  /* Restore offset read after first redirect/pointer as this is where the DNS
   * message continues */
  if (save_offset) {
    ares_buf_set_position(buf, save_offset);
  }

  if (name != NULL) {
    *name = ares_buf_finish_str(namebuf, NULL);
    if (*name == NULL) {
      status = ARES_ENOMEM; /* LCOV_EXCL_LINE: OutOfMemory */
      goto fail;            /* LCOV_EXCL_LINE: OutOfMemory */
    }
  }

  return ARES_SUCCESS;

fail:
  /* We want badname response if we couldn't parse */
  if (status == ARES_EBADRESP) {
    status = ARES_EBADNAME;
  }

  ares_buf_destroy(namebuf);
  return status;
}
