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


static int connect_cb_called;
static int close_cb_called;

static uv_connect_t connect_req;
static uv_timer_t timer;
static uv_tcp_t conn;

static void connect_cb(uv_connect_t* req, int status);
static void timer_cb(uv_timer_t* handle);
static void close_cb(uv_handle_t* handle);


static void connect_cb(uv_connect_t* req, int status) {
  ASSERT_PTR_EQ(req, &connect_req);
  ASSERT_EQ(status, UV_ECANCELED);
  connect_cb_called++;
}


static void timer_cb(uv_timer_t* handle) {
  ASSERT_PTR_EQ(handle, &timer);
  uv_close((uv_handle_t*)&conn, close_cb);
  uv_close((uv_handle_t*)&timer, close_cb);
}


static void close_cb(uv_handle_t* handle) {
  ASSERT(handle == (uv_handle_t*)&conn || handle == (uv_handle_t*)&timer);
  close_cb_called++;
}


/* Verify that connecting to an unreachable address or port doesn't hang
 * the event loop.
 */
TEST_IMPL(tcp_connect_timeout) {
  struct sockaddr_in addr;
  int r;

  ASSERT_OK(uv_ip4_addr("8.8.8.8", 9999, &addr));

  r = uv_timer_init(uv_default_loop(), &timer);
  ASSERT_OK(r);

  r = uv_timer_start(&timer, timer_cb, 50, 0);
  ASSERT_OK(r);

  r = uv_tcp_init(uv_default_loop(), &conn);
  ASSERT_OK(r);

  r = uv_tcp_connect(&connect_req,
                     &conn,
                     (const struct sockaddr*) &addr,
                     connect_cb);
  if (r == UV_ENETUNREACH)
    RETURN_SKIP("Network unreachable.");
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

/* Make sure connect fails instantly if the target is nonexisting
 * local port.
 */

static void connect_local_cb(uv_connect_t* req, int status) {
  ASSERT_PTR_EQ(req, &connect_req);
  ASSERT_NE(status, UV_ECANCELED);
  connect_cb_called++;
}

static int is_supported_system(void) {
  int semver[3];
  int min_semver[3] = {10, 0, 16299};
  int cnt;
  uv_utsname_t uname;
  ASSERT_OK(uv_os_uname(&uname));
  if (strcmp(uname.sysname, "Windows_NT") == 0) {
    cnt = sscanf(uname.release, "%d.%d.%d", &semver[0], &semver[1], &semver[2]);
    if (cnt != 3) {
      return 0;
    }
    /* release >= 10.0.16299 */
    for (cnt = 0; cnt < 3; ++cnt) {
      if (semver[cnt] > min_semver[cnt])
        return 1;
      if (semver[cnt] < min_semver[cnt])
        return 0;
    }
    return 1;
  }
  return 1;
}

TEST_IMPL(tcp_local_connect_timeout) {
  struct sockaddr_in addr;
  int r;

  if (!is_supported_system()) {
    RETURN_SKIP("Unsupported system");
  }
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &addr));

  r = uv_timer_init(uv_default_loop(), &timer);
  ASSERT_OK(r);

  /* Give it 1s to timeout. */
  r = uv_timer_start(&timer, timer_cb, 1000, 0);
  ASSERT_OK(r);

  r = uv_tcp_init(uv_default_loop(), &conn);
  ASSERT_OK(r);

  r = uv_tcp_connect(&connect_req,
                     &conn,
                     (const struct sockaddr*) &addr,
                     connect_local_cb);
  if (r == UV_ENETUNREACH)
    RETURN_SKIP("Network unreachable.");
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}

TEST_IMPL(tcp6_local_connect_timeout) {
  struct sockaddr_in6 addr;
  int r;

  if (!is_supported_system()) {
    RETURN_SKIP("Unsupported system");
  }
  if (!can_ipv6()) {
    RETURN_SKIP("IPv6 not supported");
  }

  ASSERT_OK(uv_ip6_addr("::1", 9999, &addr));

  r = uv_timer_init(uv_default_loop(), &timer);
  ASSERT_OK(r);

  /* Give it 1s to timeout. */
  r = uv_timer_start(&timer, timer_cb, 1000, 0);
  ASSERT_OK(r);

  r = uv_tcp_init(uv_default_loop(), &conn);
  ASSERT_OK(r);

  r = uv_tcp_connect(&connect_req,
                     &conn,
                     (const struct sockaddr*) &addr,
                     connect_local_cb);
  if (r == UV_ENETUNREACH)
    RETURN_SKIP("Network unreachable.");
  ASSERT_OK(r);

  r = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  ASSERT_OK(r);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
