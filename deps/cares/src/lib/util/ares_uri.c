/* MIT License
 *
 * Copyright (c) 2024 Brad house
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
#include "ares_uri.h"
#ifdef HAVE_STDINT_H
#  include <stdint.h>
#endif

struct ares_uri {
  char                scheme[16];
  char               *username;
  char               *password;
  unsigned short      port;
  char                host[256];
  char               *path;
  ares_htable_dict_t *query;
  char               *fragment;
};

/* RFC3986 character set notes:
 *    gen-delims  = ":" / "/" / "?" / "#" / "[" / "]" / "@"
 *    sub-delims  = "!" / "$" / "&" / "'" / "(" / ")"
 *                / "*" / "+" / "," / ";" / "="
 *    reserved    = gen-delims / sub-delims
 *    unreserved  = ALPHA / DIGIT / "-" / "." / "_" / "~"
 *    scheme      = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
 *    authority   = [ userinfo "@" ] host [ ":" port ]
 *    userinfo    = *( unreserved / pct-encoded / sub-delims / ":" )
 *    NOTE: Use of the format "user:password" in the userinfo field is
 *          deprecated.  Applications should not render as clear text any data
 *          after the first colon (":") character found within a userinfo
 *          subcomponent unless the data after the colon is the empty string
 *           (indicating no password).
 *    pchar         = unreserved / pct-encoded / sub-delims / ":" / "@"
 *    query       = *( pchar / "/" / "?" )
 *    fragment    = *( pchar / "/" / "?" )
 *
 *   NOTE: Due to ambiguity, "+" in a query must be percent-encoded, as old
 *         URLs used that for spaces.
 */


static ares_bool_t ares_uri_chis_subdelim(char x)
{
  switch (x) {
    case '!':
      return ARES_TRUE;
    case '$':
      return ARES_TRUE;
    case '&':
      return ARES_TRUE;
    case '\'':
      return ARES_TRUE;
    case '(':
      return ARES_TRUE;
    case ')':
      return ARES_TRUE;
    case '*':
      return ARES_TRUE;
    case '+':
      return ARES_TRUE;
    case ',':
      return ARES_TRUE;
    case ';':
      return ARES_TRUE;
    case '=':
      return ARES_TRUE;
    default:
      break;
  }
  return ARES_FALSE;
}

/* These don't actually appear to be referenced in any logic */
#if 0
static ares_bool_t ares_uri_chis_gendelim(char x)
{
  switch (x) {
    case ':':
      return ARES_TRUE;
    case '/':
      return ARES_TRUE;
    case '?':
      return ARES_TRUE;
    case '#':
      return ARES_TRUE;
    case '[':
      return ARES_TRUE;
    case ']':
      return ARES_TRUE;
    case '@':
      return ARES_TRUE;
    default:
      break;
  }
  return ARES_FALSE;
}


static ares_bool_t ares_uri_chis_reserved(char x)
{
  return ares_uri_chis_gendelim(x) || ares_uri_chis_subdelim(x);
}
#endif

static ares_bool_t ares_uri_chis_unreserved(char x)
{
  switch (x) {
    case '-':
      return ARES_TRUE;
    case '.':
      return ARES_TRUE;
    case '_':
      return ARES_TRUE;
    case '~':
      return ARES_TRUE;
    default:
      break;
  }
  return ares_isalpha(x) || ares_isdigit(x);
}

static ares_bool_t ares_uri_chis_scheme(char x)
{
  switch (x) {
    case '+':
      return ARES_TRUE;
    case '-':
      return ARES_TRUE;
    case '.':
      return ARES_TRUE;
    default:
      break;
  }
  return ares_isalpha(x) || ares_isdigit(x);
}

static ares_bool_t ares_uri_chis_authority(char x)
{
  /* This one here isn't well defined.  We are going to include the valid
   * characters of the subfields plus known delimiters */
  return ares_uri_chis_unreserved(x) || ares_uri_chis_subdelim(x) || x == '%' ||
         x == '[' || x == ']' || x == '@' || x == ':';
}

static ares_bool_t ares_uri_chis_userinfo(char x)
{
  /* NOTE: we don't include ':' here since we are using that as our
   *       username/password delimiter */
  return ares_uri_chis_unreserved(x) || ares_uri_chis_subdelim(x);
}

static ares_bool_t ares_uri_chis_path(char x)
{
  switch (x) {
    case ':':
      return ARES_TRUE;
    case '@':
      return ARES_TRUE;
    /* '/' isn't in the spec as a path character since its technically a
     * delimiter but we're not splitting on '/' so we accept it as valid */
    case '/':
      return ARES_TRUE;
    default:
      break;
  }
  return ares_uri_chis_unreserved(x) || ares_uri_chis_subdelim(x);
}

static ares_bool_t ares_uri_chis_path_enc(char x)
{
  return ares_uri_chis_path(x) || x == '%';
}

