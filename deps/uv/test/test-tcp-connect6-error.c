/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "uv.h"
#include "task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int connect_cb_called = 0;
static int close_cb_called = 0;


static void connect_cb(uv_connect_t* handle, int status) {
  ASSERT_NOT_NULL(handle);
  connect_cb_called++;
}


static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}


TEST_IMPL(tcp_connect6_error_fault) {
  const char garbage[] =
      "blah blah blah blah blah blah blah blah blah blah blah blah";
  const struct sockaddr_in6* garbage_addr;
  uv_tcp_t server;
  int r;
  uv_connect_t req;

  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");

  garbage_addr = (const struct sockaddr_in6*) &garbage;

  r = uv_tcp_init(uv_default_loop(), &server);
  ASSERT_OK(r);
  r = uv_tcp_connect(&req,
                     &server,
                     (const struct sockaddr*) garbage_addr,
                     connect_cb);
  ASSERT_EQ(r, UV_EINVAL);

  uv_close((uv_handle_t*)&server, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_OK(connect_cb_called);
  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_connect6_link_local) {
  uv_interface_address_t* ifs;
  uv_interface_address_t* p;
  struct sockaddr_in6 addr;
  uv_connect_t req;
  uv_tcp_t server;
  int ok;
  int n;

  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");

#if defined(__QEMU__)
  /* qemu's sockaddr_in6 translation is broken pre-qemu 8.0.0
   * when host endianness != guest endiannes.
   * Fixed in https://github.com/qemu/qemu/commit/44cf6731d6b.
   */
  RETURN_SKIP("Test does not currently work in QEMU");
#endif  /* defined(__QEMU__) */

  /* Check there's an interface that routes link-local (fe80::/10) traffic. */
  ASSERT_OK(uv_interface_addresses(&ifs, &n));
  for (p = ifs; p < &ifs[n]; p++)
    if (p->address.address6.sin6_family == AF_INET6)
      if (!memcmp(&p->address.address6.sin6_addr, "\xfe\x80", 2))
        break;
  ok = (p < &ifs[n]);
  uv_free_interface_addresses(ifs, n);

  if (!ok)
    RETURN_SKIP("IPv6 link-local traffic not supported");

  ASSERT_OK(uv_ip6_addr("fe80::0bad:babe", 1337, &addr));
  ASSERT_OK(uv_tcp_init(uv_default_loop(), &server));

  /* We're making two shaky assumptions here:
   * 1. There is a network interface that routes IPv6 link-local traffic, and
   * 2. There is no firewall rule that blackholes or otherwise hard-kills the
   *    connection attempt to the address above, i.e., we don't expect the
   *    connect() system call to fail synchronously.
   */
  ASSERT_OK(uv_tcp_connect(&req,
                           &server,
                           (struct sockaddr*) &addr,
                           connect_cb));

  uv_close((uv_handle_t*) &server, NULL);
  ASSERT_OK(uv_run(uv_default_loop(), UV_RUN_DEFAULT));

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
