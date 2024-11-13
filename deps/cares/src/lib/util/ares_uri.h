/* MIT License
 *
 * Copyright (c) 2024 Brad House
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
#ifndef __ARES_URI_H
#define __ARES_URI_H

/*! \addtogroup ares_uri URI parser and writer implementation
 *
 * This is a fairly complete URI parser and writer implementation (RFC 3986) for
 * schemes which use the :// syntax. Does not currently support URIs without an
 * authority section, such as "mailto:person@example.com".
 *
 * Its implementation is overkill for our current needs to be able to express
 * DNS server configuration, but there was really no reason not to support
 * a greater subset of the specification.
 *
 * @{
 */


struct ares_uri;

/*! URI object */
typedef struct ares_uri ares_uri_t;

/*! Create a new URI object
 *
 *  \return new ares_uri_t, must be freed with ares_uri_destroy()
 */
ares_uri_t             *ares_uri_create(void);

/*! Destroy an initialized URI object
 *
 *  \param[in] uri  Initialized URI object
 */
void                    ares_uri_destroy(ares_uri_t *uri);

/*! Set the URI scheme.  Automatically lower-cases the scheme provided.
 *  Only allows Alpha, Digit, +, -, and . characters.  Maximum length is
 *  15 characters.  This is required to be set to write a URI.
 *
 *  \param[in] uri    Initialized URI object
 *  \param[in] scheme Scheme to set the object to use
 *  \return ARES_SUCCESS on success
 */
ares_status_t  ares_uri_set_scheme(ares_uri_t *uri, const char *scheme);

/*! Retrieve the currently configured URI scheme.
 *
 *  \param[in] uri    Initialized URI object
 *  \return string containing URI scheme
 */
const char    *ares_uri_get_scheme(const ares_uri_t *uri);

/*! Set the username in the URI object
 *
 *  \param[in] uri      Initialized URI object
 *  \param[in] username Username to set. May be NULL to unset existing username.
 *  \return ARES_SUCCESS on success
 */
ares_status_t  ares_uri_set_username(ares_uri_t *uri, const char *username);

/*! Retrieve the currently configured username.
 *
 *  \param[in] uri    Initialized URI object
 *  \return string containing username, maybe NULL if not set.
 */
const char    *ares_uri_get_username(const ares_uri_t *uri);

/*! Set the password in the URI object
 *
 *  \param[in] uri      Initialized URI object
 *  \param[in] password Password to set. May be NULL to unset existing password.
 *  \return ARES_SUCCESS on success
 */
ares_status_t  ares_uri_set_password(ares_uri_t *uri, const char *password);

/*! Retrieve the currently configured password.
 *
 *  \param[in] uri    Initialized URI object
 *  \return string containing password, maybe NULL if not set.
 */
const char    *ares_uri_get_password(const ares_uri_t *uri);

/*! Set the host or ip address in the URI object.  This is required to be
 *  set to write a URI.  The character set is strictly validated.
 *
 *  \param[in] uri      Initialized URI object
 *  \param[in] host     IPv4, IPv6, or hostname to set.
 *  \return ARES_SUCCESS on success
 */
ares_status_t  ares_uri_set_host(ares_uri_t *uri, const char *host);

/*! Retrieve the currently configured host (or ip address).  IPv6 addresses
 *  May include a link-local scope (e.g. fe80::b542:84df:1719:65e3%en0).
 *
 *  \param[in] uri    Initialized URI object
 *  \return string containing host, maybe NULL if not set.
 */
const char    *ares_uri_get_host(const ares_uri_t *uri);

/*! Set the port to use in the URI object.  A port value of 0 will omit
 *  the port from the URI when written, thus using the scheme's default.
 *
 *  \param[in] uri  Initialized URI object
 *  \param[in] port Port to set. Use 0 to unset.
 *  \return ARES_SUCCESS on success
 */
ares_status_t  ares_uri_set_port(ares_uri_t *uri, unsigned short port);

/*! Retrieve the currently configured port
 *
 *  \param[in] uri    Initialized URI object
 *  \return port number, or 0 if not set.
 */
unsigned short ares_uri_get_port(const ares_uri_t *uri);

/*! Set the path in the URI object.  Unsupported characters will be URI-encoded
 *  when written.
 *
 *  \param[in] uri  Initialized URI object
 *  \param[in] path Path to set. May be NULL to unset existing path.
 *  \return ARES_SUCCESS on success
 */