static ares_bool_t ares_uri_chis_query(char x)
{
  switch (x) {
    case '/':
      return ARES_TRUE;
    case '?':
      return ARES_TRUE;
    default:
      break;
  }

  /* Exclude & and = used as delimiters, they're valid characters in the
   * set, just not for the individual pieces */
  return ares_uri_chis_path(x) && x != '&' && x != '=';
}

static ares_bool_t ares_uri_chis_query_enc(char x)
{
  return ares_uri_chis_query(x) || x == '%';
}

static ares_bool_t ares_uri_chis_fragment(char x)
{
  switch (x) {
    case '/':
      return ARES_TRUE;
    case '?':
      return ARES_TRUE;
    default:
      break;
  }
  return ares_uri_chis_path(x);
}

static ares_bool_t ares_uri_chis_fragment_enc(char x)
{
  return ares_uri_chis_fragment(x) || x == '%';
}

ares_uri_t *ares_uri_create(void)
{
  ares_uri_t *uri = ares_malloc_zero(sizeof(*uri));

  if (uri == NULL) {
    return NULL;
  }

  uri->query = ares_htable_dict_create();
  if (uri->query == NULL) {
    ares_free(uri);
    return NULL;
  }

  return uri;
}

void ares_uri_destroy(ares_uri_t *uri)
{
  if (uri == NULL) {
    return;
  }

  ares_free(uri->username);
  ares_free(uri->password);
  ares_free(uri->path);
  ares_free(uri->fragment);
  ares_htable_dict_destroy(uri->query);
  ares_free(uri);
}

static ares_bool_t ares_uri_scheme_is_valid(const char *uri)
{
  size_t i;

  if (ares_strlen(uri) == 0) {
    return ARES_FALSE;
  }

  if (!ares_isalpha(*uri)) {
    return ARES_FALSE;
  }

  for (i = 0; uri[i] != 0; i++) {
    if (!ares_uri_chis_scheme(uri[i])) {
      return ARES_FALSE;
    }
  }
  return ARES_TRUE;
}

static ares_bool_t ares_uri_str_isvalid(const char *str, size_t max_len,
                                        ares_bool_t (*ischr)(char))
{
  size_t i;

  if (str == NULL) {
    return ARES_FALSE;
  }

  for (i = 0; i != max_len && str[i] != 0; i++) {
    if (!ischr(str[i])) {
      return ARES_FALSE;
    }
  }
  return ARES_TRUE;
}

ares_status_t ares_uri_set_scheme(ares_uri_t *uri, const char *scheme)
{
  if (uri == NULL) {
    return ARES_EFORMERR;
  }

  if (!ares_uri_scheme_is_valid(scheme)) {
    return ARES_EBADSTR;
  }

  ares_strcpy(uri->scheme, scheme, sizeof(uri->scheme));
  ares_str_lower(uri->scheme);

  return ARES_SUCCESS;
}

const char *ares_uri_get_scheme(const ares_uri_t *uri)
{
  if (uri == NULL) {
    return NULL;
  }

  return uri->scheme;
}

static ares_status_t ares_uri_set_username_own(ares_uri_t *uri, char *username)
{
  if (uri == NULL) {
    return ARES_EFORMERR;
  }

  if (username != NULL && (!ares_str_isprint(username, ares_strlen(username)) ||
                           ares_strlen(username) == 0)) {
    return ARES_EBADSTR;
  }


  ares_free(uri->username);
  uri->username = username;
  return ARES_SUCCESS;
}

ares_status_t ares_uri_set_username(ares_uri_t *uri, const char *username)
{
  ares_status_t status;
  char         *temp = NULL;

  if (uri == NULL) {
    return ARES_EFORMERR;
  }

  if (username != NULL) {
    temp = ares_strdup(username);
    if (temp == NULL) {
      return ARES_ENOMEM;
    }
  }

  status = ares_uri_set_username_own(uri, temp);
  if (status != ARES_SUCCESS) {
    ares_free(temp);
  }

  return status;
}

const char *ares_uri_get_username(const ares_uri_t *uri)
{
  if (uri == NULL) {
    return NULL;
  }

  return uri->username;
}

static ares_status_t ares_uri_set_password_own(ares_uri_t *uri, char *password)
{
  if (uri == NULL) {
    return ARES_EFORMERR;
  }

  if (password != NULL && !ares_str_isprint(password, ares_strlen(password))) {
    return ARES_EBADSTR;
  }

  ares_free(uri->password);
  uri->password = password;
  return ARES_SUCCESS;
}

ares_status_t ares_uri_set_password(ares_uri_t *uri, const char *password)
{
  ares_status_t status;
  char         *temp = NULL;

  if (uri == NULL) {
    return ARES_EFORMERR;
  }

  if (password != NULL) {
    temp = ares_strdup(password);
    if (temp == NULL) {
      return ARES_ENOMEM;
    }
  }

  status = ares_uri_set_password_own(uri, temp);
  if (status != ARES_SUCCESS) {
    ares_free(temp);
  }

  return status;
}

