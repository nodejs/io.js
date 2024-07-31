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


static int close_cb_called = 0;


static void close_cb(uv_handle_t* handle) {
  ASSERT_NOT_NULL(handle);
  close_cb_called++;
}


TEST_IMPL(tcp_bind6_error_addrinuse) {
  struct sockaddr_in6 addr;
  uv_tcp_t server1, server2;
  int r;

  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");

  ASSERT_OK(uv_ip6_addr("::", TEST_PORT, &addr));

  r = uv_tcp_init(uv_default_loop(), &server1);
  ASSERT_OK(r);
  r = uv_tcp_bind(&server1, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_tcp_init(uv_default_loop(), &server2);
  ASSERT_OK(r);
  r = uv_tcp_bind(&server2, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  r = uv_listen((uv_stream_t*)&server1, 128, NULL);
  ASSERT_OK(r);
  r = uv_listen((uv_stream_t*)&server2, 128, NULL);
  ASSERT_EQ(r, UV_EADDRINUSE);

  uv_close((uv_handle_t*)&server1, close_cb);
  uv_close((uv_handle_t*)&server2, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(2, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_bind6_error_addrnotavail) {
  struct sockaddr_in6 addr;
  uv_tcp_t server;
  int r;

  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");

  ASSERT_OK(uv_ip6_addr("4:4:4:4:4:4:4:4", TEST_PORT, &addr));

  r = uv_tcp_init(uv_default_loop(), &server);
  ASSERT_OK(r);
  r = uv_tcp_bind(&server, (const struct sockaddr*) &addr, 0);
  ASSERT_EQ(r, UV_EADDRNOTAVAIL);

  uv_close((uv_handle_t*)&server, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_bind6_error_fault) {
  char garbage[] =
      "blah blah blah blah blah blah blah blah blah blah blah blah";
  struct sockaddr_in6* garbage_addr;
  uv_tcp_t server;
  int r;

  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");

  garbage_addr = (struct sockaddr_in6*) &garbage;

  r = uv_tcp_init(uv_default_loop(), &server);
  ASSERT_OK(r);
  r = uv_tcp_bind(&server, (const struct sockaddr*) garbage_addr, 0);
  ASSERT_EQ(r, UV_EINVAL);

  uv_close((uv_handle_t*)&server, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

/* Notes: On Linux uv_bind6(server, NULL) will segfault the program.  */

TEST_IMPL(tcp_bind6_error_inval) {
  struct sockaddr_in6 addr1;
  struct sockaddr_in6 addr2;
  uv_tcp_t server;
  int r;

  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");

  ASSERT_OK(uv_ip6_addr("::", TEST_PORT, &addr1));
  ASSERT_OK(uv_ip6_addr("::", TEST_PORT_2, &addr2));

  r = uv_tcp_init(uv_default_loop(), &server);
  ASSERT_OK(r);
  r = uv_tcp_bind(&server, (const struct sockaddr*) &addr1, 0);
  ASSERT_OK(r);
  r = uv_tcp_bind(&server, (const struct sockaddr*) &addr2, 0);
  ASSERT_EQ(r, UV_EINVAL);

  uv_close((uv_handle_t*)&server, close_cb);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, close_cb_called);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


TEST_IMPL(tcp_bind6_localhost_ok) {
  struct sockaddr_in6 addr;
  uv_tcp_t server;
  int r;

  if (!can_ipv6())
    RETURN_SKIP("IPv6 not supported");

  ASSERT_OK(uv_ip6_addr("::1", TEST_PORT, &addr));

  r = uv_tcp_init(uv_default_loop(), &server);
  ASSERT_OK(r);
  r = uv_tcp_bind(&server, (const struct sockaddr*) &addr, 0);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
