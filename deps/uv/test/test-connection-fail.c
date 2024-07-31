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

#include <stdlib.h>
#include <stdio.h>


static uv_tcp_t tcp;
static uv_connect_t req;
static int connect_cb_calls;
static int close_cb_calls;

static uv_timer_t timer;
static int timer_close_cb_calls;
static int timer_cb_calls;


static void on_close(uv_handle_t* handle) {
  close_cb_calls++;
}


static void timer_close_cb(uv_handle_t* handle) {
  timer_close_cb_calls++;
}


static void timer_cb(uv_timer_t* handle) {
  timer_cb_calls++;

  /*
   * These are the important asserts. The connection callback has been made,
   * but libuv hasn't automatically closed the socket. The user must
   * uv_close the handle manually.
   */
  ASSERT_OK(close_cb_calls);
  ASSERT_EQ(1, connect_cb_calls);

  /* Close the tcp handle. */
  uv_close((uv_handle_t*)&tcp, on_close);

  /* Close the timer. */
  uv_close((uv_handle_t*)handle, timer_close_cb);
}


static void on_connect_with_close(uv_connect_t *req, int status) {
  ASSERT_PTR_EQ((uv_stream_t*) &tcp, req->handle);
  ASSERT_EQ(status, UV_ECONNREFUSED);
  connect_cb_calls++;

  ASSERT_OK(close_cb_calls);
  uv_close((uv_handle_t*)req->handle, on_close);
}


static void on_connect_without_close(uv_connect_t *req, int status) {
  ASSERT_EQ(status, UV_ECONNREFUSED);
  connect_cb_calls++;

  uv_timer_start(&timer, timer_cb, 100, 0);

  ASSERT_OK(close_cb_calls);
}


static void connection_fail(uv_connect_cb connect_cb) {
  struct sockaddr_in client_addr, server_addr;
  int r;

  ASSERT_OK(uv_ip4_addr("0.0.0.0", 0, &client_addr));

  /* There should be no servers listening on this port. */
  ASSERT_OK(uv_ip4_addr("127.0.0.1", TEST_PORT, &server_addr));

  /* Try to connect to the server and do NUM_PINGS ping-pongs. */
  r = uv_tcp_init(uv_default_loop(), &tcp);
  ASSERT(!r);

  /* We are never doing multiple reads/connects at a time anyway. so these
   * handles can be pre-initialized. */
  ASSERT_OK(uv_tcp_bind(&tcp, (const struct sockaddr*) &client_addr, 0));

  r = uv_tcp_connect(&req,
                     &tcp,
                     (const struct sockaddr*) &server_addr,
                     connect_cb);
  ASSERT(!r);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT_EQ(1, connect_cb_calls);
  ASSERT_EQ(1, close_cb_calls);
}


/*
 * This test attempts to connect to a port where no server is running. We
 * expect an error.
 */
TEST_IMPL(connection_fail) {
/* TODO(gengjiawen): Fix test on QEMU. */
#if defined(__QEMU__)
  RETURN_SKIP("Test does not currently work in QEMU");
#endif

  connection_fail(on_connect_with_close);

  ASSERT_OK(timer_close_cb_calls);
  ASSERT_OK(timer_cb_calls);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}


/*
 * This test is the same as the first except it check that the close
 * callback of the tcp handle hasn't been made after the failed connection
 * attempt.
 */
TEST_IMPL(connection_fail_doesnt_auto_close) {
/* TODO(gengjiawen): Fix test on QEMU. */
#if defined(__QEMU__)
  RETURN_SKIP("Test does not currently work in QEMU");
#endif

  int r;

  r = uv_timer_init(uv_default_loop(), &timer);
  ASSERT_OK(r);

  connection_fail(on_connect_without_close);

  ASSERT_EQ(1, timer_close_cb_calls);
  ASSERT_EQ(1, timer_cb_calls);

  MAKE_VALGRIND_HAPPY(uv_default_loop());
  return 0;
}
