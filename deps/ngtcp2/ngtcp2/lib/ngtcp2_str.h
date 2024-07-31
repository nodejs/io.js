/*
 * ngtcp2
 *
 * Copyright (c) 2017 ngtcp2 contributors
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
#ifndef NGTCP2_STR_H
#define NGTCP2_STR_H

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <ngtcp2/ngtcp2.h>

void *ngtcp2_cpymem(void *dest, const void *src, size_t n);

/*
 * ngtcp2_setmem writes a string of length |n| consisting only |b| to
 * the buffer pointed by |dest|.  It returns dest + n;
 */
uint8_t *ngtcp2_setmem(uint8_t *dest, uint8_t b, size_t n);

/*
 * ngtcp2_get_bytes copies |n| bytes from |src| to |dest|, and returns
 * |src| + |n|.
 */
const void *ngtcp2_get_bytes(void *dest, const void *src, size_t n);

/*
 * ngtcp2_encode_hex encodes |data| of length |len| in hex string.  It
 * writes additional NULL bytes at the end of the buffer.  The buffer
 * pointed by |dest| must have at least |len| * 2 + 1 bytes space.
 * This function returns |dest|.
 */
uint8_t *ngtcp2_encode_hex(uint8_t *dest, const uint8_t *data, size_t len);

/*
 * ngtcp2_encode_ipv4 encodes binary form IPv4 address stored in
 * |addr| to human readable text form in the buffer pointed by |dest|.
 * The capacity of buffer must have enough length to store a text form
 * plus a terminating NULL byte.  The resulting text form ends with
 * NULL byte.  The function returns |dest|.
 */
uint8_t *ngtcp2_encode_ipv4(uint8_t *dest, const uint8_t *addr);

/*
 * ngtcp2_encode_ipv6 encodes binary form IPv6 address stored in
 * |addr| to human readable text form in the buffer pointed by |dest|.
 * The capacity of buffer must have enough length to store a text form
 * plus a terminating NULL byte.  The resulting text form ends with
 * NULL byte.  The function produces the canonical form of IPv6 text
 * representation described in
 * https://tools.ietf.org/html/rfc5952#section-4.  The function
 * returns |dest|.
 */
uint8_t *ngtcp2_encode_ipv6(uint8_t *dest, const uint8_t *addr);

/*
 * ngtcp2_encode_printable_ascii encodes |data| of length |len| in
 * |dest| in the following manner: printable ascii characters are
 * copied as is.  The other characters are converted to ".".  It
 * writes additional NULL bytes at the end of the buffer.  |dest| must
 * have at least |len| + 1 bytes.  This function returns |dest|.
 */
char *ngtcp2_encode_printable_ascii(char *dest, const uint8_t *data,
                                    size_t len);

/*
 * ngtcp2_cmemeq returns nonzero if the first |n| bytes of the buffers
 * pointed by |a| and |b| are equal.  The comparison is done in a
 * constant time manner.
 */
int ngtcp2_cmemeq(const uint8_t *a, const uint8_t *b, size_t n);

#endif /* NGTCP2_STR_H */