ares_status_t  ares_uri_set_path(ares_uri_t *uri, const char *path);

/*! Retrieves the path in the URI object.  If retrieved after parse, this
 *  value will be URI-decoded already.
 *
 *  \param[in] uri Initialized URI object
 *  \return path string, or NULL if not set.
 */
const char    *ares_uri_get_path(const ares_uri_t *uri);

/*! Set a new query key/value pair.  There is no set order for query keys
 *  when output in the URI, they will be emitted in a random order.  Keys are
 *  case-insensitive. Query keys and values will be automatically URI-encoded
 *  when written.
 *
 *  \param[in] uri  Initialized URI object
 *  \param[in] key  Query key to use, must be non-zero length.
 *  \param[in] val  Query value to use, may be NULL.
 *  \return ARES_SUCCESS on success
 */
ares_status_t  ares_uri_set_query_key(ares_uri_t *uri, const char *key,
                                      const char *val);

/*! Delete a specific query key.
 *
 *  \param[in] uri Initialized URI object
 *  \param[in] key Key to delete.
 *  \return ARES_SUCCESS if deleted, ARES_ENOTFOUND if not found
 */
ares_status_t  ares_uri_del_query_key(ares_uri_t *uri, const char *key);

/*! Retrieve the value associted with a query key. Keys are case-insensitive.
 *
 *  \param[in] uri Initialized URI object
 *  \param[in] key Key to retrieve.
 *  \return string representing value, may be NULL if either not found or
 *          NULL value set.  There is currently no way to indicate the
 *          difference.
 */
const char    *ares_uri_get_query_key(const ares_uri_t *uri, const char *key);

/*! Retrieve a complete list of query keys.
 *
 *  \param[in]  uri Initialized URI object
 *  \param[out] num Number of keys.
 *  \return NULL on failure or no keys. Use
 *          ares_free_array(keys, num, ares_free) when done with array.
 */
char         **ares_uri_get_query_keys(const ares_uri_t *uri, size_t *num);

/*! Set the fragment in the URI object.  Unsupported characters will be
 *  URI-encoded when written.
 *
 *  \param[in] uri      Initialized URI object
 *  \param[in] fragment Fragment to set. May be NULL to unset existing fragment.
 *  \return ARES_SUCCESS on success
 */
ares_status_t  ares_uri_set_fragment(ares_uri_t *uri, const char *fragment);

/*! Retrieves the fragment in the URI object.  If retrieved after parse, this
 *  value will be URI-decoded already.
 *
 *  \param[in] uri Initialized URI object
 *  \return fragment string, or NULL if not set.
 */
const char    *ares_uri_get_fragment(const ares_uri_t *uri);

/*! Parse the provided URI buffer into a new URI object.
 *
 *  \param[out] out  Returned new URI object. free with ares_uri_destroy().
 *  \param[in]  buf  Buffer object containing the URI
 *  \return ARES_SUCCESS on successful parse. On failure the 'buf' object will
 *          be restored to its initial state in case another parser needs to
 *          be attempted.
 */
ares_status_t  ares_uri_parse_buf(ares_uri_t **out, ares_buf_t *buf);

/*! Parse the provided URI string into a new URI object.
 *
 *  \param[out] out  Returned new URI object. free with ares_uri_destroy().
 *  \param[in]  uri  URI string to parse
 *  \return ARES_SUCCESS on successful parse
 */
ares_status_t  ares_uri_parse(ares_uri_t **out, const char *uri);

/*! Write URI object to a new string buffer.  Requires at least the scheme
 *  and host to be set for this to succeed.
 *
 *  \param[out] out  Returned new URI string. Free with ares_free().
 *  \param[in]  uri  Initialized URI object.
 *  \return ARES_SUCCESS on successful write.
 */
ares_status_t  ares_uri_write(char **out, const ares_uri_t *uri);

/*! Write URI object to an existing ares_buf_t object.  Requires at least the
 *  scheme and host to be set for this to succeed.
 *
 *  \param[in]     uri  Initialized URI object.
 *  \param[in,out] buf  Destination buf object.
 *  \return ARES_SUCCESS on successful write.
 */
ares_status_t  ares_uri_write_buf(const ares_uri_t *uri, ares_buf_t *buf);

/*! @} */

#endif /* __ARES_URI_H */