const char *ares_uri_get_password(const ares_uri_t *uri)
{
  if (uri == NULL) {
    return NULL;
  }

  return uri->password;
}

ares_status_t ares_uri_set_host(ares_uri_t *uri, const char *host)
{
  struct ares_addr addr;
  size_t           addrlen;
  char             hoststr[256];
  char            *ll_scope;

  if (uri == NULL || ares_strlen(host) == 0 ||
      ares_strlen(host) >= sizeof(hoststr)) {
    return ARES_EFORMERR;
  }

  ares_strcpy(hoststr, host, sizeof(hoststr));

  /* Look for '%' which could be a link-local scope for ipv6 addresses and
   * parse it off */
  ll_scope = strchr(hoststr, '%');
  if (ll_scope != NULL) {
    *ll_scope = 0;
    ll_scope++;
    if (!ares_str_isalnum(ll_scope)) {
      return ARES_EBADNAME;
    }
  }

  /* If its an IP address, normalize it */
  memset(&addr, 0, sizeof(addr));
  addr.family = AF_UNSPEC;
  if (ares_dns_pton(hoststr, &addr, &addrlen) != NULL) {
    char ipaddr[INET6_ADDRSTRLEN];
    ares_inet_ntop(addr.family, &addr.addr, ipaddr, sizeof(ipaddr));
    /* Only IPv6 is allowed to have a scope */
    if (ll_scope != NULL && addr.family != AF_INET6) {
      return ARES_EBADNAME;
    }

    if (ll_scope != NULL) {
      snprintf(uri->host, sizeof(uri->host), "%s%%%s", ipaddr, ll_scope);
    } else {
      ares_strcpy(uri->host, ipaddr, sizeof(uri->host));
    }
    return ARES_SUCCESS;
  }

  /* If its a hostname, make sure its a valid charset */
  if (!ares_is_hostname(host)) {
    return ARES_EBADNAME;
  }

  ares_strcpy(uri->host, host, sizeof(uri->host));
  return ARES_SUCCESS;
}

const char *ares_uri_get_host(const ares_uri_t *uri)
{
  if (uri == NULL) {
    return NULL;
  }

  return uri->host;
}

ares_status_t ares_uri_set_port(ares_uri_t *uri, unsigned short port)
{
  if (uri == NULL) {
    return ARES_EFORMERR;
  }
  uri->port = port;
  return ARES_SUCCESS;
}

unsigned short ares_uri_get_port(const ares_uri_t *uri)
{
  if (uri == NULL) {
    return 0;
  }
  return uri->port;
}

