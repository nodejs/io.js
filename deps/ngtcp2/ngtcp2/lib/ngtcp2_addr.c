/*
 * ngtcp2
 *
 * Copyright (c) 2019 ngtcp2 contributors
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
#include "ngtcp2_addr.h"

#include <string.h>
#include <assert.h>

#include "ngtcp2_unreachable.h"

ngtcp2_addr *ngtcp2_addr_init(ngtcp2_addr *dest, const ngtcp2_sockaddr *addr,
                              ngtcp2_socklen addrlen) {
  dest->addrlen = addrlen;
  dest->addr = (ngtcp2_sockaddr *)addr;
  return dest;
}

void ngtcp2_addr_copy(ngtcp2_addr *dest, const ngtcp2_addr *src) {
  dest->addrlen = src->addrlen;
  if (src->addrlen) {
    memcpy(dest->addr, src->addr, (size_t)src->addrlen);
  }
}

void ngtcp2_addr_copy_byte(ngtcp2_addr *dest, const ngtcp2_sockaddr *addr,
                           ngtcp2_socklen addrlen) {
  dest->addrlen = addrlen;
  if (addrlen) {
    memcpy(dest->addr, addr, (size_t)addrlen);
  }
}

static int sockaddr_eq(const ngtcp2_sockaddr *a, const ngtcp2_sockaddr *b) {
  assert(a->sa_family == b->sa_family);

  switch (a->sa_family) {
  case NGTCP2_AF_INET: {
    const ngtcp2_sockaddr_in *ai = (const ngtcp2_sockaddr_in *)(void *)a,
                             *bi = (const ngtcp2_sockaddr_in *)(void *)b;
    return ai->sin_port == bi->sin_port &&
           memcmp(&ai->sin_addr, &bi->sin_addr, sizeof(ai->sin_addr)) == 0;
  }
  case NGTCP2_AF_INET6: {
    const ngtcp2_sockaddr_in6 *ai = (const ngtcp2_sockaddr_in6 *)(void *)a,
                              *bi = (const ngtcp2_sockaddr_in6 *)(void *)b;
    return ai->sin6_port == bi->sin6_port &&
           memcmp(&ai->sin6_addr, &bi->sin6_addr, sizeof(ai->sin6_addr)) == 0;
  }
  default:
    ngtcp2_unreachable();
  }
}

int ngtcp2_addr_eq(const ngtcp2_addr *a, const ngtcp2_addr *b) {
  return a->addr->sa_family == b->addr->sa_family &&
         sockaddr_eq(a->addr, b->addr);
}

uint32_t ngtcp2_addr_compare(const ngtcp2_addr *aa, const ngtcp2_addr *bb) {
  uint32_t flags = NGTCP2_ADDR_COMPARE_FLAG_NONE;
  const ngtcp2_sockaddr *a = aa->addr;
  const ngtcp2_sockaddr *b = bb->addr;

  if (a->sa_family != b->sa_family) {
    return NGTCP2_ADDR_COMPARE_FLAG_FAMILY;
  }

  switch (a->sa_family) {
  case NGTCP2_AF_INET: {
    const ngtcp2_sockaddr_in *ai = (const ngtcp2_sockaddr_in *)(void *)a,
                             *bi = (const ngtcp2_sockaddr_in *)(void *)b;
    if (memcmp(&ai->sin_addr, &bi->sin_addr, sizeof(ai->sin_addr))) {
      flags |= NGTCP2_ADDR_COMPARE_FLAG_ADDR;
    }
    if (ai->sin_port != bi->sin_port) {
      flags |= NGTCP2_ADDR_COMPARE_FLAG_PORT;
    }
    return flags;
  }
  case NGTCP2_AF_INET6: {
    const ngtcp2_sockaddr_in6 *ai = (const ngtcp2_sockaddr_in6 *)(void *)a,
                              *bi = (const ngtcp2_sockaddr_in6 *)(void *)b;
    if (memcmp(&ai->sin6_addr, &bi->sin6_addr, sizeof(ai->sin6_addr))) {
      flags |= NGTCP2_ADDR_COMPARE_FLAG_ADDR;
    }
    if (ai->sin6_port != bi->sin6_port) {
      flags |= NGTCP2_ADDR_COMPARE_FLAG_PORT;
    }
    return flags;
  }
  default:
    ngtcp2_unreachable();
  }
}

int ngtcp2_addr_empty(const ngtcp2_addr *addr) { return addr->addrlen == 0; }