/* URI spec says path normalization is a requirement */
static char *ares_uri_path_normalize(const char *path)
{
  ares_status_t status;
  ares_array_t *arr     = NULL;
  ares_buf_t   *outpath = NULL;
  ares_buf_t   *inpath  = NULL;
  ares_ssize_t  i;
  size_t        j;
  size_t        len;

  inpath =
    ares_buf_create_const((const unsigned char *)path, ares_strlen(path));
  if (inpath == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  outpath = ares_buf_create();
  if (outpath == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  status = ares_buf_split_str_array(inpath, (const unsigned char *)"/", 1,
                                    ARES_BUF_SPLIT_TRIM, 0, &arr);
  if (status != ARES_SUCCESS) {
    return NULL;
  }

  for (i = 0; i < (ares_ssize_t)ares_array_len(arr); i++) {
    const char **strptr = ares_array_at(arr, (size_t)i);
    const char  *str    = *strptr;

    if (ares_streq(str, ".")) {
      ares_array_remove_at(arr, (size_t)i);
      i--;
    } else if (ares_streq(str, "..")) {
      if (i != 0) {
        ares_array_remove_at(arr, (size_t)i - 1);
        i--;
      }
      ares_array_remove_at(arr, (size_t)i);
      i--;
    }
  }

  status = ares_buf_append_byte(outpath, '/');
  if (status != ARES_SUCCESS) {
    goto done;
  }

  len = ares_array_len(arr);
  for (j = 0; j < len; j++) {
    const char **strptr = ares_array_at(arr, j);
    const char  *str    = *strptr;
    status              = ares_buf_append_str(outpath, str);
    if (status != ARES_SUCCESS) {
      goto done;
    }

    /* Path separator, but on the last entry, we need to check if it was
     * originally terminated or not because they have different meanings */
    if (j != len - 1 || path[ares_strlen(path) - 1] == '/') {
      status = ares_buf_append_byte(outpath, '/');
      if (status != ARES_SUCCESS) {
        goto done;
      }
    }
  }

done:
  ares_array_destroy(arr);
  ares_buf_destroy(inpath);
  if (status != ARES_SUCCESS) {
    ares_buf_destroy(outpath);
    return NULL;
  }

  return ares_buf_finish_str(outpath, NULL);
}

ares_status_t ares_uri_set_path(ares_uri_t *uri, const char *path)
{
  char *temp = NULL;

  if (uri == NULL) {
    return ARES_EFORMERR;
  }

  if (path != NULL && !ares_str_isprint(path, ares_strlen(path))) {
    return ARES_EBADSTR;
  }

  if (path != NULL) {
    temp = ares_uri_path_normalize(path);
    if (temp == NULL) {
      return ARES_ENOMEM;
    }
  }

  ares_free(uri->path);
  uri->path = temp;

  return ARES_SUCCESS;
}

const char *ares_uri_get_path(const ares_uri_t *uri)
{
  if (uri == NULL) {
    return NULL;
  }

  return uri->path;
}

ares_status_t ares_uri_set_query_key(ares_uri_t *uri, const char *key,
                                     const char *val)
{
  if (uri == NULL || key == NULL || *key == 0) {
    return ARES_EFORMERR;
  }

  if (!ares_str_isprint(key, ares_strlen(key)) ||
      (val != NULL && !ares_str_isprint(val, ares_strlen(val)))) {
    return ARES_EBADSTR;
  }

  if (!ares_htable_dict_insert(uri->query, key, val)) {
    return ARES_ENOMEM;
  }
  return ARES_SUCCESS;
}

ares_status_t ares_uri_del_query_key(ares_uri_t *uri, const char *key)
{
  if (uri == NULL || key == NULL || *key == 0 ||
      !ares_str_isprint(key, ares_strlen(key))) {
    return ARES_EFORMERR;
  }

  if (!ares_htable_dict_remove(uri->query, key)) {
    return ARES_ENOTFOUND;
  }

  return ARES_SUCCESS;
}

const char *ares_uri_get_query_key(const ares_uri_t *uri, const char *key)
{
  if (uri == NULL || key == NULL || *key == 0 ||
      !ares_str_isprint(key, ares_strlen(key))) {
    return NULL;
  }

  return ares_htable_dict_get_direct(uri->query, key);
}

char **ares_uri_get_query_keys(const ares_uri_t *uri, size_t *num)
{
  if (uri == NULL || num == NULL) {
    return NULL;
  }

  return ares_htable_dict_keys(uri->query, num);
}

static ares_status_t ares_uri_set_fragment_own(ares_uri_t *uri, char *fragment)
{
  if (uri == NULL) {
    return ARES_EFORMERR;
  }

  if (fragment != NULL && !ares_str_isprint(fragment, ares_strlen(fragment))) {
    return ARES_EBADSTR;
  }

  ares_free(uri->fragment);
  uri->fragment = fragment;
  return ARES_SUCCESS;
}

ares_status_t ares_uri_set_fragment(ares_uri_t *uri, const char *fragment)
{
  ares_status_t status;
  char         *temp = NULL;

  if (uri == NULL) {
    return ARES_EFORMERR;
  }

  if (fragment != NULL) {
    temp = ares_strdup(fragment);
    if (temp == NULL) {
      return ARES_ENOMEM;
    }
  }

  status = ares_uri_set_fragment_own(uri, temp);
  if (status != ARES_SUCCESS) {
    ares_free(temp);
  }

  return status;
}

const char *ares_uri_get_fragment(const ares_uri_t *uri)
{
  if (uri == NULL) {
    return NULL;
  }
  return uri->fragment;
}

static ares_status_t ares_uri_encode_buf(ares_buf_t *buf, const char *str,
                                         ares_bool_t (*ischr)(char))
{
  size_t i;

  if (buf == NULL || str == NULL) {
    return ARES_EFORMERR;
  }

  for (i = 0; str[i] != 0; i++) {
    if (ischr(str[i])) {
      if (ares_buf_append_byte(buf, (unsigned char)str[i]) != ARES_SUCCESS) {
        return ARES_ENOMEM;
      }
    } else {
      if (ares_buf_append_byte(buf, '%') != ARES_SUCCESS) {
        return ARES_ENOMEM;
      }
      if (ares_buf_append_num_hex(buf, (size_t)str[i], 2) != ARES_SUCCESS) {
        return ARES_ENOMEM;
      }
    }
  }
  return ARES_SUCCESS;
}

static ares_status_t ares_uri_write_scheme(const ares_uri_t *uri,
                                           ares_buf_t       *buf)
{
  ares_status_t status;

  status = ares_buf_append_str(buf, uri->scheme);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_buf_append_str(buf, "://");

  return status;
}

static ares_status_t ares_uri_write_authority(const ares_uri_t *uri,
                                              ares_buf_t       *buf)
{
  ares_status_t status;
  ares_bool_t   is_ipv6 = ARES_FALSE;

  if (ares_strlen(uri->username)) {
    status = ares_uri_encode_buf(buf, uri->username, ares_uri_chis_userinfo);
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  if (ares_strlen(uri->password)) {
    status = ares_buf_append_byte(buf, ':');
    if (status != ARES_SUCCESS) {
      return status;
    }

    status = ares_uri_encode_buf(buf, uri->password, ares_uri_chis_userinfo);
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  if (ares_strlen(uri->username) || ares_strlen(uri->password)) {
    status = ares_buf_append_byte(buf, '@');
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  /* We need to write ipv6 addresses with [ ] */
  if (strchr(uri->host, '%') != NULL) {
    /* If we have a % in the name, it must be ipv6 link local scope, so we
     * don't need to check anything else */
    is_ipv6 = ARES_TRUE;
  } else {
    /* Parse the host to see if it is an ipv6 address */
    struct ares_addr addr;
    size_t           addrlen;
    memset(&addr, 0, sizeof(addr));
    addr.family = AF_INET6;
    if (ares_dns_pton(uri->host, &addr, &addrlen) != NULL) {
      is_ipv6 = ARES_TRUE;
    }
  }

  if (is_ipv6) {
    status = ares_buf_append_byte(buf, '[');
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  status = ares_buf_append_str(buf, uri->host);
  if (status != ARES_SUCCESS) {
    return status;
  }

  if (is_ipv6) {
    status = ares_buf_append_byte(buf, ']');
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  if (uri->port > 0) {
    status = ares_buf_append_byte(buf, ':');
    if (status != ARES_SUCCESS) {
      return status;
    }
    status = ares_buf_append_num_dec(buf, uri->port, 0);
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  return status;
}

static ares_status_t ares_uri_write_path(const ares_uri_t *uri, ares_buf_t *buf)
{
  ares_status_t status;

  if (ares_strlen(uri->path) == 0) {
    return ARES_SUCCESS;
  }

  if (*uri->path != '/') {
    status = ares_buf_append_byte(buf, '/');
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  status = ares_uri_encode_buf(buf, uri->path, ares_uri_chis_path);
  if (status != ARES_SUCCESS) {
    return status;
  }

  return ARES_SUCCESS;
}

static ares_status_t ares_uri_write_query(const ares_uri_t *uri,
                                          ares_buf_t       *buf)
{
  ares_status_t status;
  char        **keys;
  size_t        num_keys = 0;
  size_t        i;

  if (ares_htable_dict_num_keys(uri->query) == 0) {
    return ARES_SUCCESS;
  }

  keys = ares_uri_get_query_keys(uri, &num_keys);
  if (keys == NULL || num_keys == 0) {
    return ARES_ENOMEM;
  }

  status = ares_buf_append_byte(buf, '?');
  if (status != ARES_SUCCESS) {
    goto done;
  }

  for (i = 0; i < num_keys; i++) {
    const char *val;

    if (i != 0) {
      status = ares_buf_append_byte(buf, '&');
      if (status != ARES_SUCCESS) {
        goto done;
      }
    }

    status = ares_uri_encode_buf(buf, keys[i], ares_uri_chis_query);
    if (status != ARES_SUCCESS) {
      goto done;
    }

    val = ares_uri_get_query_key(uri, keys[i]);
    if (val != NULL) {
      status = ares_buf_append_byte(buf, '=');
      if (status != ARES_SUCCESS) {
        goto done;
      }

      status = ares_uri_encode_buf(buf, val, ares_uri_chis_query);
      if (status != ARES_SUCCESS) {
        goto done;
      }
    }
  }

done:
  ares_free_array(keys, num_keys, ares_free);
  return status;
}

static ares_status_t ares_uri_write_fragment(const ares_uri_t *uri,
                                             ares_buf_t       *buf)
{
  ares_status_t status;

  if (!ares_strlen(uri->fragment)) {
    return ARES_SUCCESS;
  }

  status = ares_buf_append_byte(buf, '#');
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_uri_encode_buf(buf, uri->fragment, ares_uri_chis_fragment);
  if (status != ARES_SUCCESS) {
    return status;
  }

  return ARES_SUCCESS;
}

ares_status_t ares_uri_write_buf(const ares_uri_t *uri, ares_buf_t *buf)
{
  ares_status_t status;
  size_t        orig_len;

  if (uri == NULL || buf == NULL) {
    return ARES_EFORMERR;
  }

  if (ares_strlen(uri->scheme) == 0 || ares_strlen(uri->host) == 0) {
    return ARES_ENODATA;
  }

  orig_len = ares_buf_len(buf);

  status = ares_uri_write_scheme(uri, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_uri_write_authority(uri, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_uri_write_path(uri, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_uri_write_query(uri, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_uri_write_fragment(uri, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

done:
  if (status != ARES_SUCCESS) {
    ares_buf_set_length(buf, orig_len);
  }
  return status;
}

ares_status_t ares_uri_write(char **out, const ares_uri_t *uri)
{
  ares_buf_t   *buf;
  ares_status_t status;

  if (out == NULL || uri == NULL) {
    return ARES_EFORMERR;
  }

  *out = NULL;

  buf = ares_buf_create();
  if (buf == NULL) {
    return ARES_ENOMEM;
  }

  status = ares_uri_write_buf(uri, buf);
  if (status != ARES_SUCCESS) {
    ares_buf_destroy(buf);
    return status;
  }

  *out = ares_buf_finish_str(buf, NULL);
  return ARES_SUCCESS;
}

#define xdigit_val(x)     \
  ((x >= '0' && x <= '9') \
     ? (x - '0')          \
     : ((x >= 'A' && x <= 'F') ? (x - 'A' + 10) : (x - 'a' + 10)))

static ares_status_t ares_uri_decode_inplace(char *str, ares_bool_t is_query,
                                             ares_bool_t must_be_printable,
                                             size_t     *out_len)
{
  size_t i;
  size_t len = 0;

  for (i = 0; str[i] != 0; i++) {
    if (is_query && str[i] == '+') {
      str[len++] = ' ';
      continue;
    }

    if (str[i] != '%') {
      str[len++] = str[i];
      continue;
    }

    if (!ares_isxdigit(str[i + 1]) || !ares_isxdigit(str[i + 2])) {
      return ARES_EBADSTR;
    }

    str[len] = (char)(xdigit_val(str[i + 1]) << 4 | xdigit_val(str[i + 2]));

    if (must_be_printable && !ares_isprint(str[len])) {
      return ARES_EBADSTR;
    }

    len++;

    i += 2;
  }

  str[len] = 0;

  *out_len = len;
  return ARES_SUCCESS;
}

static ares_status_t ares_uri_parse_scheme(ares_uri_t *uri, ares_buf_t *buf)
{
  ares_status_t status;
  size_t        bytes;
  char          scheme[sizeof(uri->scheme)];

  ares_buf_tag(buf);

  bytes =
    ares_buf_consume_until_seq(buf, (const unsigned char *)"://", 3, ARES_TRUE);
  if (bytes == SIZE_MAX || bytes > sizeof(uri->scheme)) {
    return ARES_EBADSTR;
  }

  status = ares_buf_tag_fetch_string(buf, scheme, sizeof(scheme));
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_uri_set_scheme(uri, scheme);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Consume :// */
  ares_buf_consume(buf, 3);

  return ARES_SUCCESS;
}

static ares_status_t ares_uri_parse_userinfo(ares_uri_t *uri, ares_buf_t *buf)
{
  size_t        userinfo_len;
  size_t        username_len;
  ares_bool_t   has_password = ARES_FALSE;
  char         *temp         = NULL;
  ares_status_t status;
  size_t        len;

  ares_buf_tag(buf);

  /* Search for @, if its not found, return */
  userinfo_len = ares_buf_consume_until_charset(buf, (const unsigned char *)"@",
                                                1, ARES_TRUE);

  if (userinfo_len == SIZE_MAX) {
    return ARES_SUCCESS;
  }

  /* Rollback since now we know there really is userinfo */
  ares_buf_tag_rollback(buf);

  /* Search for ':', if it isn't found or its past the '@' then we only have
   * a username and no password */
  ares_buf_tag(buf);
  username_len = ares_buf_consume_until_charset(buf, (const unsigned char *)":",
                                                1, ARES_TRUE);
  if (username_len < userinfo_len) {
    has_password = ARES_TRUE;
    status       = ares_buf_tag_fetch_strdup(buf, &temp);
    if (status != ARES_SUCCESS) {
      goto done;
    }

    status = ares_uri_decode_inplace(temp, ARES_FALSE, ARES_TRUE, &len);
    if (status != ARES_SUCCESS) {
      goto done;
    }

    status = ares_uri_set_username_own(uri, temp);
    if (status != ARES_SUCCESS) {
      goto done;
    }
    temp = NULL;

    /* Consume : */
    ares_buf_consume(buf, 1);
  }

  ares_buf_tag(buf);
  ares_buf_consume_until_charset(buf, (const unsigned char *)"@", 1, ARES_TRUE);
  status = ares_buf_tag_fetch_strdup(buf, &temp);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_uri_decode_inplace(temp, ARES_FALSE, ARES_TRUE, &len);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  if (has_password) {
    status = ares_uri_set_password_own(uri, temp);
  } else {
    status = ares_uri_set_username_own(uri, temp);
  }
  if (status != ARES_SUCCESS) {
    goto done;
  }
  temp = NULL;

  /* Consume @ */
  ares_buf_consume(buf, 1);

done:
  ares_free(temp);
  return status;
}

static ares_status_t ares_uri_parse_hostport(ares_uri_t *uri, ares_buf_t *buf)
{
  unsigned char b;
  char          host[256];
  char          port[6];
  size_t        len;
  ares_status_t status;

  status = ares_buf_peek_byte(buf, &b);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Bracketed syntax for ipv6 addresses */
  if (b == '[') {
    ares_buf_consume(buf, 1);
    ares_buf_tag(buf);
    len = ares_buf_consume_until_charset(buf, (const unsigned char *)"]", 1,
                                         ARES_TRUE);
    if (len == SIZE_MAX) {
      return ARES_EBADSTR;
    }

    status = ares_buf_tag_fetch_string(buf, host, sizeof(host));
    if (status != ARES_SUCCESS) {
      return status;
    }
    /* Consume ']' */
    ares_buf_consume(buf, 1);
  } else {
    /* Either ipv4 or hostname */
    ares_buf_tag(buf);
    ares_buf_consume_until_charset(buf, (const unsigned char *)":", 1,
                                   ARES_FALSE);

    status = ares_buf_tag_fetch_string(buf, host, sizeof(host));
    if (status != ARES_SUCCESS) {
      return status;
    }
  }

  status = ares_uri_set_host(uri, host);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* No port if nothing left to consume */
  if (!ares_buf_len(buf)) {
    return status;
  }

  status = ares_buf_peek_byte(buf, &b);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Only valid extra character at this point is ':' */
  if (b != ':') {
    return ARES_EBADSTR;
  }
  ares_buf_consume(buf, 1);

  len = ares_buf_len(buf);
  if (len == 0 || len > sizeof(port) - 1) {
    return ARES_EBADSTR;
  }

  status = ares_buf_fetch_bytes(buf, (unsigned char *)port, len);
  if (status != ARES_SUCCESS) {
    return status;
  }
  port[len] = 0;

  if (!ares_str_isnum(port)) {
    return ARES_EBADSTR;
  }

  status = ares_uri_set_port(uri, (unsigned short)atoi(port));
  if (status != ARES_SUCCESS) {
    return status;
  }

  return ARES_SUCCESS;
}

static ares_status_t ares_uri_parse_authority(ares_uri_t *uri, ares_buf_t *buf)
{
  ares_status_t        status;
  size_t               bytes;
  ares_buf_t          *auth = NULL;
  const unsigned char *ptr;
  size_t               ptr_len;

  ares_buf_tag(buf);

  bytes = ares_buf_consume_until_charset(buf, (const unsigned char *)"/?#", 3,
                                         ARES_FALSE);
  if (bytes == 0) {
    return ARES_EBADSTR;
  }

  status = ares_buf_tag_fetch_constbuf(buf, &auth);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  ptr = ares_buf_peek(auth, &ptr_len);
  if (!ares_uri_str_isvalid((const char *)ptr, ptr_len,
                            ares_uri_chis_authority)) {
    status = ARES_EBADSTR;
    goto done;
  }

  status = ares_uri_parse_userinfo(uri, auth);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_uri_parse_hostport(uri, auth);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  /* NOTE: the /, ?, or # is still in the buffer at this point so it can
   *       be used to determine what parser should be called next */

done:
  ares_buf_destroy(auth);
  return status;
}

static ares_status_t ares_uri_parse_path(ares_uri_t *uri, ares_buf_t *buf)
{
  unsigned char b;
  char         *path = NULL;
  ares_status_t status;
  size_t        len;

  if (ares_buf_len(buf) == 0) {
    return ARES_SUCCESS;
  }

  status = ares_buf_peek_byte(buf, &b);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Not a path, must be one of the others */
  if (b != '/') {
    return ARES_SUCCESS;
  }

  ares_buf_tag(buf);
  ares_buf_consume_until_charset(buf, (const unsigned char *)"?#", 2,
                                 ARES_FALSE);
  status = ares_buf_tag_fetch_strdup(buf, &path);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  if (!ares_uri_str_isvalid(path, SIZE_MAX, ares_uri_chis_path_enc)) {
    status = ARES_EBADSTR;
    goto done;
  }

  status = ares_uri_decode_inplace(path, ARES_FALSE, ARES_TRUE, &len);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_uri_set_path(uri, path);
  if (status != ARES_SUCCESS) {
    goto done;
  }

done:
  ares_free(path);
  return status;
}

static ares_status_t ares_uri_parse_query_buf(ares_uri_t *uri, ares_buf_t *buf)
{
  ares_status_t status = ARES_SUCCESS;
  char         *key    = NULL;
  char         *val    = NULL;

  while (ares_buf_len(buf) > 0) {
    unsigned char b = 0;
    size_t        len;

    ares_buf_tag(buf);

    /* Its valid to have only a key with no value, so we search for both
     * delims */
    len = ares_buf_consume_until_charset(buf, (const unsigned char *)"&=", 2,
                                         ARES_FALSE);
    if (len == 0) {
      /* If we're here, we have a zero length key which is invalid */
      status = ARES_EBADSTR;
      goto done;
    }

    if (ares_buf_len(buf) > 0) {
      /* Determine if we stopped on & or = */
      status = ares_buf_peek_byte(buf, &b);
      if (status != ARES_SUCCESS) {
        goto done;
      }
    }

    status = ares_buf_tag_fetch_strdup(buf, &key);
    if (status != ARES_SUCCESS) {
      goto done;
    }

    if (!ares_uri_str_isvalid(key, SIZE_MAX, ares_uri_chis_query_enc)) {
      status = ARES_EBADSTR;
      goto done;
    }

    status = ares_uri_decode_inplace(key, ARES_TRUE, ARES_TRUE, &len);
    if (status != ARES_SUCCESS) {
      goto done;
    }

    /* Fetch Value */
    if (b == '=') {
      /* Skip delimiter */
      ares_buf_consume(buf, 1);
      ares_buf_tag(buf);
      len = ares_buf_consume_until_charset(buf, (const unsigned char *)"&", 1,
                                           ARES_FALSE);
      if (len > 0) {
        status = ares_buf_tag_fetch_strdup(buf, &val);
        if (status != ARES_SUCCESS) {
          goto done;
        }

        if (!ares_uri_str_isvalid(val, SIZE_MAX, ares_uri_chis_query_enc)) {
          status = ARES_EBADSTR;
          goto done;
        }

        status = ares_uri_decode_inplace(val, ARES_TRUE, ARES_TRUE, &len);
        if (status != ARES_SUCCESS) {
          goto done;
        }
      }
    }

    if (b != 0) {
      /* Consume '&' */
      ares_buf_consume(buf, 1);
    }

    status = ares_uri_set_query_key(uri, key, val);
    if (status != ARES_SUCCESS) {
      goto done;
    }

    ares_free(key);
    key = NULL;
    ares_free(val);
    val = NULL;
  }

done:
  ares_free(key);
  ares_free(val);
  return status;
}

static ares_status_t ares_uri_parse_query(ares_uri_t *uri, ares_buf_t *buf)
{
  unsigned char b;
  ares_status_t status;
  ares_buf_t   *query = NULL;
  size_t        len;

  if (ares_buf_len(buf) == 0) {
    return ARES_SUCCESS;
  }

  status = ares_buf_peek_byte(buf, &b);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Not a query, must be one of the others */
  if (b != '?') {
    return ARES_SUCCESS;
  }

  /* Only possible terminator is fragment indicator of '#' */
  ares_buf_consume(buf, 1);
  ares_buf_tag(buf);
  len = ares_buf_consume_until_charset(buf, (const unsigned char *)"#", 1,
                                       ARES_FALSE);
  if (len == 0) {
    /* No data, return */
    return ARES_SUCCESS;
  }

  status = ares_buf_tag_fetch_constbuf(buf, &query);
  if (status != ARES_SUCCESS) {
    return status;
  }

  status = ares_uri_parse_query_buf(uri, query);
  ares_buf_destroy(query);

  return status;
}

static ares_status_t ares_uri_parse_fragment(ares_uri_t *uri, ares_buf_t *buf)
{
  unsigned char b;
  char         *fragment = NULL;
  ares_status_t status;
  size_t        len;

  if (ares_buf_len(buf) == 0) {
    return ARES_SUCCESS;
  }

  status = ares_buf_peek_byte(buf, &b);
  if (status != ARES_SUCCESS) {
    return status;
  }

  /* Not a fragment, must be one of the others */
  if (b != '#') {
    return ARES_SUCCESS;
  }

  ares_buf_consume(buf, 1);

  if (ares_buf_len(buf) == 0) {
    return ARES_SUCCESS;
  }

  /* Rest of the buffer is the fragment */
  status = ares_buf_fetch_str_dup(buf, ares_buf_len(buf), &fragment);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  if (!ares_uri_str_isvalid(fragment, SIZE_MAX, ares_uri_chis_fragment_enc)) {
    status = ARES_EBADSTR;
    goto done;
  }

  status = ares_uri_decode_inplace(fragment, ARES_FALSE, ARES_TRUE, &len);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_uri_set_fragment_own(uri, fragment);
  if (status != ARES_SUCCESS) {
    goto done;
  }
  fragment = NULL;

done:
  ares_free(fragment);
  return status;
}

ares_status_t ares_uri_parse_buf(ares_uri_t **out, ares_buf_t *buf)
{
  ares_status_t status;
  ares_uri_t   *uri = NULL;
  size_t        orig_pos;

  if (out == NULL || buf == NULL) {
    return ARES_EFORMERR;
  }

  *out = NULL;

  orig_pos = ares_buf_get_position(buf);

  uri = ares_uri_create();
  if (uri == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  status = ares_uri_parse_scheme(uri, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_uri_parse_authority(uri, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_uri_parse_path(uri, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_uri_parse_query(uri, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_uri_parse_fragment(uri, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

done:
  if (status != ARES_SUCCESS) {
    ares_buf_set_position(buf, orig_pos);
    ares_uri_destroy(uri);
  } else {
    *out = uri;
  }
  return status;
}

ares_status_t ares_uri_parse(ares_uri_t **out, const char *str)
{
  ares_status_t status;
  ares_buf_t   *buf = NULL;

  if (out == NULL || str == NULL) {
    return ARES_EFORMERR;
  }

  *out = NULL;

  buf = ares_buf_create();
  if (buf == NULL) {
    status = ARES_ENOMEM;
    goto done;
  }

  status = ares_buf_append_str(buf, str);
  if (status != ARES_SUCCESS) {
    goto done;
  }

  status = ares_uri_parse_buf(out, buf);
  if (status != ARES_SUCCESS) {
    goto done;
  }

done:
  ares_buf_destroy(buf);

  return status;
}
